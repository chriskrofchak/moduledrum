// The master effects (.clew increment 0009): MasterFx, the mixer DSP's master section run on a second emulated DSP, and the
// host side that feeds it the OS's own parameter words (HostModel::setMasterFx and the group-per-tick pipeline). Needs the OS
// file; the cases that use the preset kits also need MD_KITS_SYX.
//   mfx_*      MasterFx against the references: the interpreter, and DSP1's own whole main loop (tests/Dsp1Reference)
//   mhost_*    the OS side: the 38 words, the slew, the tempo, the one-group-per-tick cadence
//   gmaster_*  the master output of the golden pattern, with the engine (ctest golden-master)
#include "mini_test.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <pthread.h>
#endif
#include "Dsp1Reference.h"
#include "Firmware.h"
#include "MachineRunner.h"
#include "MdEngine.h"
#include "dsp56kEmu/dsp.h"
#include "golden/GoldenPattern.h"
#include "md/library/MdSysex.h"
using namespace md;
using namespace md::engine;

#if defined(__APPLE__)
// libmalloc's logging hook (what MallocStackLogging uses), exported by libsystem_malloc: while it is set, every allocation in
// every malloc zone calls it. A JIT compile allocates (asmjit's code holder), so "no allocation in a live block" also means
// "no compile in a live block".
extern "C" void (*malloc_logger)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result, uint32_t skip);
#endif

namespace {
struct Os {
    fw::Firmware firmware;
    std::vector<uint8_t> image;   // section 0, the ColdFire OS
};
const Os& os()
{
    static Os o = [] {
        const auto path = mt::requireOsFile();
        Os r{fw::loadFirmware(path), {}};
        r.image = fw::parseContainer(fw::parseSysex(fw::readFile(path))).sections.at(0).data;
        return r;
    }();
    return o;
}

// The preset kits of MD_KITS_SYX (the 16 real ones in slots 0-15), or a skip
const library::Bank& kits()
{
    static library::Bank b = [] {
        const char* env = std::getenv("MD_KITS_SYX");
        const std::string p = env ? env : "";
        if (p.empty() || !std::filesystem::exists(p)) throw mt::Skip{"no kit snapshot (set MD_KITS_SYX to a Machinedrum .syx with kits)"};
        return library::loadSyxFile(p);
    }();
    return b;
}
bool haveKits() { const char* e = std::getenv("MD_KITS_SYX"); return e && *e && std::filesystem::exists(e); }

std::string hex(uint64_t v) { std::ostringstream o; o << std::hex << std::setw(16) << std::setfill('0') << v; return o.str(); }
std::string hex6(uint32_t v) { std::ostringstream o; o << std::hex << std::setw(6) << std::setfill('0') << v; return o.str(); }

// An engine for its host (the OS side: master words, mixer inputs) and its native chain; no master section of its own
std::unique_ptr<Engine> chainEngine() { return std::make_unique<Engine>(os().firmware, os().image, EngineOptions{false}); }

struct Rng {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// A random kit's master bytes, extremes included, as the spike's JIT-vs-interpreter test drew them
std::array<uint8_t, 32> randomMasterBytes(Rng& r)
{
    std::array<uint8_t, 32> fx{};
    for (auto& b : fx) {
        const unsigned k = r.next() % 8;
        b = k == 0 ? 0 : k == 1 ? 127 : uint8_t(r.next() % 128);
    }
    if (r.next() % 3 == 0) { fx[6] = 127; fx[28] = uint8_t(r.next() % 40); fx[31] = uint8_t(r.next() % 128); }
    return fx;
}

// Random buses: per bus a kind of signal (silence, noise, full-scale square, sparse impulses), changed with the kit
struct Signal {
    std::array<int, 3> kind{};
    void pick(Rng& r) { for (auto& k : kind) k = int(r.next() % 4); }
    void fill(Rng& r, int block, Mixer::Output& in) const
    {
        for (int f = 0; f < 32; ++f)
            for (size_t c = 0; c < 2; ++c) {
                const int i = 2 * f + int(c);
                auto gen = [&](int bus) -> int32_t {
                    switch (kind[size_t(bus)]) {
                        case 1: return (int32_t(r.next() << 8) >> 8) >> 2;
                        case 2: return ((i / 2 + block) & 16) ? 0x7fffff : -0x800000;
                        case 3: return (block % 7 == 0 && i == 0) ? 0x600000 : 0;
                        default: return 0;
                    }
                };
                in.main[size_t(f)][c] = gen(0);
                in.rev[size_t(f)][c] = gen(1);
                in.del[size_t(f)][c] = gen(2);
            }
        for (int f = 0; f < 32; ++f)
            for (size_t c = 0; c < 6; ++c) in.frame[size_t(f)][c] = (c == 2 || c == 5) && kind[0] == 1 ? int32_t(r.next() << 8) >> 12 : 0;
    }
};

// Every word of the master pass's state that two MasterFx instances must agree on: the pass's scratch and buffers, the
// whole of Y below the loop variables, the dynamix ring and a stride sample of the echo and reverb rings (about 3800 words)
long compareState(const MasterFx& a, const MasterFx& b, long& compared)
{
    long bad = 0;
    auto x = [&](uint32_t from, uint32_t to, uint32_t step = 1) { for (uint32_t i = from; i < to; i += step) { ++compared; bad += a.readX(i) != b.readX(i); } };
    auto y = [&](uint32_t from, uint32_t to) { for (uint32_t i = from; i < to; ++i) { ++compared; bad += a.readY(i) != b.readY(i); } };
    x(0, 0x200); x(0x400, 0x4c0); x(0x600, 0x640); x(0x143d00, 0x143dc0);
    y(0, 0x1ca);
    x(0x1000da, 0x138000, 61);
    return bad;
}

bool masterFxIdentical(const Mixer::Stereo& a, const Mixer::Stereo& b, long& bad)
{
    bool same = true;
    for (size_t f = 0; f < 32; ++f)
        for (size_t c = 0; c < 2; ++c)
            if (a[f][c] != b[f][c]) { same = false; ++bad; }
    return same;
}

// The synthetic word sets the engine warms the master up with (HostModel::masterWarmUpWords), for the cases that do the same
std::vector<uint32_t> warmUpSets(HostModel<VoiceEngine>& host, int sets)
{
    std::vector<uint32_t> words(size_t(sets) * master::kWords);
    host.masterWarmUpWords(sets, words.data());
    return words;
}
}

// ---------------------------------------------------------------------------------------------------------------------
// MasterFx

// 1(a): the JIT and the interpreter agree on the master section: two instances fed the same random kits, extremes and
// signals (silence, noise, full scale, impulses), every output and state word compared after every block
TEST_CASE(mfx_jit_vs_interpreter)
{
    if (!dsp56k::g_useJIT) throw mt::Skip{"no JIT in this build (MD_USE_JIT=OFF): both instances would be the interpreter"};
    auto chain = chainEngine();
    MasterFx jit(os().firmware), ref(os().firmware, true);
    Rng rng{12345};
    Signal sig;
    Mixer::Output in;
    Mixer::Stereo outJ, outR;
    constexpr int kBlocks = 3200;
    long compared = 0, bad = 0, kitChanges = 0;
    for (int b = 0; b < kBlocks; ++b) {
        if (b % 40 == 0) {
            const auto fx = randomMasterBytes(rng);
            uint32_t words[master::kWords];
            chain->host().computeMasterWords(fx.data(), 60.0 + rng.next() % 180, words);
            jit.setWords(words); ref.setWords(words);
            sig.pick(rng);
            ++kitChanges;
        }
        sig.fill(rng, b, in);
        CHECK_MSG(jit.process(in, outJ), "JIT: " << jit.faultReason());
        CHECK_MSG(ref.process(in, outR), "interpreter: " << ref.faultReason());
        masterFxIdentical(outJ, outR, bad);
        bad += compareState(jit, ref, compared);
        compared += 64;
    }
    std::printf("mfx_jit_vs_interpreter: %d blocks, %ld random kits, %ld words compared, %ld differences\n", kBlocks, kitChanges, compared, bad);
    CHECK_EQ(bad, 0L);
}

namespace {
// 1(b): the master-only harness against DSP1's whole main loop on the golden pattern. The native chain (voice DSP, TrackFx,
// Mixer) feeds MasterFx; the same voice blocks and OS words feed the whole loop (16 track functions, the mix, the master
// section). After every block: frame channels 2 and 5 (the master output), the master's final block X:$180 and the master's
// whole Y state (Y:$150-$1c1: parameters, filter states, dynamix, meters) must be the same word for word.
// _edits: change some master bytes mid-run (an echo TIME and FB change, then the gate, EQ and compressor), so that the
// words keep moving through the OS slew and the group cadence while both DSPs run.
long runAgainstWholeLoop(const std::array<uint8_t, 32>& bytes, int blocks, const char* label, long& compared, bool edits = true)
{
    auto chain = chainEngine();
    auto& host = chain->host();
    MasterFx master(os().firmware);
    test::Dsp1Reference ref(os().firmware);
    // both start with the pass on silence and the init's parameters (the loop's first pass, the master pass from $342)
    CHECK_MSG(ref.startUp(), ref.fault());
    Mixer::Output silence;
    Mixer::Stereo out;
    CHECK(master.process(silence, out));

    bool primed = false;
    auto deliver = [&] {
        const unsigned groups = host.takeMasterGroups();
        for (int g = 0; g < master::kGroups; ++g) {
            if (!(groups & (1u << g))) continue;
            const auto& gr = master::kGroup[g];
            const uint32_t* w = host.masterWords().data() + gr.offset;
            master.writeGroup(g, w);
            for (int i = 0; i < gr.count; ++i) ref.setY(gr.y + uint32_t(i), w[i]);
            if (g == 0 && !primed) { ref.setY(0x15a, w[0]); primed = true; }	// the echo starts at its delay, as MasterFx does
        }
    };
    for (int i = 0; i < master::kParams; ++i) host.setMasterFx(i, bytes[size_t(i)]);
    host.primeMasterFx();
    deliver();

    golden::Pattern pattern;
    pattern.setup(host);
    Engine::Output eo;
    long bad = 0;
    for (int b = 0; b < blocks; ++b) {
        pattern.beforeBlock(host, b);
        if (edits && b == blocks / 3) { host.setMasterFx(8, 100); host.setMasterFx(11, 90); host.setMasterFx(12, 10); }	// echo TIME up, FB, FILTF
        if (edits && b == 2 * blocks / 3) {
            host.setMasterFx(14, 0); host.setMasterFx(6, 40); host.setMasterFx(21, 100); host.setMasterFx(26, 30); host.setMasterFx(8, 3);	// echo MONO, gate on, EQ PG, compressor threshold, TIME down
        }
        CHECK_MSG(chain->render(eo), chain->fault());
        deliver();
        test::Dsp1Reference::Voices v = chain->voiceOutput();
        test::Dsp1Reference::FxWords fx;
        test::Dsp1Reference::MixWords mix;
        for (size_t t = 0; t < 16; ++t) { fx[t] = host.mixerInput(int(t)).fx; mix[t] = host.mixerInput(int(t)).mix; }
        CHECK_MSG(ref.runBlock(v, fx, mix), ref.fault());
        CHECK_MSG(master.process(eo.mix, out), master.faultReason());
        for (uint32_t f = 0; f < 32; ++f) {
            ++compared; bad += (uint32_t(out[f][0]) & 0xffffff) != ref.x(0x400 + 6 * f + 2);
            ++compared; bad += (uint32_t(out[f][1]) & 0xffffff) != ref.x(0x400 + 6 * f + 5);
        }
        for (uint32_t a = 0x180; a < 0x1c0; ++a) { ++compared; bad += master.readX(a) != ref.x(a); }
        for (uint32_t a = 0x150; a < 0x1c2; ++a) { ++compared; bad += master.readY(a) != ref.y(a); }
        if (bad && b < 10000) { std::printf("  %s: block %d: %ld words differ so far\n", label, b, bad); if (bad > 20) break; }
    }
    return bad;
}
}

TEST_CASE(mfx_vs_whole_dsp1_loop)
{
    struct Set { std::string name; std::array<uint8_t, 32> bytes; };
    std::vector<Set> sets;
    if (haveKits()) {
        // the spike's three: EFM (a long echo), BOOGIE and ALIENS (reverb and compressor)
        for (const size_t k : {size_t(1), size_t(9), size_t(13)}) sets.push_back({"kit " + kits().kits.at(k).name, kits().kits.at(k).masterFx.bytes()});
    } else {
        sets.push_back({"default", master::kDefaultParams});
        sets.push_back({"dry", golden::kMasterDry});
        sets.push_back({"wet", golden::kMasterWet});
    }
    constexpr int kBlocks = 4000;
    long compared = 0, bad = 0;
    for (const auto& s : sets) {
        long c = 0;
        const long b = runAgainstWholeLoop(s.bytes, kBlocks, s.name.c_str(), c);
        std::printf("mfx_vs_whole_dsp1_loop: %s, %d blocks, %ld words compared, %ld differences\n", s.name.c_str(), kBlocks, c, b);
        compared += c; bad += b;
    }
    std::printf("mfx_vs_whole_dsp1_loop: %zu sets, %ld words compared, %ld differences\n", sets.size(), compared, bad);
    CHECK_EQ(bad, 0L);
}

namespace {
#if defined(__APPLE__)
std::atomic<uint64_t> g_mallocs{0};
pthread_t g_countThread;
void (*g_prevLogger)(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) = nullptr;
void countingLogger(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip)
{
    if ((type & 2) != 0 && pthread_equal(pthread_self(), g_countThread)) g_mallocs.fetch_add(1, std::memory_order_relaxed);   // MALLOC_LOG_TYPE_ALLOCATE
    if (g_prevLogger) g_prevLogger(type, a1, a2, a3, result, skip);
}
struct AllocCount {
    AllocCount() { g_countThread = pthread_self(); g_prevLogger = malloc_logger; malloc_logger = countingLogger; }
    ~AllocCount() { malloc_logger = g_prevLogger; }
    static uint64_t now() { return g_mallocs.load(std::memory_order_relaxed); }
};
constexpr bool kCanCountAllocs = true;
#else
struct AllocCount { static uint64_t now() { return 0; } };
constexpr bool kCanCountAllocs = false;
#endif

double microseconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
{
    return std::chrono::duration<double, std::micro>(b - a).count();
}
}

// 1(c): a block after the warm-up. The warm-up compiles everything the program can reach (random kits and signals below
// take paths it never saw: no allocation, so no JIT compile either, in any of 4000 live blocks) and a block costs < 25 us.
TEST_CASE(mfx_block_cost_and_no_compile)
{
    auto chain = chainEngine();
    const auto sets = warmUpSets(chain->host(), Engine::kMasterWarmUpSets);
    // a cold instance: the first block compiles the program as it goes
    double coldFirst = 0;
    {
        MasterFx cold(os().firmware);
        cold.setWords(sets.data());
        Mixer::Output in;
        Mixer::Stereo out;
        Rng rng{777};
        Signal sig; sig.pick(rng);
        sig.fill(rng, 1, in);
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(cold.process(in, out));
        coldFirst = microseconds(t0, std::chrono::steady_clock::now());
    }
    MasterFx m(os().firmware);
    const auto w0 = std::chrono::steady_clock::now();
    m.warmUp(sets.data(), Engine::kMasterWarmUpSets);
    const double warmMs = microseconds(w0, std::chrono::steady_clock::now()) / 1000.0;
    CHECK_MSG(m.faultReason().empty(), m.faultReason());

    Rng rng{4242};
    Signal sig;
    Mixer::Output in;
    Mixer::Stereo out;
    constexpr int kBlocks = 4000;
    std::vector<double> us;
    us.reserve(kBlocks);
    uint64_t allocBlocks = 0, allocs = 0;
    long kitChanges = 0;
    {
        AllocCount counter;
        for (int b = 0; b < kBlocks; ++b) {
            if (b % 40 == 0) {
                const auto fx = randomMasterBytes(rng);
                uint32_t words[master::kWords];
                chain->host().computeMasterWords(fx.data(), 60.0 + rng.next() % 180, words);
                m.setWords(words);
                sig.pick(rng);
                ++kitChanges;
            }
            sig.fill(rng, b, in);
            const uint64_t before = AllocCount::now();
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = m.process(in, out);
            const auto t1 = std::chrono::steady_clock::now();
            const uint64_t after = AllocCount::now();
            CHECK_MSG(ok, m.faultReason());
            us.push_back(microseconds(t0, t1));
            allocs += after - before;
            allocBlocks += after != before;
        }
    }
    std::vector<double> sorted = us;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    for (double u : us) sum += u;
    const double median = sorted[sorted.size() / 2], mean = sum / double(us.size());
    std::printf("mfx_block_cost_and_no_compile: warm-up %.1f ms (cold first block %.0f us); %d live blocks, %ld random kits: "
                "median %.1f us, mean %.1f us, p99 %.1f us, max %.1f us; %llu allocations in %llu blocks%s\n",
                warmMs, coldFirst, kBlocks, kitChanges, median, mean, sorted[sorted.size() * 99 / 100], sorted.back(),
                (unsigned long long)allocs, (unsigned long long)allocBlocks, kCanCountAllocs ? "" : " (not counted on this platform)");
    if (dsp56k::g_useJIT) {
        CHECK_MSG(median < 25.0, "median block " << median << " us");
        CHECK_MSG(mean < 25.0, "mean block " << mean << " us");
        CHECK_MSG(coldFirst > 1000.0, "the cold first block took only " << coldFirst << " us: nothing was compiled in it?");
        if (kCanCountAllocs) CHECK_EQ(allocs, uint64_t(0));
    } else {
        CHECK_MSG(median < 400.0, "median block " << median << " us");
    }
}

// The warm-up leaves an instance as a fresh one is: same blocks in, same blocks out and the same state
TEST_CASE(mfx_warmup_leaves_a_fresh_state)
{
    auto chain = chainEngine();
    const auto sets = warmUpSets(chain->host(), Engine::kMasterWarmUpSets);
    MasterFx fresh(os().firmware), warmed(os().firmware);
    warmed.warmUp(sets.data(), Engine::kMasterWarmUpSets);
    Rng rng{99};
    Signal sig;
    Mixer::Output in;
    Mixer::Stereo a, b;
    long compared = 0, bad = 0;
    for (int blk = 0; blk < 600; ++blk) {
        if (blk % 50 == 0) {
            const auto fx = randomMasterBytes(rng);
            uint32_t words[master::kWords];
            chain->host().computeMasterWords(fx.data(), 125.0, words);
            fresh.setWords(words); warmed.setWords(words);
            sig.pick(rng);
        }
        sig.fill(rng, blk, in);
        CHECK(fresh.process(in, a));
        CHECK(warmed.process(in, b));
        masterFxIdentical(a, b, bad);
        bad += compareState(fresh, warmed, compared);
    }
    std::printf("mfx_warmup_leaves_a_fresh_state: 600 blocks, %ld words compared, %ld differences\n", compared, bad);
    CHECK_EQ(bad, 0L);
}

// ---------------------------------------------------------------------------------------------------------------------
// The OS side

namespace {
// A HostModel on a fresh OS image of its own, so that the smoothed state starts where a new engine's does
struct HostRig {
    MachineRunner runner;
    std::unique_ptr<HostModel<VoiceEngine>> host;
    explicit HostRig(VoiceEngine& voices) : runner(std::vector<uint8_t>(os().image)), host(std::make_unique<HostModel<VoiceEngine>>(runner, voices)) {}
    uint16_t smoothed(int i) const { return runner.peek16(0x1000d9c + 2 * uint32_t(i)); }
};
VoiceEngine& sharedVoices()
{
    static VoiceEngine v(os().firmware);	// the host's slot writes need one; nothing here renders
    return v;
}
}

// 2: the 38 words of the spike's test vector: kit "EFM" of the preset file at 120 BPM with the smoothed values exactly v << 7
// (its master bytes are written out here, so no kit file is needed)
TEST_CASE(mhost_test_vector)
{
    const std::array<uint8_t, 32> efm = {105, 21, 68, 0, 1, 68, 127, 109,   4, 0, 32, 34, 0, 57, 0, 103,
                                         64, 64, 64, 64, 64, 64, 64, 127,   84, 92, 94, 18, 127, 112, 21, 0};
    static const uint32_t expected[master::kWords] = {
        // Y:$150..$158
        0x017018, 0x004001, 0x000000, 0x010200, 0x220000, 0x0078b4, 0x071408, 0x7ffe00, 0x338000,
        // Y:$170..$179
        0x200000, 0xe05200, 0x7eb900, 0x200000, 0xfea900, 0x056000, 0x018abe, 0x008dad, 0x080000, 0x7e0200,
        // Y:$17a..$184
        0x00b0ee, 0x000b7b, 0x023b0d, 0x7e72df, 0x000000, 0x020c07, 0x1a2779, 0x012967, 0x38ae80, 0x0403fa, 0x2aaaaa,
        // Y:$185..$18c
        0x0085f9, 0x093100, 0x000022, 0x000000, 0x23bf00, 0x7fffff, 0x690000, 0x000a80,
    };
    HostRig rig(sharedVoices());
    uint32_t words[master::kWords];
    rig.host->computeMasterWords(efm.data(), 120.0, words);
    int wrong = 0;
    for (int i = 0; i < master::kWords; ++i) {
        if (words[i] != expected[i]) { std::printf("  word %d: %s, expected %s\n", i, hex6(words[i]).c_str(), hex6(expected[i]).c_str()); ++wrong; }
    }
    CHECK_EQ(wrong, 0);
    // computing them on the side leaves the host as it was: its own words are those of the default kit
    const auto before = rig.host->masterWords();
    uint32_t again[master::kWords];
    rig.host->computeMasterWords(efm.data(), 120.0, again);
    CHECK(rig.host->masterWords() == before);
    CHECK(std::equal(std::begin(words), std::end(words), std::begin(again)));
}

// 2: the words of the 16 real preset kits after the OS's slew has settled (from the cleared kit's settled state, 125 BPM),
// as one checksum, so a change in the pipeline shows
TEST_CASE(mhost_preset_kits_converged_words)
{
    constexpr uint64_t kExpected = 0x3f50575c608c9c35ull;
    uint64_t h = 1469598103934665603ull;
    int n = 0;
    for (size_t k = 0; k < 16; ++k) {
        const auto& kit = kits().kits.at(k);
        CHECK(!kit.isFree());
        HostRig rig(sharedVoices());
        const auto bytes = kit.masterFx.bytes();
        for (int i = 0; i < master::kParams; ++i) rig.host->setMasterFx(i, bytes[size_t(i)]);
        rig.host->primeMasterFx();
        for (uint32_t w : rig.host->masterWords()) { h ^= w; h *= 1099511628211ull; }
        ++n;
    }
    std::printf("mhost_preset_kits_converged_words: %d kits, checksum %s\n", n, hex(h).c_str());
    CHECK_MSG(h == kExpected, "checksum " << hex(h) << ", expected " << hex(kExpected));
}

// 2: the OS's slew ends 3 units below a target it approaches from below, exact from above (the hardware's quirk, kept)
TEST_CASE(mhost_slew_quirk)
{
    HostRig rig(sharedVoices());
    const int lev = 15;		// echo LEV, default 96
    CHECK_EQ(int(rig.smoothed(lev)), 96 * 128 - 3);	// settled at the cleared kit: it came from 0, from below
    rig.host->setMasterFx(lev, 127);
    for (int t = 0; t < 120; ++t) rig.host->tick();
    CHECK_EQ(int(rig.smoothed(lev)), 127 * 128 - 3);	// from below: 3 under the target << 7
    rig.host->setMasterFx(lev, 10);
    for (int t = 0; t < 120; ++t) rig.host->tick();
    CHECK_EQ(int(rig.smoothed(lev)), 10 * 128);			// from above: exact
    rig.host->setMasterFx(lev, 11);
    for (int t = 0; t < 120; ++t) rig.host->tick();
    CHECK_EQ(int(rig.smoothed(lev)), 11 * 128 - 3);		// one step up: below again
    rig.host->setMasterFx(lev, 0);
    for (int t = 0; t < 120; ++t) rig.host->tick();
    CHECK_EQ(int(rig.smoothed(lev)), 0);
    // the track levels' smoothing next to it is untouched by the master (they sit at their settings)
    CHECK_EQ(int(rig.runner.peek16(0x1000d7c)), 100 << 7);
}

// 2: a tempo change moves Y:$150 and Y:$151 (the echo's delay offset and its read rate) and nothing else
TEST_CASE(mhost_tempo_moves_echo_time_only)
{
    HostRig rig(sharedVoices());
    // the wet set, with a long echo TIME (byte 8): 100 x 82688 / BPM samples, 66 k at 125 BPM, longer than the ring below 85 BPM
    auto bytes = golden::kMasterWet;
    bytes[8] = 100;
    for (int i = 0; i < master::kParams; ++i) rig.host->setMasterFx(i, bytes[size_t(i)]);
    rig.host->primeMasterFx();
    const auto before = rig.host->masterWords();
    auto change = [&](double bpm) {
        rig.host->setTempo(bpm);
        for (int t = 0; t < 4; ++t) rig.host->tick();		// one tick per group
        return rig.host->masterWords();
    };
    auto movedWords = [&](const std::array<uint32_t, master::kWords>& a, const std::array<uint32_t, master::kWords>& b) {
        std::vector<size_t> moved;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) moved.push_back(i);
        return moved;
    };
    // 125 -> 90 BPM: the delay offset (Y:$150) moves, the read rate (Y:$151, 1.0 while the delay fits the ring) does not
    const auto slower = change(90.0);
    CHECK((movedWords(before, slower) == std::vector<size_t>{0}));
    // -> 30 BPM: the delay no longer fits the ring (Y:$150 = $20) and the rate drops below 1.0
    const auto slowest = change(30.0);
    CHECK((movedWords(slower, slowest) == std::vector<size_t>{0, 1}));
    CHECK_EQ(slowest[0], 0x20u);
    CHECK(slowest[1] != 0x4001u);
    // and back
    CHECK(change(125.0) == before);
}

// 2: one group per tick, in the order of the OS's tick counter (echo, dynamix, EQ, reverb), and a group only sees a change
// when its tick comes
TEST_CASE(mhost_one_group_per_tick)
{
    HostRig rig(sharedVoices());
    CHECK_EQ(rig.host->takeMasterGroups(), 0xfu);		// the priming marked all four
    CHECK_EQ(rig.host->takeMasterGroups(), 0u);
    for (int t = 0; t < 12; ++t) {
        rig.host->tick();
        CHECK_EQ(rig.host->takeMasterGroups(), 1u << (t & 3));
    }
    // EQ GAIN (kit byte 23) belongs to group 2: its word (Y:$179, index 18 of the 38) changes on the third tick after the edit
    const auto before = rig.host->masterWords();
    rig.host->setMasterFx(23, 40);
    rig.host->tick();		// tick 12 (group 0)
    CHECK_EQ(rig.host->masterWords()[18], before[18]);
    rig.host->tick();		// group 1
    CHECK_EQ(rig.host->masterWords()[18], before[18]);
    rig.host->tick();		// group 2: the smoothed value has moved by now
    CHECK(rig.host->masterWords()[18] != before[18]);
    CHECK_EQ(rig.host->takeMasterGroups(), 7u);
    // the same through the engine's render: a tick every 11 blocks, so a group per 11 blocks reaches the DSP
    auto eng = std::make_unique<Engine>(os().firmware, os().image);
    CHECK(eng->hasMaster());
    Engine::Output out;
    const uint32_t y150 = eng->master()->readY(0x150);
    CHECK_EQ(y150, eng->host().masterWords()[0]);		// primed: the first block is at the kit's settings
    CHECK_EQ(eng->master()->readY(0x15a), y150);		// and the echo does not glide in from DSP1's power-on offset
    eng->host().setTempo(60.0);
    for (int b = 0; b < 44; ++b) eng->render(out);	// four ticks
    CHECK(eng->master()->readY(0x150) != y150);
}

// ---------------------------------------------------------------------------------------------------------------------
// The golden pattern's master output

namespace {
// The master-output hash of the golden pattern with these master bytes (engine primed on them), and the dry hash next to it
struct MasterRun { uint64_t dry, master; };
MasterRun goldenWithMaster(const std::array<uint8_t, 32>& bytes)
{
    Engine eng(os().firmware, os().image);
    CHECK(eng.hasMaster());
    for (int i = 0; i < master::kParams; ++i) eng.host().setMasterFx(i, bytes[size_t(i)]);
    eng.primeMaster();
    MasterRun r{};
    r.dry = golden::render(eng, [](int, const Engine::Output&) {}, &r.master);
    CHECK_MSG(eng.fault().empty(), "DSP fault: " << eng.fault());
    return r;
}
}

TEST_CASE(gmaster_synthetic_sets)
{
    constexpr uint64_t kDry = 0xfe784d9f02fa30b2ull, kWet = 0x9b8a53ca6fae15feull;
    const auto dry = goldenWithMaster(golden::kMasterDry);
    const auto wet = goldenWithMaster(golden::kMasterWet);
    std::printf("gmaster_synthetic_sets: dry mix %s; master dry-ish %s, wet %s\n", hex(dry.dry).c_str(), hex(dry.master).c_str(), hex(wet.master).c_str());
    CHECK_MSG(dry.dry == golden::kHashOs163 && wet.dry == golden::kHashOs163, "the dry mix moved: " << hex(dry.dry) << " " << hex(wet.dry));
    CHECK_MSG(dry.master == kDry, "dry-ish master hash " << hex(dry.master) << ", expected " << hex(kDry));
    CHECK_MSG(wet.master == kWet, "wet master hash " << hex(wet.master) << ", expected " << hex(kWet));
    CHECK(dry.master != wet.master);
}

TEST_CASE(gmaster_kit_master_bytes)
{
    // the golden pattern's own machines and mix, with the master bytes of these preset kits (TRX, EFM, BOOGIE, ALIENS)
    struct Expect { size_t kit; uint64_t hash; };
    static const Expect expected[] = {{0, 0x5f4b45659f477ab7ull}, {1, 0x1222389a11b38fe6ull}, {9, 0x2abcba9c5e9bf547ull}, {13, 0xb892b2c5f5a1e43bull}};
    int wrong = 0;
    for (const auto& e : expected) {
        const auto& kit = kits().kits.at(e.kit);
        const auto r = goldenWithMaster(kit.masterFx.bytes());
        std::printf("gmaster_kit_master_bytes: kit %zu %s: dry mix %s, master %s\n", e.kit, kit.name.c_str(), hex(r.dry).c_str(), hex(r.master).c_str());
        CHECK_MSG(r.dry == golden::kHashOs163, "the dry mix moved: " << hex(r.dry));
        wrong += r.master != e.hash;
    }
    CHECK_EQ(wrong, 0);
}

// The engine's master stage is nothing but what it says: the native chain, the host's words handed to a MasterFx at the
// block boundary, the pass. Done by hand with a fresh MasterFx (no warm-up) it gives the engine's hash, so neither the
// warm-up nor the engine's glue changes what the DSP computes. And no block is compiled while the pattern plays.
TEST_CASE(gmaster_engine_is_the_chain_by_hand)
{
    const auto bytes = golden::kMasterWet;
    const auto viaEngine = goldenWithMaster(bytes);

    auto chain = chainEngine();
    auto& host = chain->host();
    MasterFx master(os().firmware);
    for (int i = 0; i < master::kParams; ++i) host.setMasterFx(i, bytes[size_t(i)]);
    host.primeMasterFx();
    golden::Pattern pattern;
    pattern.setup(host);
    Engine::Output out;
    Mixer::Stereo m;
    for (int b = 0; b < golden::kBlocks; ++b) {
        pattern.beforeBlock(host, b);
        CHECK(chain->render(out));
        const unsigned groups = host.takeMasterGroups();
        for (int g = 0; g < master::kGroups; ++g)
            if (groups & (1u << g)) master.writeGroup(g, host.masterWords().data() + master::kGroup[g].offset);
        CHECK_MSG(master.process(out.mix, m), master.faultReason());
        pattern.hashMaster(m);
        pattern.hashBlock(out.mix);
    }
    std::printf("gmaster_engine_is_the_chain_by_hand: engine %s, by hand %s\n", hex(viaEngine.master).c_str(), hex(pattern.masterHash()).c_str());
    CHECK_MSG(pattern.masterHash() == viaEngine.master, "by hand " << hex(pattern.masterHash()) << ", engine " << hex(viaEngine.master));
    CHECK_MSG(pattern.hash() == golden::kHashOs163, "the dry mix moved: " << hex(pattern.hash()));
}

// While the golden pattern plays through an engine with a master section, the JIT compiles nothing: the construction's
// warm-up did it all
TEST_CASE(gmaster_no_compile_while_playing)
{
    if (!dsp56k::g_useJIT) throw mt::Skip{"no JIT in this build"};
    Engine eng(os().firmware, os().image);
    CHECK(eng.hasMaster());
    for (int i = 0; i < master::kParams; ++i) eng.host().setMasterFx(i, golden::kMasterWet[size_t(i)]);
    eng.primeMaster();
    const size_t before = eng.master()->compiledBlocks();
    CHECK(before > 300);
    golden::render(eng);
    // the same engine with the settings swept across a few kits' worth of extremes while it plays
    Rng rng{31337};
    Engine::Output out;
    for (int b = 0; b < 6000; ++b) {
        if (b % 500 == 0) {
            const auto fx = randomMasterBytes(rng);
            for (int i = 0; i < master::kParams; ++i) eng.host().setMasterFx(i, fx[size_t(i)]);
            if (b % 1000 == 0) eng.host().setTempo(40.0 + double(rng.next() % 200));
        }
        CHECK(eng.render(out));
    }
    std::printf("gmaster_no_compile_while_playing: %zu JIT blocks after construction, %zu after 12 s of the pattern and 6000 blocks of changing kits\n", before, eng.master()->compiledBlocks());
    CHECK_EQ(eng.master()->compiledBlocks(), before);
}

// The engine without a master section: Output::master stays silent and the dry hash is the same
TEST_CASE(gmaster_disabled_is_silent)
{
    Engine eng(os().firmware, os().image, EngineOptions{false});
    CHECK(!eng.hasMaster());
    uint64_t masterHash = 0;
    const uint64_t dry = golden::render(eng, [](int, const Engine::Output&) {}, &masterHash);
    CHECK_MSG(dry == golden::kHashOs163, "hash " << hex(dry));
    golden::Pattern p;
    Mixer::Stereo zero{};
    for (int b = 0; b < golden::kBlocks; ++b) p.hashMaster(zero);
    CHECK_MSG(masterHash == p.masterHash(), "the master output was not silent");
}

// ParallelEngine carries the master too, on the same words: the same master hash as the single voice DSP
TEST_CASE(gmaster_parallel_engine)
{
    const auto single = goldenWithMaster(golden::kMasterWet);
    ParallelEngine eng(os().firmware, os().image, 2);
    CHECK(eng.hasMaster());
    for (int i = 0; i < master::kParams; ++i) eng.host().setMasterFx(i, golden::kMasterWet[size_t(i)]);
    eng.primeMaster();
    uint64_t masterHash = 0;
    const uint64_t dry = golden::render(eng, [](int, const ParallelEngine::Output&) {}, &masterHash);
    CHECK_MSG(dry == golden::kHashOs163, "hash " << hex(dry));
    CHECK_MSG(masterHash == single.master, "master hash " << hex(masterHash) << " vs " << hex(single.master) << " with one voice DSP");
}
