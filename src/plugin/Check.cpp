// Dev tool (not installed; ctest runs it): drives MdOneProcessor the way a host and the editor's OS picker do, without
// a window. Modelled on Monomodule's src/plugin/Capture.cpp --check-* modes.
//
//   md-plugin-check --check-os-load [--fw os.syx]
//       Pass/fail: a fresh install starts unconfigured; a wrong or missing file is reported, saves nothing and leaves
//       the plugin running; the OS file boots the engine, lands in the shared settings, and processBlock then runs the
//       engine in step with the host (44.1 and 48 kHz) and outputs silence; a second instance picks up the shared
//       setting. Exit code 77 (skipped) without an OS file. Uses a temporary MD_SETTINGS_DIR, never the user's settings.
//
//   md-plugin-check --check-render [--fw os.syx]
//       Pass/fail, the audio path (.clew increment 0003), with master_fx off (the dry main bus; --check-master has it on):
//       a fixed note sequence on all 16 tracks rendered through the
//       processor at 44.1 kHz must give kRenderHashOs163 (main output), whatever the host's block size; at 48 and 96 kHz
//       the output must be exactly the 44.1 kHz render taken through the converter, with the latency the processor
//       reports (and so the same peak, energy, timing and length); a track on its own bus leaves the main mix and
//       plays on that bus; the host's tempo (or the tempo setting without one) reaches the engine; the MIDI channel
//       filter, both note maps (each with its own hash for the same notes), velocity, accent and the editor's keyboard.
//       Exit code 77 (skipped) without an OS file.
//
//   md-plugin-check --check-state [--fw os.syx] [--kits kits.syx]
//       Pass/fail, parameters and state (.clew increment 0004): the parameter layout (ids, groups, names, ranges,
//       defaults, getName's cost); a machine change as a host makes it (the engine plays the machine's SYN defaults at
//       once, the SYN parameters follow and are renamed on the message thread, the host is told, SYN values set since are
//       kept; the machine parameters are meta parameters); the master effects' parameters (.clew increment 0009); an
//       edited session saved and restored (every parameter, the same state bytes, the same sound); a schema 2 state (the
//       init kit's master bytes) and a schema 1 state. With a kit snapshot (--kits, else $MD_KITS_SYX): every kit loaded
//       (its master bytes included) and exported is the same dump byte for byte; kit 0 played through the processor with
//       the master off gives kKit0ProcessorHashOs163, the same as md::library::applyKit on the engine with the
//       processor's warm-up (and tests/test_kits.cpp's kKit0Hash without it), and again after its state is restored into
//       a fresh processor. Exit code 77 (skipped) without an OS file.
//
//   md-plugin-check --check-master [--fw os.syx] [--kits kits.syx]
//       Pass/fail, the master effects in the plugin (.clew increment 0009): master_fx on by default; the latency (18
//       samples at 44.1 kHz, the conversion's and the 18 engine frames as one delay at 48 and 96 kHz) and the tail follow
//       it; the first live callback after an OS load compiles nothing in the master DSP; the regression render with the
//       master on gives kMasterRenderHashOs163 / kMasterRenderHashWhiteKeysOs163 (any host block size; at 48 and 96 kHz
//       the 44.1 kHz render through the converter); a GND-IM impulse lands on the same engine frame on the main bus, a
//       track bus and both send buses; the track and send buses are the master-off render's, sample for sample; master
//       edits change the sound only with the master on; switching it while playing. With a kit snapshot: kit 0 through
//       the processor with the master on is the engine's master output (applyKit, the kit's master bytes,
//       EngineT::primeMaster), the sends lined up with it. Exit code 77 (skipped) without an OS file.
//
//   md-plugin-check --bench [seconds] [--fw os.syx]
//       CPU use with all 16 tracks triggered 8 times per beat at 120 BPM: the processor at a 64-sample host buffer at
//       44.1, 48 and 96 kHz with the master effects on, as it ships (mean and worst callback against its real-time
//       budget, and the heap allocations in processBlock after the first second, the JIT's apart: see --check-rt), and
//       for reference the engine alone (master section included) and the processor at 44.1 kHz with the master off.
//
//   md-plugin-check --check-rt [--fw os.syx]
//       Pass/fail, the real-time path (.clew increment 0014): no heap allocation in processBlock (operator new, and on
//       macOS every malloc) at 44.1, 48 and 96 kHz and several block sizes, with notes on every track, editor keys,
//       automation, tempo and machine changes, master edits, a kit load (the master primed) and master_fx switched off
//       and on; only the DSP's JIT may allocate, when it compiles a block the first
//       time the voice program reaches it (counted apart). MD_RT_ALLOC_TRACE=1 prints the call stacks of the first
//       allocations. Exit code 77 (skipped) without an OS file, or off macOS (needs malloc_logger).
//
//   md-plugin-check --check-editor [--fw os.syx] [--kits kits.syx]
//       Pass/fail, the editor (.clew increment 0008), headless: without an OS file it opens and its knobs work; with one,
//       track select binds every page to the track's parameters; a knob follows the host, and a drag (up or right, 160 px
//       for the range, shift 8x finer), the wheel, a typed value (clamped; bipolar) and a double-click (the default) set
//       its parameter, one gesture each; the machine picker refuses greyed machines, sets the machine and the SYN page
//       relabels with the machine's defaults (also when the host changes it); the LFO page names its destination after
//       that track's machine; triggered tracks flash; the GLOBAL tab's faders, tempo, channel and note map; the MASTER
//       tab's four pages (the hardware's order and labels, every cell on its master byte) and GLOBAL's MASTER switch
//       (.clew increment 0009). With a kit
//       snapshot (--kits, else $MD_KITS_SYX): dropped on the editor it is copied into the kit folder and listed (its used
//       kits only), a click on kit 4 loads it, EXPORT writes the kit, the folder is rescanned when files change. Exit
//       code 77 (skipped) without an OS file, once the OS-free part has passed. Uses a temporary MD_SETTINGS_DIR.
//
//   md-plugin-check --snapshot <dir> [--fw os.syx] [--kits kits.syx]
//       The editor rendered offscreen to PNG, for review without a window: unconfigured.png and wrong-file.png (the
//       built-in stand-in face) and, with an OS file, at the base size and at 200 % (<name>-200.png): track (track 1's
//       four pages, an LFO set up, track 3 flashing), picker (the machine picker open), edit (a value being typed),
//       global-empty (the GLOBAL tab, no kit file yet), master (the MASTER tab: the default kit's master effects; and
//       master-off.png, switched off) and, with a kit snapshot, global (the snapshot in the kit folder, its kit 4 loaded
//       from the browser), kit-track (track 5 of that kit) and kit-master (its master effects); then track-50.png.
//       Also checks the resizing: 50-200 % of the base size, aspect ratio locked, the size left by the user kept as a
//       shared setting. Never commit these PNGs: they show artwork read from the OS file.
//
// The OS file: --fw, else $MD_OS_SYX, else the MD_OS_SYX CMake option.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <set>
#include <thread>
#include <vector>
#include "golden/GoldenPattern.h"
#include "md/library/KitApply.h"
#include "md/library/MdSysex.h"
#include "AllocCounter.h"
#include "one/OneEditor.h"
#include "one/OneProcessor.h"
#include "ScaledEditor.h"
#include "SharedSettings.h"
#include "dsp/Resampler.h"
#include "dsp56kEmu/dspconfig.h"

using md::plugin::one::MdOneProcessor;

namespace {

int g_failures = 0;

void check(bool ok, const juce::String& what)
{
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (!ok) ++g_failures;
}

// Runs n host blocks; true when every output sample was 0
bool runSilent(MdOneProcessor& p, double rate, int blockSize, int n)
{
    p.setRateAndBufferSizeDetails(rate, blockSize);
    p.prepareToPlay(rate, blockSize);
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    bool silent = true;
    for (int b = 0; b < n; ++b) {
        buf.applyGain(0.0f);
        buf.setSample(0, 0, 1.0f);   // processBlock must overwrite whatever the host leaves in the buffer
        p.processBlock(buf, midi);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i) silent = silent && buf.getSample(ch, i) == 0.0f;
    }
    return silent;
}

int checkOsLoad(const juce::String& os)
{
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-settings");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);

    MdOneProcessor p;
    check(!p.engineReady() && p.statusText().startsWith("No Machinedrum OS file selected"), "fresh start is unconfigured: " + p.statusText());
    check(runSilent(p, 44100.0, 512, 4) && p.engineBlocks() == 0, "processBlock without an engine outputs silence");

    const auto wrong = settingsDir.getSiblingFile("md-plugin-check-not-an-os.syx");
    wrong.replaceWithText("not a Machinedrum OS file\n");
    p.setFirmwarePath(wrong.getFullPathName());
    check(!p.engineReady() && p.statusText().startsWith("OS file load failed"), "a wrong file reports an error: " + p.statusText());
    check(md::plugin::loadSharedOsPath().isEmpty(), "a wrong file is not saved as the shared OS path");
    check(runSilent(p, 44100.0, 512, 4), "the plugin keeps running after a wrong file");
    p.setFirmwarePath(settingsDir.getChildFile("missing.syx").getFullPathName());
    check(!p.engineReady() && p.statusText().startsWith("OS file not found"), "a missing file is reported: " + p.statusText());

    if (os.isEmpty() || !juce::File(os).existsAsFile()) {
        std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n");
        wrong.deleteFile();
        return g_failures ? 1 : 77;
    }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    p.setFirmwarePath(os);
    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    check(p.engineReady() && p.statusText().contains("engine ready"), "the OS file boots the engine (" + juce::String(juce::roundToInt(loadMs)) + " ms): " + p.statusText());
    check(md::plugin::loadSharedOsPath() == os, "the OS path is saved in " + md::plugin::sharedSettingsFile().getFullPathName());
    check(runSilent(p, 44100.0, 512, 100) && p.engineBlocks() == 100 * 512 / 32, "44.1 kHz: silence, " + juce::String(p.engineBlocks()) + " engine blocks for 100 x 512 host samples");
    const auto before = p.engineBlocks();
    check(runSilent(p, 48000.0, 480, 100), "48 kHz: silence");
    // the engine renders whole 32-frame blocks as frames are needed: 100 x 480 host samples are 44100 frames, 1379 blocks
    const auto frames = uint64_t((100 * 480 - 1) * 441 / 480 + 1);
    const auto expected = (frames + 31) / 32;
    check(p.engineBlocks() - before == expected, "48 kHz: " + juce::String(p.engineBlocks() - before) + " engine blocks, expected " + juce::String(expected));
    check(p.getLatencySamples() == p.conversionLatency() && p.conversionLatency() > 0, "48 kHz: latency reported, " + juce::String(p.getLatencySamples()) + " samples");
    check(p.statusText().contains("engine time") && !p.statusText().contains("FAULT"), "status after rendering: " + p.statusText());

    MdOneProcessor second;
    check(second.engineReady() && second.firmwarePath() == os, "a second instance loads the shared OS path");

    p.setFirmwarePath(wrong.getFullPathName());
    check(!p.engineReady() && md::plugin::loadSharedOsPath() == os, "a wrong file after a good one keeps the shared setting");

    wrong.deleteFile();
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// ---- Offline renders through the processor (--check-render, --bench) ----

// The main output of the regression render at 44.1 kHz with OS 1.63 (FNV-1a over the 24-bit samples, L R interleaved),
// one per note map: the same notes, so the white-key map plays other tracks (and none for half of them). Any change to the
// engine, the kit handling or the note timing changes them. The chromatic one is increment 0003's, recorded before the
// note map was a setting (that map was the only one then).
constexpr uint64_t kRenderHashOs163 = 0xa28b9dc178776939ull;            // NoteMap::Chromatic (36-51 and 0-15)
constexpr uint64_t kRenderHashWhiteKeysOs163 = 0x14291ff9302212d4ull;   // NoteMap::WhiteKeys (the MD's own): tracks 1-9 play

// A fixed tempo, as a host's transport reports it
struct FixedPlayHead final : juce::AudioPlayHead {
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm(bpm); return p; }
};

// A note-on at an engine frame (44.1 kHz time); a render at another rate sends it at host sample round(frame * rate / 44100)
struct Note { int64_t frame; int channel; int note; int velocity; };

// The test kit: machines of each family that plays without ROM samples (TRX, EFM, E12, P-I), every parameter given, so
// the hash does not depend on the processor's default kit. SYN1-8 stay at each machine's defaults (from the OS's table)
// except an LFO on track 2 and E12 retrigs on track 5, both timed by the tempo.
template<class SetMachine, class SetParam>
void forTestKit(SetMachine&& setMachine, SetParam&& setParam)
{
    static constexpr int kMachines[16] = {16, 17, 32, 33, 48, 49, 64, 65, 22, 23, 38, 55, 24, 19, 57, 72};
    for (int t = 0; t < 16; ++t) {
        setMachine(t, kMachines[t]);
        const int params[16] = {0, 0, 64, 64, 0, 127, 0, 0, 0, 100, (t * 37) % 128, 24, 32, 0, 0, 0};   // AMD .. LFOM
        for (int k = 0; k < 16; ++k) setParam(t, 8 + k, params[k]);
    }
    setParam(1, 21, 64); setParam(1, 22, 60);                      // track 2: LFO speed and depth (onto its SYN1)
    setParam(4, 5, 30); setParam(4, 6, 64);                        // track 5 (E12 BD): RTRG, RTIM
    setParam(2, 12, 30); setParam(2, 13, 90); setParam(2, 14, 60); // track 3: filter
    setParam(3, 15, 20);                                           // track 4: SRR
    setParam(6, 16, 40);                                           // track 7: DIST
}

void setupTestKit(MdOneProcessor& p)
{
    forTestKit([&](int t, int m) { p.setMachine(t, m); }, [&](int t, int k, int v) { p.setTrackParam(t, k, v); });
    for (int t = 0; t < 16; ++t) { p.setTrackLevel(t, 100); p.setTrackOutput(t, false); }
}

// The regression sequence: 32 steps of 172 engine blocks (16ths at ~120 BPM), two tracks per step on MIDI channel 10,
// alternating the two note maps, varied velocities, and on each step a note on channel 1 that the processor (listening
// on 10) must ignore. Each note sits 8 frames before an engine block boundary, so it triggers in the same engine block
// at every host rate: the engine then renders exactly the same audio at 44.1, 48 and 96 kHz.
constexpr int64_t kStepFrames = 172 * 32;
constexpr int kSteps = 32;
constexpr int64_t kSequenceFrames = kSteps * kStepFrames + 22050;   // + 0.5 s of tail

std::vector<Note> testSequence()
{
    std::vector<Note> s;
    for (int k = 0; k < kSteps; ++k) {
        const int64_t frame = k * kStepFrames + 4 * 32 - 8;
        const int base = k % 2 == 0 ? 36 : 0;
        s.push_back({frame, 10, base + k % 16, 30 + (k * 53) % 98});
        s.push_back({frame, 10, base + (k * 7 + 3) % 16, 127 - (k * 29) % 90});
        s.push_back({frame, 1, 36 + (k * 5) % 16, 127});
    }
    return s;
}

struct RenderOptions {
    double rate = 44100.0;
    int block = 512;
    double hostBpm = 120.0;   // the play head's tempo; 0 = no play head
    // The three globals are applied after `setup`; nullopt leaves what `setup` (a restored state) left. An optional rather than a
    // flag: each global keeps its own default, and a pass can leave out any one of them.
    std::optional<double> tempo = 120.0;   // the processor's tempo setting
    std::optional<int> midiChannel = 10;
    std::optional<MdOneProcessor::NoteMap> noteMap = MdOneProcessor::NoteMap::Chromatic;   // set explicitly: each map has its own hash
    // master_fx, applied with the globals. Off by default: the main bus is then the dry mix, as when these hashes were
    // recorded (before the master effects); --check-master records its own with it on
    std::optional<bool> masterFx = false;
    bool allBuses = false;    // every track bus (mono) and both sends on
    bool testKit = true;      // the test kit (else the processor's default kit, or what `setup` loads)
    std::function<void(MdOneProcessor&)> setup;   // after the test kit
    std::function<void(MdOneProcessor&)> after;   // after the render (the processor is about to go)
};

struct Render {
    std::vector<std::vector<float>> ch;   // every output channel, the reported latency removed
    std::vector<int> busChannel;          // first channel of each output bus in ch, -1 when off
    int latency = 0;
    uint64_t engineBlocks = 0;
    uint32_t triggered = 0;               // MdOneProcessor::takeTriggeredTracks() after the render
    bool ok = false;
    const std::vector<float>& bus(int b, int c = 0) const { return ch[size_t(busChannel[size_t(b)] + c)]; }
};

// `frames` engine frames of `notes` through a fresh processor (a fresh engine: renders are comparable sample for sample)
Render render(const std::vector<Note>& notes, int64_t frames, const RenderOptions& o)
{
    Render r;
    MdOneProcessor p;
    if (!p.engineReady()) { check(false, "render: no engine: " + p.statusText()); return r; }
    if (o.testKit) setupTestKit(p);
    if (o.setup) o.setup(p);
    if (o.midiChannel) p.setMidiChannel(*o.midiChannel);
    if (o.noteMap) p.setNoteMap(*o.noteMap);
    if (o.tempo) p.setTempo(*o.tempo);
    if (o.masterFx) p.setMasterOn(*o.masterFx);
    if (o.allBuses) {
        auto layout = p.getBusesLayout();
        for (int b = 1; b < MdOneProcessor::kNumBuses; ++b)
            layout.outputBuses.getReference(b) = b < MdOneProcessor::kReverbBus ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        if (!p.setBusesLayout(layout)) { check(false, "render: every bus on is a supported layout"); return r; }
    }
    p.setRateAndBufferSizeDetails(o.rate, o.block);
    p.prepareToPlay(o.rate, o.block);
    FixedPlayHead head;
    head.bpm = o.hostBpm;
    if (o.hostBpm > 0.0) p.setPlayHead(&head);
    r.latency = p.getLatencySamples();
    for (int b = 0; b < MdOneProcessor::kNumBuses; ++b)
        r.busChannel.push_back(p.getBus(false, b)->isEnabled() ? p.getChannelIndexInProcessBlockBuffer(false, b, 0) : -1);
    const int64_t total = int64_t(std::ceil(double(frames) * o.rate / 44100.0)) + r.latency;
    const int nch = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buf(nch, o.block);
    juce::MidiBuffer midi;
    r.ch.assign(size_t(nch), {});
    for (auto& c : r.ch) c.reserve(size_t(total + o.block));
    size_t next = 0;
    for (int64_t pos = 0; pos < total; pos += o.block) {
        midi.clear();
        for (; next < notes.size(); ++next) {
            const auto s = int64_t(std::llround(double(notes[next].frame) * o.rate / 44100.0));
            if (s >= pos + o.block) break;
            midi.addEvent(juce::MidiMessage::noteOn(notes[next].channel, notes[next].note, uint8_t(notes[next].velocity)), int(s - pos));
        }
        p.processBlock(buf, midi);
        for (int c = 0; c < nch; ++c) r.ch[size_t(c)].insert(r.ch[size_t(c)].end(), buf.getReadPointer(c), buf.getReadPointer(c) + o.block);
    }
    p.setPlayHead(nullptr);
    r.engineBlocks = p.engineBlocks();
    r.triggered = p.takeTriggeredTracks();
    if (o.after) o.after(p);
    for (auto& c : r.ch) { c.erase(c.begin(), c.begin() + r.latency); c.resize(size_t(total - r.latency)); }
    r.ok = p.engineReady() && !p.statusText().contains("FAULT");
    if (!r.ok) check(false, "render: " + p.statusText());
    return r;
}

uint64_t hashMain(const Render& r)
{
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](int64_t v) { h ^= uint64_t(v); h *= 1099511628211ull; };
    for (size_t i = 0; i < r.ch[0].size(); ++i)
        for (int c = 0; c < 2; ++c) mix(std::lrint(double(r.ch[size_t(c)][i]) * 8388608.0));   // back to the engine's 24-bit samples
    return h;
}

juce::String hex(uint64_t v) { return juce::String::toHexString(juce::int64(v)).paddedLeft('0', 16); }

float peakOf(const std::vector<float>& x) { float p = 0.f; for (float v : x) p = std::max(p, std::abs(v)); return p; }
bool silentOf(const std::vector<float>& x) { return std::all_of(x.begin(), x.end(), [](float v) { return v == 0.f; }); }

// What a listener would compare between two renders of the main output at different rates (latency removed)
struct Shape { double peakDb, energyDb, peakTime, endTime; };
Shape shapeOf(const Render& r, double rate)
{
    double e = 0.0;
    float peak = 0.f;
    size_t peakAt = 0, last = 0;
    for (size_t i = 0; i < r.ch[0].size(); ++i)
        for (int c = 0; c < 2; ++c) {
            const float v = std::abs(r.ch[size_t(c)][i]);
            e += double(v) * double(v);
            if (v > peak) { peak = v; peakAt = i; }
            if (v > 1e-3f) last = i;
        }
    return {20.0 * std::log10(double(peak)), 10.0 * std::log10(e / rate), double(peakAt) / rate, double(last) / rate};
}

// The 44.1 kHz render taken to `rate` offline, as the processor's converter does it (output m at engine time m * 44100 / rate)
std::vector<float> convertOffline(const std::vector<float>& x, double rate, size_t count)
{
    md::dsp::SincKernel k;
    k.design(44100.0, rate);
    md::dsp::RateMap map;
    map.set(44100.0, rate);
    md::dsp::History h;
    h.prepare(int(x.size()) + 64);
    h.push(x.data(), int(x.size()));
    std::vector<float> taps(size_t(k.numTaps())), y;
    for (size_t m = 0; m < count; ++m) {
        const auto at = map.at(int64_t(m));
        if (at.index + k.halfTaps() >= int64_t(x.size())) break;   // past the end of the 44.1 kHz render
        y.push_back(h.dot(at.index - k.halfTaps() + 1, k.taps(at, taps.data()), k.numTaps()));
    }
    return y;
}

int checkRender(const juce::String& os)
{
    if (os.isEmpty() || !juce::File(os).existsAsFile()) {
        std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n");
        return 77;
    }
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-render");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    md::plugin::saveSharedOsPath(os);   // every processor below loads it
    const auto seq = testSequence();
    const double seconds = double(kSequenceFrames) / 44100.0;

    // 44.1 kHz: the regression hash, the same for any host block size
    const auto base = render(seq, kSequenceFrames, {});
    if (!base.ok) return 1;
    const auto hash = hashMain(base);
    check(hash == kRenderHashOs163, "44.1 kHz, 512-sample blocks: main output hash " + hex(hash) + ", expected " + hex(kRenderHashOs163)
          + " (" + juce::String(seconds, 2) + " s, peak " + juce::String(shapeOf(base, 44100.0).peakDb, 2) + " dBFS, "
          + juce::String(base.engineBlocks) + " engine blocks)");
    check(base.latency == 0, "44.1 kHz: no latency reported");
    check(base.triggered == 0xffff, "every track triggered (" + juce::String::toHexString(int(base.triggered)) + ")");
    RenderOptions odd;
    odd.block = 61;
    const auto oddBlocks = render(seq, kSequenceFrames, odd);
    check(oddBlocks.ok && oddBlocks.ch[0] == base.ch[0] && oddBlocks.ch[1] == base.ch[1], "44.1 kHz, 61-sample blocks: the same samples");

    // The white-key map (the hardware's): the same notes give its own hash; the sequence moved to the white keys of the
    // same tracks plays exactly the chromatic render
    {
        using NoteMap = MdOneProcessor::NoteMap;
        RenderOptions white;
        white.noteMap = NoteMap::WhiteKeys;
        const auto w = render(seq, kSequenceFrames, white);
        const auto wh = hashMain(w);
        check(w.ok && wh == kRenderHashWhiteKeysOs163, "white-key map, the same notes: main output hash " + hex(wh) + ", expected "
              + hex(kRenderHashWhiteKeysOs163) + " (tracks triggered " + juce::String::toHexString(int(w.triggered)) + ")");
        auto moved = seq;
        for (auto& note : moved)
            note.note = md::plugin::one::params::noteForTrack(md::plugin::one::params::trackForNote(note.note, NoteMap::Chromatic), NoteMap::WhiteKeys);
        const auto m = render(moved, kSequenceFrames, white);
        check(m.ok && m.ch[0] == base.ch[0] && m.ch[1] == base.ch[1], "white-key map, the sequence on the white keys: the chromatic render's samples");
    }

    // 48 and 96 kHz: the engine renders the same; the output is that through the converter, delayed by the reported latency.
    // Its peak may exceed the 44.1 kHz samples' (the waveform peaks between them) up to the true peak: measured at 4x, which can
    // under-read a sharp peak a little (here the 48 and 96 kHz grids share the point nearest it: both read +1.07 dB, 4x +1.04).
    const auto b44 = shapeOf(base, 44100.0);
    float truePeak = 0.f;
    for (int c = 0; c < 2; ++c) truePeak = std::max(truePeak, peakOf(convertOffline(base.ch[size_t(c)], 4 * 44100.0, 4 * base.ch[0].size())));
    const double truePeakDb = 20.0 * std::log10(double(truePeak));
    for (const double rate : {48000.0, 96000.0}) {
        RenderOptions o;
        o.rate = rate;
        o.block = rate > 50000.0 ? 1024 : 256;
        const auto r = render(seq, kSequenceFrames, o);
        if (!r.ok) continue;
        const auto tag = juce::String(rate / 1000.0, 0) + " kHz: ";
        const int expectedLatency = int(std::ceil(36.0 * rate / 44100.0));   // (35 + 1) engine frames of the converter's half length
        check(r.latency == expectedLatency, tag + "latency " + juce::String(r.latency) + " samples, expected " + juce::String(expectedLatency));
        float worst = 0.f;
        size_t compared = 0;
        for (int c = 0; c < 2; ++c) {
            const auto ref = convertOffline(base.ch[size_t(c)], rate, r.ch[size_t(c)].size());
            compared = ref.size();
            for (size_t i = 0; i < ref.size(); ++i) worst = std::max(worst, std::abs(ref[i] - r.ch[size_t(c)][i]));
        }
        check(compared > r.ch[0].size() - 200 && worst < 1e-6f, tag + "the 44.1 kHz render through the converter, max difference "
              + juce::String(worst, 9) + " over " + juce::String(compared) + " samples");
        const auto s = shapeOf(r, rate);
        check(s.peakDb >= b44.peakDb - 0.1 && s.peakDb <= truePeakDb + 0.2 && std::abs(s.energyDb - b44.energyDb) < 0.1
              && std::abs(s.peakTime - b44.peakTime) < 1e-3 && std::abs(s.endTime - b44.endTime) < 5e-3,
              tag + "against 44.1 kHz: peak " + juce::String(s.peakDb - b44.peakDb, 3) + " dB (true peak "
              + juce::String(truePeakDb - b44.peakDb, 3) + " dB), energy " + juce::String(s.energyDb - b44.energyDb, 4)
              + " dB, peak at " + juce::String((s.peakTime - b44.peakTime) * 1e3, 3) + " ms, end " + juce::String((s.endTime - b44.endTime) * 1e3, 2)
              + " ms (" + juce::String(r.engineBlocks) + " engine blocks)");
    }

    // Track 1 on its own bus (no sends) leaves the main mix: the main output is as with its VOL at 0, and its bus has it
    {
        const auto noSends = [](MdOneProcessor& p) { p.setTrackParam(0, 19, 0); p.setTrackParam(0, 20, 0); };
        RenderOptions own;
        own.allBuses = true;
        own.setup = [&](MdOneProcessor& p) { noSends(p); p.setTrackOutput(0, true); };
        const auto a = render(seq, kSequenceFrames, own);
        RenderOptions muted;
        muted.setup = [&](MdOneProcessor& p) { noSends(p); p.setTrackParam(0, 17, 0); };
        const auto b = render(seq, kSequenceFrames, muted);
        if (a.ok && b.ok) {
            check(a.ch[0] == b.ch[0] && a.ch[1] == b.ch[1], "track 1 on its own bus: the main mix without it (= its VOL at 0)");
            bool othersSilent = true;
            for (int t = 1; t < 16; ++t) othersSilent = othersSilent && silentOf(a.bus(MdOneProcessor::kTrackBus + t));
            check(peakOf(a.bus(MdOneProcessor::kTrackBus)) > 0.01f && othersSilent, "track 1's bus plays it (peak "
                  + juce::String(20.0 * std::log10(peakOf(a.bus(MdOneProcessor::kTrackBus))), 1) + " dBFS); the other track buses are silent");
            check(peakOf(a.bus(MdOneProcessor::kReverbBus)) > 0.001f && peakOf(a.bus(MdOneProcessor::kDelayBus, 1)) > 0.001f,
                  "the reverb and delay send buses carry the sends (peaks " + juce::String(20.0 * std::log10(peakOf(a.bus(MdOneProcessor::kReverbBus))), 1)
                  + " / " + juce::String(20.0 * std::log10(peakOf(a.bus(MdOneProcessor::kDelayBus))), 1) + " dBFS)");
        }
        RenderOptions offBus;   // routed to its own bus while the host has that bus off: it stays in the main mix
        offBus.setup = [](MdOneProcessor& p) { p.setTrackOutput(0, true); };
        const auto c = render(seq, kSequenceFrames, offBus);
        check(c.ok && hashMain(c) == hash, "a track routed to a bus the host has off stays in the main mix");
    }

    // Tempo: the host's reaches the engine (LFO, E12 retrigs); without a play head the tempo setting does
    {
        RenderOptions host140;
        host140.hostBpm = 140.0;
        const auto a = render(seq, kSequenceFrames, host140);
        RenderOptions setting140;
        setting140.hostBpm = 0.0;
        setting140.tempo = 140.0;
        const auto b = render(seq, kSequenceFrames, setting140);
        check(a.ok && hashMain(a) != hash, "host tempo 140 BPM changes the sound (" + hex(hashMain(a)) + ")");
        check(a.ok && b.ok && a.ch[0] == b.ch[0] && a.ch[1] == b.ch[1], "no play head: the tempo setting (140) acts as the host's");
    }

    // MIDI: channel filter, both note maps, velocity, the editor's keyboard (short renders)
    {
        const int64_t shortFrames = 8192;
        const std::vector<Note> ch1 = {{24, 1, 36, 127}, {24, 1, 38, 127}};
        RenderOptions on10;
        const auto ignored = render(ch1, shortFrames, on10);
        check(silentOf(ignored.ch[0]) && ignored.triggered == 0, "listening on channel 10: notes on channel 1 are ignored");
        RenderOptions omni;
        omni.midiChannel = 0;
        check(!silentOf(render(ch1, shortFrames, omni).ch[0]), "omni: notes on channel 1 play");
        const auto n36 = render({{24, 10, 36, 100}}, shortFrames, {});
        const auto n0 = render({{24, 10, 0, 100}}, shortFrames, {});
        check(!silentOf(n36.ch[0]) && n36.ch[0] == n0.ch[0], "notes 36 and 0 both trigger track 1, identically");
        const auto n51 = render({{24, 10, 51, 100}}, shortFrames, {});
        const auto n15 = render({{24, 10, 15, 100}}, shortFrames, {});
        check(!silentOf(n51.ch[0]) && n51.ch[0] == n15.ch[0], "notes 51 and 15 both trigger track 16, identically");
        check(silentOf(render({{24, 10, 52, 100}, {24, 10, 16, 100}, {24, 10, 35, 100}}, shortFrames, {}).ch[0]), "notes 16-35 and 52+ trigger nothing");
        const auto loud = render({{24, 10, 36, 127}}, shortFrames, {});
        const auto soft = render({{24, 10, 36, 64}}, shortFrames, {});
        const double ratio = double(peakOf(loud.ch[0])) / double(peakOf(soft.ch[0]));
        check(ratio > 1.9 && ratio < 2.05, "velocity 127 vs 64: peak ratio " + juce::String(ratio, 3) + " (the MD's VOL gain is linear in velocity)");
        RenderOptions keys;
        keys.setup = [](MdOneProcessor& p) { p.keyboardState().noteOn(1, 36, 1.0f); };
        check(!silentOf(render({}, shortFrames, keys).ch[0]), "the editor's keyboard plays (channel 1 while the processor listens on 10)");

        // The white-key map: 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62 are tracks 1-16, nothing else plays
        RenderOptions white;
        white.noteMap = MdOneProcessor::NoteMap::WhiteKeys;
        const auto w38 = render({{24, 10, 38, 100}}, shortFrames, white);
        const auto c37 = render({{24, 10, 37, 100}}, shortFrames, {});
        check(!silentOf(w38.ch[0]) && w38.ch[0] == c37.ch[0] && w38.triggered == 2u, "white keys: note 38 triggers track 2 (as 37 does on the chromatic map)");
        const auto w62 = render({{24, 10, 62, 100}}, shortFrames, white);
        check(w62.ch[0] == n51.ch[0] && w62.triggered == 0x8000u, "white keys: note 62 triggers track 16");
        std::vector<Note> whiteKeys;
        for (int t = 0; t < 16; ++t) whiteKeys.push_back({24, 10, md::plugin::one::params::kWhiteKeys[size_t(t)], 100});
        check(render(whiteKeys, shortFrames, white).triggered == 0xffffu, "white keys: all 16 trigger tracks 1-16");
        check(silentOf(render({{24, 10, 37, 100}, {24, 10, 39, 100}, {24, 10, 61, 100}, {24, 10, 0, 100}, {24, 10, 15, 100},
                               {24, 10, 35, 100}, {24, 10, 63, 100}}, shortFrames, white).ch[0]), "white keys: black keys, 0-15, 35 and 63 trigger nothing");

        // Accent: with an amount set, a note at velocity 127 is an accented trig (VEL 128 + 2 x 64 = 256 instead of 127)
        RenderOptions accent;
        accent.setup = [](MdOneProcessor& p) { p.setAccent(64); };
        const auto accented = render({{24, 10, 36, 127}}, shortFrames, accent);
        const double accentRatio = double(peakOf(accented.ch[0])) / double(peakOf(loud.ch[0]));
        check(accentRatio > 1.95 && accentRatio < 2.06, "accent 64: velocity 127 plays accented, peak ratio " + juce::String(accentRatio, 3) + " to the unaccented 127 (256 / 127)");
        check(render({{24, 10, 36, 126}}, shortFrames, accent).ch[0] == render({{24, 10, 36, 126}}, shortFrames, {}).ch[0], "accent 64: velocity 126 is not accented");
    }

    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// ---- Parameters, kits and state (--check-state) ----

namespace params = md::plugin::one::params;

// Kit 0 of the user's snapshot (MD_presets_1_50.syx, "TRX") through the processor: the kit test's triggers (below), main
// output and both sends hashed as golden::Pattern hashes the engine's output, with OS 1.63. The processor warms its
// engine up for 16 silent blocks before it plays (MdOneProcessor::kWarmUpBlocks); the same render straight on the engine
// with the same warm-up gives the same hash, and without it tests/test_kits.cpp's kKit0Hash (f5c618b7df5550fa).
constexpr uint64_t kKit0ProcessorHashOs163 = 0xda62047690565e38ull;
constexpr uint64_t kKit0Hash = 0xf5c618b7df5550faull;   // tests/test_kits.cpp

// The kit test's sequence (tests/test_kits.cpp): golden::Pattern's triggers, four random tracks every 300 engine blocks at
// velocity 100 for 12 s, as MIDI notes on channel 10 at the engine block they trigger in
struct TriggerRecorder {
    int block = 0;
    MdOneProcessor::NoteMap map = MdOneProcessor::NoteMap::WhiteKeys;
    std::vector<Note> notes;
    void trigger(int track, int velocity) { notes.push_back({int64_t(block) * 32, 10, params::noteForTrack(track, map), velocity}); }
};

std::vector<Note> kitTestSequence(MdOneProcessor::NoteMap map)
{
    md::golden::Pattern pattern;
    TriggerRecorder r;
    r.map = map;
    for (r.block = 0; r.block < md::golden::kBlocks; ++r.block) pattern.beforeBlock(r, r.block);
    return r.notes;
}

// golden::Pattern's hash of a processor render with every bus on: per frame and side the main output, the six A-F
// outputs (no bus plays them; 0, as every track of a kit goes to the main mix), the reverb and the delay send
uint64_t hashKitTest(const Render& r)
{
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](int64_t v) { h ^= uint64_t(v); h *= 1099511628211ull; };
    const auto word = [](float v) { return int64_t(std::lrint(double(v) * 8388608.0)); };
    for (size_t i = 0; i < r.ch[0].size(); ++i)
        for (int c = 0; c < 2; ++c) {
            mix(word(r.bus(0, c)[i]));
            for (int q = 0; q < 6; ++q) mix(0);
            mix(word(r.bus(MdOneProcessor::kReverbBus, c)[i]));
            mix(word(r.bus(MdOneProcessor::kDelayBus, c)[i]));
        }
    return h;
}

// The kit test straight on the engine (md::library::applyKit, as tests/test_kits.cpp), after `warmUp` silent blocks
uint64_t directKitTest(const juce::String& os, const md::library::Kit& kit, int warmUp)
{
    const std::filesystem::path path(os.toStdString());
    const auto firmware = md::fw::loadFirmware(path);
    auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(path)));
    md::engine::Engine eng(firmware, std::move(c.sections.at(0).data));
    md::engine::Engine::Output out;
    for (int b = 0; b < warmUp; ++b) eng.render(out);
    md::library::applyKit(eng.host(), kit);
    md::golden::Pattern pattern;
    for (int b = 0; b < md::golden::kBlocks; ++b) {
        pattern.beforeBlock(eng.host(), b);
        eng.render(out);
        pattern.hashBlock(out.mix);
    }
    return pattern.hash();
}

// What a host sees: parameter-info changes (names) and parameter changes the processor makes itself
struct HostListener final : juce::AudioProcessorListener {
    int infoChanges = 0;
    std::vector<int> changed;   // parameter indices
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override { changed.push_back(index); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override { if (d.parameterInfoChanged) ++infoChanges; }
};

std::vector<float> parameterValues(const MdOneProcessor& p)
{
    std::vector<float> v;
    for (auto* q : p.getParameters()) v.push_back(q->getValue());
    return v;
}

juce::MemoryBlock stateOf(MdOneProcessor& p)
{
    juce::MemoryBlock m;
    p.getStateInformation(m);
    return m;
}

// AudioParameterInt keeps the host-facing calls (getText, getValueForText, getDefaultValue) private; hosts use the base
const juce::AudioProcessorParameter& asBase(const juce::AudioProcessorParameter& p) { return p; }

// The SYN names the OS gives a machine: "T<n> " + its label, "-" for an unused knob
juce::String expectedSynName(const MdOneProcessor& p, int track, int machine, int k)
{
    for (const auto& m : p.machines())
        if (m.id == machine) return "T" + juce::String(track + 1) + " " + (m.params[size_t(k)].empty() ? juce::String("-") : juce::String(m.params[size_t(k)]));
    return {};
}

int checkState(const juce::String& os, const juce::String& kitsPath)
{
    if (os.isEmpty() || !juce::File(os).existsAsFile()) {
        std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n");
        return 77;
    }
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-state");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    md::plugin::saveSharedOsPath(os);
    using NoteMap = MdOneProcessor::NoteMap;

    // The layout: 16 groups T1..T16 of 31 parameters, then Global; ids, names, ranges and defaults
    {
        MdOneProcessor p;
        const auto& ps = p.getParameters();
        check(ps.size() == params::kTotal, "parameter count " + juce::String(ps.size()) + " (16 x " + juce::String(params::kTrackParams) + " + "
              + juce::String(params::kGlobals) + " + " + juce::String(params::kMasterGroupParams) + ")");
        bool idsOk = true, rangesOk = true, namesOk = true, automatable = true;
        juce::StringArray ids;
        for (int t = 0; t < 16; ++t)
            for (int slot = 0; slot < params::kTrackParams; ++slot) {
                auto* q = dynamic_cast<juce::AudioParameterInt*>(ps[t * params::kTrackParams + slot]);
                if (q == nullptr || q != &p.parameter(t, slot)) { idsOk = false; continue; }
                idsOk = idsOk && q->getParameterID() == params::trackId(t, slot);
                rangesOk = rangesOk && q->getRange() == juce::Range<int>(0, params::maxOf(slot));
                const bool syn = slot >= params::kKit && slot < params::kKit + params::kSyn;
                namesOk = namesOk && (syn ? q->getName(64) == expectedSynName(p, t, p.machine(t), slot - params::kKit) : q->getName(64) == params::trackName(t, slot));
                automatable = automatable && q->isAutomatable();
                ids.add(q->getParameterID());
            }
        for (int i = params::kTracks * params::kTrackParams; i < ps.size(); ++i) {
            const auto* q = dynamic_cast<juce::RangedAudioParameter*>(ps[i]);
            if (q != nullptr) ids.add(q->getParameterID());
            automatable = automatable && ps[i]->isAutomatable();
        }
        constexpr int g0 = params::kTracks * params::kTrackParams, m0 = g0 + params::kGlobals;   // the globals, then the master effects
        const auto globalsOk = ids.size() == params::kTotal && ids[g0] == "tempo" && ids[g0 + 1] == "midi_channel" && ids[g0 + 2] == "accent"
                             && ids[g0 + 3] == "note_map" && ids[m0] == "master_fx" && ids[m0 + 1] == "fx_rev_dvol" && ids[m0 + 8] == "fx_rev_lev"
                             && ids[m0 + 9] == "fx_echo_time" && ids[m0 + 13] == "fx_echo_filtf" && ids[m0 + 17] == "fx_eq_lf" && ids[m0 + 24] == "fx_eq_gain"
                             && ids[m0 + 25] == "fx_dyn_atck" && ids[m0 + 32] == "fx_dyn_mix";
        bool masterOk = &p.masterOnParameter() == ps[m0] && p.masterOnParameter().get() && p.masterOnParameter().getName(64) == "MASTER FX";
        for (int i = 0; i < MdOneProcessor::kMasterParams && masterOk; ++i) {
            const auto& q = p.masterParameter(i);
            masterOk = &q == ps[m0 + 1 + i] && q.getParameterID() == params::masterId(i) && q.getName(64) == params::masterName(i)
                    && q.getRange() == juce::Range<int>(0, 127) && q.get() == md::engine::master::kDefaultParams[size_t(i)]
                    && juce::roundToInt(q.convertFrom0to1(asBase(q).getDefaultValue())) == md::engine::master::kDefaultParams[size_t(i)];
        }
        ids.removeDuplicates(false);
        check(idsOk && globalsOk && ids.size() == params::kTotal, "ids in order and unique: track0_machine, track0_syn1 .. track0_lfom, track0_level, "
              "track0_lfo_track .. track0_lfo_type, ..., tempo, midi_channel, accent, note_map, master_fx, fx_rev_dvol .. fx_rev_lev, fx_echo_time .. "
              "fx_echo_lev, fx_eq_lf .. fx_eq_gain, fx_dyn_atck .. fx_dyn_mix");
        check(masterOk, "the master effects: MASTER FX on; 32 bytes 0-127 named \"" + p.masterParameter(0).getName(64) + "\" .. \""
              + p.masterParameter(8).getName(64) + "\" .. \"" + p.masterParameter(31).getName(64) + "\", defaulting to the init kit's (a cleared MD kit's)");
        check(rangesOk, "ranges: machine 0-191, kit parameters and LEV 0-127, LFO track 0-15, param 0-23, shapes 0-5, update 0-2");
        check(namesOk, "names: \"" + p.parameter(0, params::kMachine).getName(64) + "\", \"" + p.parameter(0, params::kKit).getName(64) + "\" .. \""
              + p.parameter(0, params::kKit + 8).getName(64) + "\" .. \"" + p.parameter(15, params::kLevel).getName(64) + "\", \""
              + p.parameter(15, params::kLfo + 4).getName(64) + "\" (SYN1-8 after the machine's knobs)");
        check(automatable, "every parameter automatable");
        const auto groups = p.getParameterTree().getSubgroups(false);
        bool groupsOk = groups.size() == 18;
        for (int g = 0; g < 17 && groupsOk; ++g)
            groupsOk = groups[g]->getName() == (g < 16 ? "T" + juce::String(g + 1) : juce::String("Global"))
                    && groups[g]->getParameters(false).size() == (g < 16 ? params::kTrackParams : params::kGlobals);
        if (groupsOk) {   // Master FX: master_fx, then REV, ECHO, EQ, DYN of 8 each
            const auto* m = groups[17];
            const auto sub = m->getSubgroups(false);
            groupsOk = m->getName() == "Master FX" && m->getParameters(false).size() == 1 && m->getParameters(true).size() == params::kMasterGroupParams
                    && sub.size() == params::kMasterBlocks;
            for (int b = 0; b < sub.size() && groupsOk; ++b)
                groupsOk = sub[b]->getName() == params::kMasterBlockNames[size_t(b)] && sub[b]->getParameters(false).size() == params::kMasterBlockParams;
        }
        check(groupsOk, "groups T1..T16 (31 each), Global (4) and Master FX (MASTER FX and the sub-groups REV, ECHO, EQ, DYN of 8 each)");
        bool meta = true;
        for (int i = 0; i < ps.size(); ++i) meta = meta && ps[i]->isMetaParameter() == (i < params::kTracks * params::kTrackParams && i % params::kTrackParams == params::kMachine);
        check(meta, "the 16 machine parameters, which rewrite SYN1-8, are meta parameters (AU: IsGlobalMeta), no other one is");
        const auto raw = [](const juce::RangedAudioParameter& q) { return juce::roundToInt(q.convertFrom0to1(q.getDefaultValue())); };
        check(raw(p.parameter(3, params::kKit + 10)) == 64 && raw(p.parameter(3, params::kKit + 13)) == 127 && raw(p.parameter(3, params::kKit + 17)) == 100
              && raw(p.parameter(3, params::kKit + 18)) == 64 && raw(p.parameter(3, params::kLevel)) == 100 && raw(p.parameter(3, params::kLfo)) == 3
              && raw(p.parameter(3, params::kKit + 1)) == p.synDefault(p.machine(3), 1) && p.noteMap() == NoteMap::WhiteKeys && p.midiChannel() == 0
              && p.accent() == 0 && std::abs(p.tempo() - 120.0) < 1e-3,
              "defaults: EQF 64, FLTW 127, VOL 100, PAN 64, LEV 100, LFO on its own track, SYN at the machine's default; white-key map, omni, accent 0, 120 BPM");
        const auto trxBd = std::find_if(p.machines().begin(), p.machines().end(), [](const auto& m) { return m.id == 16; });
        auto& machine0 = p.parameter(0, params::kMachine);
        check(trxBd != p.machines().end() && machine0.getCurrentValueAsText() == juce::String(trxBd->name)
              && juce::roundToInt(machine0.convertFrom0to1(asBase(machine0).getValueForText(juce::String(trxBd->name).toLowerCase()))) == 16
              && juce::roundToInt(machine0.convertFrom0to1(asBase(machine0).getValueForText("38"))) == 38,
              "machine value text: the OS's name (" + machine0.getCurrentValueAsText() + "), typed back by name or id");
        // getName is what hosts call most: SYN names are cached, the others are the parameters' own
        const auto t0 = std::chrono::steady_clock::now();
        size_t chars = 0;
        constexpr int kRounds = 200;
        for (int r = 0; r < kRounds; ++r)
            for (auto* q : ps) chars += size_t(q->getName(64).length());
        const double ns = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1e9 / double(kRounds * ps.size());
        check(ns < 2000.0, "getName: " + juce::String(juce::roundToInt(ns)) + " ns per call over all " + juce::String(ps.size()) + " parameters (" + juce::String(int(chars)) + " characters)");
    }

    // A machine change as a host makes it (setValueNotifyingHost, possibly off the message thread): the engine plays the new
    // machine with its SYN defaults at once; the SYN parameters take them (unless set since) and are renamed on the message
    // thread (handleParameterChanges, also run by the processor's timer), and the host is told the names changed.
    {
        MdOneProcessor p;
        HostListener host;
        p.addListener(&host);
        p.setTrackParam(0, 0, 5);   // TRX-BD's SYN1 away from its default
        auto& machine = p.parameter(0, params::kMachine);
        const int efmBd = 32;
        machine.setValueNotifyingHost(machine.convertTo0to1(float(efmBd)));
        const bool deferred = p.trackParam(0, 0) == 5 && p.synName(0, 4) == expectedSynName(p, 0, 16, 4);
        auto& syn3 = p.parameter(0, params::kKit + 2);
        syn3.setValueNotifyingHost(syn3.convertTo0to1(99.0f));   // SYN3 set after the machine: a host restoring its automation
        host.changed.clear();
        host.infoChanges = 0;
        p.handleParameterChanges();
        bool values = true, names = true, defaults = true;
        for (int k = 0; k < 8; ++k) {
            values = values && p.trackParam(0, k) == (k == 2 ? 99 : p.synDefault(efmBd, k));
            names = names && p.parameter(0, params::kKit + k).getName(64) == expectedSynName(p, 0, efmBd, k);
            const auto& q = p.parameter(0, params::kKit + k);
            defaults = defaults && juce::roundToInt(q.convertFrom0to1(asBase(q).getDefaultValue())) == p.synDefault(efmBd, k);
        }
        juce::StringArray shown;
        for (int k = 0; k < 8; ++k) shown.add(p.parameter(0, params::kKit + k).getName(64) + " " + juce::String(p.trackParam(0, k)));
        check(deferred, "machine set by the host: the SYN parameters keep their values and names until the message thread runs");
        check(values && names, "after it: renamed and reset to " + p.machineName(efmBd) + "'s defaults, SYN3 set since kept: " + shown.joinIntoString(", "));
        check(host.infoChanges == 1 && std::count(host.changed.begin(), host.changed.end(), 1) == 1 && std::count(host.changed.begin(), host.changed.end(), 3) == 0,
              "the host is told: parameter info changed (" + juce::String(host.infoChanges) + "), SYN1's new value (SYN3 not touched)");
        check(defaults, "SYN1-8's default values are the machine's");
        p.setMachine(5, 20);   // TRX-RS: PTCH DEC DIST and five unused knobs
        check(p.synName(5, 2) == expectedSynName(p, 5, 20, 2) && p.synName(5, 3) == "T6 -" && p.synName(5, 7) == "T6 -" && p.trackParam(5, 0) == p.synDefault(20, 0),
              "setMachine (message thread): renamed and reset when it returns; unused knobs are named \"-\": " + p.synName(5, 2) + ", " + p.synName(5, 3));
        auto& lfoParam = p.parameter(0, params::kLfo + 1);
        p.setTrackLfo(0, 0, 0);
        check(asBase(lfoParam).getText(lfoParam.convertTo0to1(4.0f), 16) == "MOD" && asBase(lfoParam).getText(lfoParam.convertTo0to1(17.0f), 16) == "VOL"
              && asBase(p.parameter(0, params::kLfo + 4)).getText(1.0f, 16) == "HOLD" && asBase(p.parameter(0, params::kLfo)).getText(1.0f, 16) == "T16",
              "LFO page text: destination parameter by its label (MOD, VOL), update HOLD, track T16");
        p.removeListener(&host);
    }
    // ... and the sound: right after the host's change (SYN parameters not yet updated) the engine already plays the defaults
    {
        const std::vector<Note> note = {{24, 10, 36, 100}};
        RenderOptions hostChange;
        hostChange.setup = [](MdOneProcessor& p) {
            p.setTrackParam(0, 0, 5);
            auto& m = p.parameter(0, params::kMachine);
            m.setValueNotifyingHost(m.convertTo0to1(32.0f));
        };
        hostChange.after = [](MdOneProcessor& p) { check(p.trackParam(0, 0) == 5, "(the SYN1 parameter was still 5 while it played)"); };
        RenderOptions api;
        api.setup = [](MdOneProcessor& p) { p.setMachine(0, 32); };
        RenderOptions synSet;
        synSet.setup = [](MdOneProcessor& p) { p.setMachine(0, 32); p.setTrackParam(0, 0, 5); };
        const auto a = render(note, 8192, hostChange), b = render(note, 8192, api), c = render(note, 8192, synSet);
        check(a.ok && b.ok && !silentOf(a.ch[0]) && a.ch[0] == b.ch[0] && a.ch[0] != c.ch[0],
              "the engine plays the new machine's defaults at once (= setMachine; SYN1 at 5 sounds different)");
    }

    // A session through the state: every kind of parameter changed, saved, restored into a fresh processor
    {
        juce::MemoryBlock saved;
        std::vector<float> before;
        RenderOptions edit;
        edit.noteMap = NoteMap::Chromatic;   // not the processor's default (white keys): a restore that skips it plays other tracks
        edit.midiChannel = 3;
        edit.tempo = 133.5;
        edit.hostBpm = 0.0;
        edit.setup = [](MdOneProcessor& p) {
            auto& m = p.parameter(2, params::kMachine);
            m.setValueNotifyingHost(m.convertTo0to1(49.0f));   // E12-SD, as a host sets it
            p.setTrackParam(2, 3, 111);
            p.handleParameterChanges();
            p.setTrackLfo(2, 0, 7);
            p.setTrackLfo(2, 1, 5);
            p.setTrackLfo(2, 2, 3);
            p.setTrackLfo(2, 4, 1);
            p.setTrackParam(2, 22, 90);
            p.setTrackLevel(9, 77);
            p.setAccent(40);
            p.setTrackOutput(4, true);
            p.setMasterFx(9, 5);      // echo MOD
            p.setMasterFx(31, 64);    // dynamix MIX
        };
        edit.after = [&](MdOneProcessor& p) { saved = stateOf(p); before = parameterValues(p); };
        const auto seq = testSequence();
        std::vector<Note> notes = seq;
        for (auto& n : notes) n.channel = 3;
        const auto a = render(notes, kSequenceFrames, edit);
        juce::MemoryBlock again;
        std::vector<float> after;
        RenderOptions restore = edit;
        restore.testKit = false;
        // channel, note map, tempo and master_fx come from the restored state alone: render() must not set them again
        restore.midiChannel = std::nullopt;
        restore.noteMap = std::nullopt;
        restore.tempo = std::nullopt;
        restore.masterFx = std::nullopt;
        restore.setup = [&](MdOneProcessor& p) { p.setStateInformation(saved.getData(), int(saved.getSize())); };
        restore.after = [&](MdOneProcessor& p) {
            again = stateOf(p);
            after = parameterValues(p);
            check(p.midiChannel() == 3 && p.noteMap() == NoteMap::Chromatic && std::abs(p.tempo() - 133.5) < 1e-3 && !p.masterOn()
                      && p.masterFx(9) == 5 && p.masterFx(31) == 64,
                  "a schema 3 state restores the MIDI channel (3), note map (chromatic), tempo (133.5), master_fx (off) and the master bytes");
        };
        const auto b = render(notes, kSequenceFrames, restore);
        check(before.size() == size_t(params::kTotal) && before == after, "an edited session: every parameter restored");
        check(saved == again, "its state saved again is the same bytes (" + juce::String(int(saved.getSize())) + " bytes)");
        check(a.ok && b.ok && hashMain(a) == hashMain(b) && !silentOf(a.ch[0]), "and it renders the same (" + hex(hashMain(a)) + ")");
        const auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), int(saved.getSize()));
        check(xml != nullptr && xml->getIntAttribute("schema") == 3 && xml->getChildByName("PARAMETERS") != nullptr
              && xml->getChildByName("PARAMETERS")->getNumChildElements() == params::kTotal && xml->getChildByName("KIT") != nullptr
              && xml->getIntAttribute("ownBusTracks") == 16 && !xml->toString().contains(os),
              "state: schema 3, " + juce::String(params::kTotal) + " PARAMs, the KIT, ownBusTracks; no OS path");
        // the same session as a schema 2 state (increment 0004: no master parameters): the init kit's master bytes, master_fx on
        if (xml != nullptr) {
            auto old2 = *xml;
            old2.setAttribute("schema", 2);
            auto* values = old2.getChildByName("PARAMETERS");
            for (auto* e = values->getFirstChildElement(); e != nullptr;) {
                auto* next = e->getNextElement();
                const auto id = e->getStringAttribute("id");
                if (id.startsWith("fx_") || id == "master_fx") values->removeChildElement(e, true);
                e = next;
            }
            juce::MemoryBlock state2;
            juce::AudioProcessor::copyXmlToBinary(old2, state2);
            MdOneProcessor p2;
            p2.setStateInformation(state2.getData(), int(state2.getSize()));
            bool init = p2.masterOn();
            for (int i = 0; i < MdOneProcessor::kMasterParams; ++i) init = init && p2.masterFx(i) == md::engine::master::kDefaultParams[size_t(i)];
            check(init && values->getNumChildElements() == params::kTracks * params::kTrackParams + params::kGlobals && p2.midiChannel() == 3 && p2.trackLevel(9) == 77,
                  "a schema 2 state (" + juce::String(values->getNumChildElements()) + " PARAMs) restores its parameters, the init kit's master bytes and master_fx on");
        }
        // schema 1 (increment 0003): the settings it had
        MdOneProcessor p;
        juce::XmlElement old("ModuledrumState");
        old.setAttribute("schema", 1);
        old.setAttribute("firmwarePath", os);
        old.setAttribute("midiChannel", 5);
        old.setAttribute("tempo", 133.0);
        old.setAttribute("ownBusTracks", 3);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary(old, oldState);
        p.setStateInformation(oldState.getData(), int(oldState.getSize()));
        check(p.midiChannel() == 5 && std::abs(p.tempo() - 133.0) < 1e-3 && p.trackOutput(0) && p.trackOutput(1) && !p.trackOutput(2),
              "a schema 1 state (0003) restores its MIDI channel, tempo and track outputs");
    }

    // Kits (the user's snapshot)
    const auto kitsFile = kitsPath.isNotEmpty() ? kitsPath : juce::String(std::getenv("MD_KITS_SYX") ? std::getenv("MD_KITS_SYX") : "");
    if (kitsFile.isEmpty() || !juce::File(kitsFile).existsAsFile()) {
        std::printf("[SKIP] kits: no kit snapshot (set MD_KITS_SYX to a Machinedrum .syx with kits, or pass --kits)\n");
        settingsDir.deleteRecursively();
        return g_failures ? 1 : 0;
    }
    const auto bank = md::library::loadSyxFile(kitsFile.toStdString());
    check(bank.kits.size() == 64, "kit snapshot: " + juce::String(int(bank.kits.size())) + " kits (" + juce::String(bank.usedKits()) + " used)");
    if (bank.kits.empty()) return 1;
    const auto& kit0 = bank.kits[0];
    {
        // every kit: loaded through the processor, the parameters hold its values; exported, it is the same kit dump
        MdOneProcessor p;
        int same = 0, valuesOk = 0, masterOk = 0;
        std::set<std::array<uint8_t, 32>> masterSets;
        for (const auto& k : bank.kits) {
            p.loadKit(k, juce::String(bank.name));
            bool v = true;
            for (int t = 0; t < 16; ++t)
                for (int slot = 0; slot < params::kTrackParams; ++slot) {
                    const auto* f = slot == params::kMachine ? &k.machine[size_t(t)] : slot < params::kLevel ? &k.params[size_t(t)][size_t(slot - params::kKit)]
                                  : slot == params::kLevel ? &k.level[size_t(t)] : nullptr;
                    if (f != nullptr) v = v && p.parameter(t, slot).get() == int(*f);
                }
            valuesOk += v ? 1 : 0;
            // the master bytes: in the parameters (kit order), and back out of them into the kit
            const auto bytes = k.masterFx.bytes();
            bool m = p.parametersToKit().masterFx.bytes() == bytes;
            for (int i = 0; i < MdOneProcessor::kMasterParams; ++i) m = m && p.masterFx(i) == int(bytes[size_t(i)]);
            masterOk += m ? 1 : 0;
            masterSets.insert(bytes);
            same += md::library::encodeKit(p.parametersToKit()) == md::library::encodeKit(k) ? 1 : 0;
        }
        check(valuesOk == int(bank.kits.size()), "every kit loaded: machines, parameters and levels in the parameters (" + juce::String(valuesOk) + " kits)");
        check(masterOk == int(bank.kits.size()) && masterSets.size() > 2, "every kit's 32 master bytes: in the master parameters, and back in parametersToKit ("
              + juce::String(masterOk) + " kits, " + juce::String(int(masterSets.size())) + " different master settings)");
        check(same == int(bank.kits.size()), "every kit exported (parametersToKit + encodeKit) is the same dump byte for byte (" + juce::String(same) + " kits)");
        // kit 0 exported and decoded: the same parameter values; an edit shows in the export
        p.loadKit(kit0, juce::String(bank.name));
        const auto bytes = md::library::encodeKit(p.parametersToKit());
        md::library::Kit back;
        const bool decoded = md::library::decodeKit(bytes.data(), bytes.size(), back);
        bool equal = decoded && back.machine == kit0.machine && back.params == kit0.params && back.level == kit0.level;
        for (int t = 0; t < 16 && equal; ++t) {
            const auto &x = back.lfo[size_t(t)], &y = kit0.lfo[size_t(t)];
            equal = x.destTrack == y.destTrack && x.destParam == y.destParam && x.shape1 == y.shape1 && x.shape2 == y.shape2 && x.type == y.type;
        }
        check(equal && back.name == "TRX" && p.kitName() == "TRX" && p.kitBank() == juce::String(bank.name),
              "kit 0 (" + p.kitName() + ", bank " + p.kitBank() + ") exported with encodeKit decodes to the same machines, parameters, levels and LFO pages");
        p.setTrackParam(3, 17, 12);
        p.setMachine(7, 33);
        p.setMasterFx(10, 99);   // echo MFRQ
        const auto edited = p.parametersToKit();
        check(edited.params[3][17] == 12 && edited.machine[7] == 33 && edited.params[7][0] == p.synDefault(33, 0) && edited.params[0] == kit0.params[0]
                  && edited.masterFx.delay[2] == 99 && edited.masterFx.reverb == kit0.masterFx.reverb && edited.masterFx.dynamics == kit0.masterFx.dynamics,
              "an edit (VOL, a machine change with its SYN defaults, echo MFRQ) is in the export, the rest unchanged");
    }
    {
        // kit 0 played through the processor, master_fx off (the dry main bus): the kit test's hash, the same as straight
        // on the engine with the same warm-up (--check-master plays it with the master on)
        juce::MemoryBlock saved;
        RenderOptions o;
        o.testKit = false;
        o.allBuses = true;
        o.hostBpm = 0.0;
        o.tempo = 125.0;   // HostModel's default, which the engine renders keep
        o.noteMap = NoteMap::WhiteKeys;
        o.setup = [&](MdOneProcessor& p) { p.loadKit(kit0, juce::String(bank.name)); };
        o.after = [&](MdOneProcessor& p) { saved = stateOf(p); };
        const auto seq = kitTestSequence(NoteMap::WhiteKeys);
        const int64_t frames = int64_t(md::golden::kBlocks) * 32;
        const auto a = render(seq, frames, o);
        const auto ha = hashKitTest(a);
        const auto direct = directKitTest(os, kit0, MdOneProcessor::kWarmUpBlocks);
        const auto plain = directKitTest(os, kit0, 0);
        check(a.ok && ha == kKit0ProcessorHashOs163, "kit 0 through the processor, master off (" + juce::String(int(seq.size())) + " notes, 12 s): hash " + hex(ha)
              + ", expected " + hex(kKit0ProcessorHashOs163) + " (peak " + juce::String(20.0 * std::log10(double(peakOf(a.ch[0]))), 1) + " dBFS)");
        check(ha == direct, "the same as applyKit straight on the engine after the processor's " + juce::String(MdOneProcessor::kWarmUpBlocks)
              + " warm-up blocks (" + hex(direct) + ")");
        check(plain == kKit0Hash, "that render without the warm-up is tests/test_kits.cpp's kKit0Hash (" + hex(plain) + ")");
        // saved, restored into a fresh processor, rendered again: the same
        RenderOptions r = o;
        r.setup = [&](MdOneProcessor& p) { p.setStateInformation(saved.getData(), int(saved.getSize())); };
        juce::String restoredKit;
        r.after = [&](MdOneProcessor& p) { restoredKit = p.kitName() + " / " + p.kitBank(); };
        const auto b = render(seq, frames, r);
        check(b.ok && hashKitTest(b) == ha, "state saved, restored into a fresh processor, rendered again: the same hash (" + hex(hashKitTest(b)) + "), kit "
              + restoredKit);
    }
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// ---- The master effects (--check-master) ----

juce::String kitsFileOf(const juce::String& kitsPath);   // the editor's section

// The regression render (testSequence on the test kit, which leaves the master bytes at the init kit's) with master_fx on:
// the main output at 44.1 kHz with OS 1.63, the reported latency removed, hashed as hashMain hashes it. One per note map,
// as kRenderHashOs163 / kRenderHashWhiteKeysOs163 are the same renders with the master off.
constexpr uint64_t kMasterRenderHashOs163 = 0xece6201f17cf1b81ull;            // NoteMap::Chromatic
constexpr uint64_t kMasterRenderHashWhiteKeysOs163 = 0x7fd32e31f89ea574ull;   // NoteMap::WhiteKeys
// Kit 0 of the user's snapshot through the processor with the master on (as kKit0ProcessorHashOs163, hashKitTest): the
// main bus is the master output at the kit's master settings
constexpr uint64_t kKit0MasterProcessorHashOs163 = 0x288d02a442ac6b0bull;

// The first frame with a non-zero sample, -1 for silence
int64_t onsetOf(const std::vector<float>& x)
{
    const auto it = std::find_if(x.begin(), x.end(), [](float v) { return v != 0.f; });
    return it == x.end() ? -1 : int64_t(it - x.begin());
}

// The kit test straight on the engine with the kit's master effects, as the processor does a kit load: directKitTest's
// steps, then the kit's 32 master bytes (HostModel::setMasterFx) and EngineT::primeMaster. Hashed as hashKitTest hashes a
// processor render with the master on and its latency removed: the master output kMasterLatency frames on (its dry path is
// that late, and the processor lines the other buses up with it), the sends as the mixer made them.
uint64_t directKitTestMaster(const juce::String& os, const md::library::Kit& kit, int warmUp)
{
    const std::filesystem::path path(os.toStdString());
    const auto firmware = md::fw::loadFirmware(path);
    auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(path)));
    md::engine::Engine eng(firmware, std::move(c.sections.at(0).data));
    md::engine::Engine::Output out;
    for (int b = 0; b < warmUp; ++b) eng.render(out);
    md::library::applyKit(eng.host(), kit);
    const auto bytes = kit.masterFx.bytes();
    for (int i = 0; i < MdOneProcessor::kMasterParams; ++i) eng.host().setMasterFx(i, bytes[size_t(i)]);
    eng.primeMaster();
    md::golden::Pattern pattern;
    std::vector<std::array<int32_t, 2>> main, rev, del;
    for (int b = 0; b < md::golden::kBlocks + 1; ++b) {   // one block more: the master output's last frames
        if (b < md::golden::kBlocks) pattern.beforeBlock(eng.host(), b);
        eng.render(out);
        for (int f = 0; f < 32; ++f) {
            main.push_back({out.master[size_t(f)][0], out.master[size_t(f)][1]});
            rev.push_back({out.mix.rev[size_t(f)][0], out.mix.rev[size_t(f)][1]});
            del.push_back({out.mix.del[size_t(f)][0], out.mix.del[size_t(f)][1]});
        }
    }
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](int64_t v) { h ^= uint64_t(v); h *= 1099511628211ull; };
    for (size_t i = 0; i < size_t(md::golden::kBlocks) * 32; ++i)
        for (size_t c2 = 0; c2 < 2; ++c2) {
            mix(main[i + MdOneProcessor::kMasterLatency][c2]);
            for (int q = 0; q < 6; ++q) mix(0);
            mix(rev[i][c2]);
            mix(del[i][c2]);
        }
    return h;
}

int checkMaster(const juce::String& os, const juce::String& kitsPath)
{
    if (os.isEmpty() || !juce::File(os).existsAsFile()) {
        std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n");
        return 77;
    }
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-master");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    md::plugin::saveSharedOsPath(os);
    using NoteMap = MdOneProcessor::NoteMap;
    constexpr int kLatency = MdOneProcessor::kMasterLatency;

    // master_fx on by default; the latency and the tail follow it; the first live callback compiles nothing in the master
    {
        MdOneProcessor p;
        const auto expectLag = [](double rate) { return rate == 44100.0 ? kLatency : int(std::ceil((36.0 + kLatency) * rate / 44100.0)); };
        p.setRateAndBufferSizeDetails(44100.0, 512);
        p.prepareToPlay(44100.0, 512);
        check(p.engineReady() && p.masterOn() && p.masterActive() && p.getLatencySamples() == kLatency && p.getTailLengthSeconds() == MdOneProcessor::kMasterTailSeconds,
              "master_fx on by default: latency " + juce::String(p.getLatencySamples()) + " samples at 44.1 kHz (the master's dry path), tail "
              + juce::String(p.getTailLengthSeconds(), 1) + " s");
        const int compiled = p.masterCompiledBlocks();
        juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), 512);
        juce::MidiBuffer midi;
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock(buf, midi);
        const double first = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::vector<double> times;
        for (int b = 0; b < 64; ++b) {
            const auto t1 = std::chrono::steady_clock::now();
            p.processBlock(buf, midi);
            times.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count());
        }
        std::sort(times.begin(), times.end());
        const bool jit = dsp56k::g_jitSupported;   // the interpreter build compiles nothing
        check((jit ? compiled > 0 && p.masterCompiledBlocks() == compiled : compiled == 0) && first < 5e-3,
              "the first live callback after the OS load (512 samples, 16 engine blocks): " + juce::String(first * 1e6, 0) + " us (median of the next 64: "
              + juce::String(times[times.size() / 2] * 1e6, 0) + " us); " + (jit ? "the master DSP's " + juce::String(compiled)
              + " compiled JIT blocks unchanged (" + juce::String(p.masterCompiledBlocks()) + "): its warm-up compiled them" : juce::String("interpreter: no JIT")));
        p.setMasterOn(false);
        check(!p.masterActive() && p.getLatencySamples() == 0 && p.getTailLengthSeconds() == 0.0, "master_fx off: latency 0, tail 0");
        p.setMasterOn(true);
        check(p.getLatencySamples() == kLatency, "on again: latency " + juce::String(p.getLatencySamples()));
        for (const double rate : {48000.0, 96000.0}) {
            p.setRateAndBufferSizeDetails(rate, 512);
            p.prepareToPlay(rate, 512);
            const int on = p.getLatencySamples();
            p.setMasterOn(false);
            const int off = p.getLatencySamples();
            p.setMasterOn(true);
            check(on == expectLag(rate) && off == int(std::ceil(36.0 * rate / 44100.0)) && p.getLatencySamples() == on,
                  juce::String(rate / 1000.0, 0) + " kHz: latency " + juce::String(on) + " samples with the master (the converter's 35 + 1 frames and the 18 as one delay, "
                  "rounded up), " + juce::String(off) + " without");
        }
    }

    // The regression render with the master on: its hash per note map, the same for any host block size, and the
    // master-off render's track and send buses sample for sample
    const auto seq = testSequence();
    RenderOptions on;
    on.masterFx = true;
    int compiledBefore = -1, compiledAfter = -2;
    on.setup = [&](MdOneProcessor& p) { compiledBefore = p.masterCompiledBlocks(); };
    on.after = [&](MdOneProcessor& p) { compiledAfter = p.masterCompiledBlocks(); };
    const auto base = render(seq, kSequenceFrames, on);
    if (!base.ok) return 1;
    const auto hash = hashMain(base);
    check(hash == kMasterRenderHashOs163, "master on, 44.1 kHz, 512-sample blocks: main output hash " + hex(hash) + ", expected " + hex(kMasterRenderHashOs163)
          + " (peak " + juce::String(shapeOf(base, 44100.0).peakDb, 2) + " dBFS, latency " + juce::String(base.latency) + ")");
    check(compiledAfter == compiledBefore && (compiledBefore > 0 || !dsp56k::g_jitSupported),
          "no JIT compile in the master DSP while it played (" + juce::String(compiledBefore) + " blocks before and after)");
    RenderOptions odd = on;
    odd.block = 61;
    odd.setup = {};
    odd.after = {};
    const auto oddBlocks = render(seq, kSequenceFrames, odd);
    check(oddBlocks.ok && oddBlocks.ch[0] == base.ch[0] && oddBlocks.ch[1] == base.ch[1], "master on, 61-sample blocks: the same samples");
    {
        RenderOptions white = odd;
        white.block = 512;
        white.noteMap = NoteMap::WhiteKeys;
        const auto w = render(seq, kSequenceFrames, white);
        check(w.ok && hashMain(w) == kMasterRenderHashWhiteKeysOs163, "master on, white-key map: main output hash " + hex(hashMain(w)) + ", expected "
              + hex(kMasterRenderHashWhiteKeysOs163));
    }
    RenderOptions off;
    const auto dry = render(seq, kSequenceFrames, off);
    check(dry.ok && hashMain(dry) == kRenderHashOs163 && hashMain(dry) != hash, "master off: the dry render, " + hex(hashMain(dry)));
    {
        // every bus on, track 1 on its own: with the master on, the track and send buses are the master-off render's
        RenderOptions a = on, b = off;
        a.setup = b.setup = [](MdOneProcessor& p) { p.setTrackOutput(0, true); p.setTrackOutput(9, true); };
        a.after = {};
        a.allBuses = b.allBuses = true;
        const auto ra = render(seq, kSequenceFrames, a), rb = render(seq, kSequenceFrames, b);
        bool same = ra.ok && rb.ok;
        int compared = 0;
        for (int bus = 1; same && bus < MdOneProcessor::kNumBuses; ++bus)
            for (int c = 0; c < (bus < MdOneProcessor::kReverbBus ? 1 : 2); ++c, ++compared) same = same && ra.bus(bus, c) == rb.bus(bus, c);
        check(same && !silentOf(ra.bus(MdOneProcessor::kTrackBus)) && !silentOf(ra.bus(MdOneProcessor::kReverbBus)) && ra.latency == kLatency,
              "master on: the 16 track buses and both send buses (" + juce::String(compared) + " channels) are the master-off render's, sample for sample, "
              "once the reported latency is taken off");
    }

    // An impulse (GND-IM) lands on the same engine frame on every bus: track 1 in the main mix with both sends up, track 2
    // the same on its own bus, triggered together
    {
        const int64_t at = 1000 + 7;
        const std::vector<Note> notes = {{at, 10, 36, 127}, {at, 10, 37, 127}};
        RenderOptions o;
        o.testKit = false;
        o.allBuses = true;
        o.masterFx = true;
        o.setup = [](MdOneProcessor& p) {
            for (int t = 0; t < 2; ++t) {
                p.setMachine(t, 3);             // GND-IM
                p.setTrackParam(t, 19, 127);    // DEL
                p.setTrackParam(t, 20, 127);    // REV
            }
            p.setTrackOutput(1, true);
        };
        const auto r = render(notes, 8192, o);
        RenderOptions dryO = o;
        dryO.masterFx = false;
        const auto d = render(notes, 8192, dryO);
        const int64_t main = onsetOf(r.bus(0)), mainR = onsetOf(r.bus(0, 1)), track = onsetOf(r.bus(MdOneProcessor::kTrackBus + 1)),
                      rev = onsetOf(r.bus(MdOneProcessor::kReverbBus)), del = onsetOf(r.bus(MdOneProcessor::kDelayBus)), dryMain = onsetOf(d.bus(0));
        check(r.ok && d.ok && main >= at && main == mainR && main == track && main == rev && main == del && main == dryMain,
              "an impulse triggered at frame " + juce::String(at) + " lands at frame " + juce::String(main) + " (engine block boundary + the voice) on the main bus L/R ("
              + juce::String(main) + "/" + juce::String(mainR) + "), track 2's bus (" + juce::String(track) + "), the reverb send (" + juce::String(rev) + ") and the delay send ("
              + juce::String(del) + ") with the master on, and on the dry main bus with it off (" + juce::String(dryMain) + ")");
    }

    // 48 and 96 kHz: the 44.1 kHz master render through the converter, with the latency reported
    for (const double rate : {48000.0, 96000.0}) {
        RenderOptions o = odd;
        o.rate = rate;
        o.block = rate > 50000.0 ? 1024 : 256;
        const auto r = render(seq, kSequenceFrames, o);
        if (!r.ok) continue;
        float worst = 0.f;
        size_t compared = 0;
        for (int c = 0; c < 2; ++c) {
            const auto ref = convertOffline(base.ch[size_t(c)], rate, r.ch[size_t(c)].size());
            compared = ref.size();
            for (size_t i = 0; i < ref.size(); ++i) worst = std::max(worst, std::abs(ref[i] - r.ch[size_t(c)][i]));
        }
        check(compared > r.ch[0].size() - 200 && worst < 1e-6f, juce::String(rate / 1000.0, 0) + " kHz, master on: the 44.1 kHz render through the converter with the "
              + juce::String(r.latency) + " samples of latency taken off, max difference " + juce::String(worst, 9) + " over " + juce::String(compared) + " samples");
    }

    // Master edits: with the master on they change the sound; with it off the main bus stays the dry mix
    {
        const auto lev0 = [](MdOneProcessor& p) { p.setMasterFx(7, 0); p.setMasterFx(23, 64); };   // reverb LEV 0, EQ GAIN 64
        RenderOptions a = odd, b = off;
        a.setup = b.setup = lev0;
        const auto ra = render(seq, kSequenceFrames, a), rb = render(seq, kSequenceFrames, b);
        check(ra.ok && hashMain(ra) != hash && rb.ok && hashMain(rb) == kRenderHashOs163,
              "reverb LEV 0 and EQ GAIN 64: the master render changes (" + hex(hashMain(ra)) + "), the dry one does not");
    }

    // Switching while it plays: the latency follows, nothing faults, the master DSP goes on where it stood
    {
        MdOneProcessor p;
        setupTestKit(p);
        p.setMidiChannel(10);
        p.setNoteMap(NoteMap::Chromatic);
        p.setRateAndBufferSizeDetails(44100.0, 256);
        p.prepareToPlay(44100.0, 256);
        juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), 256);
        juce::MidiBuffer midi;
        bool ok = true, sound = false;
        juce::StringArray lags;
        for (int b = 0; b < 700; ++b) {
            if (b % 100 == 50) {
                p.setMasterOn(!p.masterOn());
                lags.add(juce::String(p.getLatencySamples()));
                ok = ok && p.getLatencySamples() == (p.masterOn() ? kLatency : 0);
            }
            midi.clear();
            if (b % 16 == 0) midi.addEvent(juce::MidiMessage::noteOn(10, 36 + (b / 16) % 16, uint8_t(100)), 3);
            p.processBlock(buf, midi);
            for (int i = 0; i < 256; ++i) {
                const float v = buf.getSample(0, i);
                ok = ok && std::isfinite(v) && std::abs(v) < 4.0f;
                sound = sound || v != 0.f;
            }
        }
        check(ok && sound && p.engineReady() && !p.statusText().contains("FAULT"),
              "master_fx switched 7 times while playing: the reported latency follows (" + lags.joinIntoString(" ") + "), no fault: " + p.statusText());
    }

    // Kit 0 with the master on: the engine's master output at the kit's settings, the other buses lined up with it
    const auto kitsFile = kitsFileOf(kitsPath);
    if (kitsFile.isEmpty() || !juce::File(kitsFile).existsAsFile()) {
        std::printf("[SKIP] kits: no kit snapshot (set MD_KITS_SYX to a Machinedrum .syx with kits, or pass --kits)\n");
    } else {
        const auto bank = md::library::loadSyxFile(kitsFile.toStdString());
        const auto& kit0 = bank.kits.at(0);
        RenderOptions o;
        o.testKit = false;
        o.allBuses = true;
        o.hostBpm = 0.0;
        o.tempo = 125.0;
        o.noteMap = NoteMap::WhiteKeys;
        o.masterFx = true;
        o.setup = [&](MdOneProcessor& p) { p.loadKit(kit0, juce::String(bank.name)); };
        const auto r = render(kitTestSequence(NoteMap::WhiteKeys), int64_t(md::golden::kBlocks) * 32, o);
        const auto h = hashKitTest(r);
        const auto direct = directKitTestMaster(os, kit0, MdOneProcessor::kWarmUpBlocks);
        check(r.ok && h == kKit0MasterProcessorHashOs163, "kit 0 (" + juce::String(kit0.name) + ") through the processor with the master on: hash " + hex(h)
              + ", expected " + hex(kKit0MasterProcessorHashOs163) + " (peak " + juce::String(20.0 * std::log10(double(peakOf(r.ch[0]))), 1) + " dBFS)");
        check(h == direct, "the same as the engine's master output with applyKit, the kit's master bytes and primeMaster after the processor's "
              + juce::String(MdOneProcessor::kWarmUpBlocks) + " warm-up blocks, the sends " + juce::String(kLatency) + " frames behind it (" + hex(direct) + ")");
    }
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// CPU use: every track triggered 8 times per beat at 120 BPM (every 86 engine blocks, ~1/16 s), the test kit.
int bench(const juce::String& os, double seconds)
{
    if (os.isEmpty() || !juce::File(os).existsAsFile()) { std::printf("bench needs the OS file\n"); return 77; }
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-bench");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    md::plugin::saveSharedOsPath(os);
    constexpr int kStepBlocks = 86;
    const auto frames = int64_t(seconds * 44100.0);
    std::vector<Note> notes;
    for (int64_t f = 24; f < frames; f += kStepBlocks * 32)
        for (int t = 0; t < 16; ++t) notes.push_back({f, 10, md::plugin::one::params::noteForTrack(t, MdOneProcessor::NoteMap::WhiteKeys), 100});

    // the engine alone (md::engine::Engine with the same kit and triggers)
    {
        const std::filesystem::path path(os.toStdString());
        const auto firmware = md::fw::loadFirmware(path);
        auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(path)));
        md::engine::Engine eng(firmware, std::move(c.sections.at(0).data));
        auto& h = eng.host();
        forTestKit([&](int t, int m) { h.setMachine(t, uint8_t(m)); }, [&](int t, int k, int v) { h.setParam(t, k, v); });
        h.setTempo(120.0);
        md::engine::Engine::Output out;
        const int64_t blocks = frames / 32;
        double s = 0.0, worst[2] = {};   // worst block with and without the control tick (every 11th block)
        for (int64_t b = 0; b < blocks; ++b) {
            if (b % kStepBlocks == 1) for (int t = 0; t < 16; ++t) h.trigger(t, 100);
            const auto t0 = std::chrono::steady_clock::now();
            eng.render(out);
            const double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            s += d;
            auto& w = worst[b % 11 == 0 ? 1 : 0];
            if (b >= 44100 / 32) w = std::max(w, d);   // after the first second (first touches of the engine's memory)
        }
        std::printf("engine alone:              %5.1f %% of real time (%.2f s for %.1f s of audio); worst 32-frame block after 1 s %.0f us, %.0f us with the control tick (a block is %.0f us)\n",
                    100.0 * s / (double(blocks) * 32 / 44100.0), s, double(blocks) * 32 / 44100.0, worst[0] * 1e6, worst[1] * 1e6, 32 / 44100.0 * 1e6);
    }
    // the processor as it ships (master_fx on) at three rates, then at 44.1 kHz with the master off for reference
    struct Run { double rate; bool master; };
    for (const Run run : {Run{44100.0, true}, Run{48000.0, true}, Run{96000.0, true}, Run{44100.0, false}}) {
        const double rate = run.rate;
        MdOneProcessor p;
        if (!p.engineReady()) { std::printf("no engine: %s\n", p.statusText().toRawUTF8()); return 1; }
        setupTestKit(p);
        p.setMidiChannel(10);
        p.setNoteMap(MdOneProcessor::NoteMap::WhiteKeys);
        p.setMasterOn(run.master);
        constexpr int kBlock = 64;
        p.setRateAndBufferSizeDetails(rate, kBlock);
        p.prepareToPlay(rate, kBlock);
        juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), kBlock);
        juce::MidiBuffer midi;
        const auto total = int64_t(double(frames) * rate / 44100.0);
        size_t next = 0;
        double sum = 0.0;
        int callbacks = 0;
        std::vector<double> times;   // after the first second (first touches of the engine's memory)
        md::plugin::check::AllocCounts allocs;   // in processBlock, after the first second
        int jitCallbacks = 0;
        const double budget = kBlock / rate;
        for (int64_t pos = 0; pos + kBlock <= total; pos += kBlock) {
            midi.clear();
            for (; next < notes.size(); ++next) {
                const auto s = int64_t(std::llround(double(notes[next].frame) * rate / 44100.0));
                if (s >= pos + kBlock) break;
                midi.addEvent(juce::MidiMessage::noteOn(notes[next].channel, notes[next].note, uint8_t(notes[next].velocity)), int(s - pos));
            }
            const bool warm = pos >= int64_t(rate);
            if (warm) md::plugin::check::AllocCounter::begin();
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock(buf, midi);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (warm) {
                const auto c = md::plugin::check::AllocCounter::end();
                allocs += c;
                jitCallbacks += c.jit > 0 ? 1 : 0;
            }
            sum += s; ++callbacks;
            if (warm) times.push_back(s);
        }
        std::sort(times.begin(), times.end());
        const auto at = [&](double q) { return times[std::min(times.size() - 1, size_t(q * double(times.size())))]; };
        const auto over = std::count_if(times.begin(), times.end(), [&](double t) { return t > budget; });
        std::printf("processor %3.0f kHz, %d-sample blocks, master %s: %5.1f %% of real time; callbacks after 1 s (budget %.0f us): 99 %% %.0f us, 99.9 %% %.0f us, max %.0f us, %d of %d over budget; latency %d; allocations after 1 s: %llu, and %llu in the JIT's compiles in %d callbacks\n",
                    rate / 1000.0, kBlock, run.master ? "on " : "off", 100.0 * sum / (double(callbacks) * budget), budget * 1e6, at(0.99) * 1e6, at(0.999) * 1e6, times.back() * 1e6, int(over), int(times.size()), p.getLatencySamples(),
                    (unsigned long long) allocs.outsideJit(), (unsigned long long) allocs.jit, jitCallbacks);
    }
    settingsDir.deleteRecursively();
    return 0;
}

// ---- The real-time path (--check-rt) ----

// No heap allocation in processBlock (.clew increment 0014). Each setting renders kRtSeconds of the bench's load (all
// 16 tracks triggered 8 times per beat) plus what a session adds: editor keyboard notes, automation, tempo and machine
// changes between blocks (each as a host or the editor makes it, outside the counted calls). Every allocation in every
// processBlock call counts (AllocCounter: malloc level, operator new among them), from the first call on. The one
// exception is dsp56300's JIT: it compiles each voice-program block on the audio thread the first time the program
// reaches it, and that allocates (inherent to the emulator, not in scope here); AllocCounter tells those allocations
// apart by their call stacks and they are reported, not held against the check. The sequence plays twice (it repeats
// every kRtPeriod seconds); the second time through shows the JIT settled (it compiles little or nothing more).
int checkRt(const juce::String& os)
{
    using md::plugin::check::AllocCounter;
    using md::plugin::check::AllocCounts;
    if (os.isEmpty() || !juce::File(os).existsAsFile()) { std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n"); return 77; }
    if (!AllocCounter::mallocHooked()) { std::printf("[SKIP] counting allocations needs macOS's malloc_logger (to tell the JIT's apart)\n"); return 77; }
    check(AllocCounter::selfTest(), "the allocation counter sees operator new and malloc");
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-rt");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    md::plugin::saveSharedOsPath(os);
    const bool trace = std::getenv("MD_RT_ALLOC_TRACE") != nullptr;

    constexpr double kRtPeriod = 4.0, kRtSeconds = 2 * kRtPeriod;
    constexpr int64_t kStepFrames = 86 * 32;   // 16ths at ~120 BPM
    struct Setting { double rate; int block; bool allBuses; };
    for (const Setting s : {Setting{44100.0, 64, false}, Setting{48000.0, 480, true}, Setting{96000.0, 256, false}, Setting{44100.0, 1024, true}}) {
        MdOneProcessor p;
        if (!p.engineReady()) { check(false, "no engine: " + p.statusText()); return 1; }
        setupTestKit(p);
        p.setMidiChannel(10);
        p.setNoteMap(MdOneProcessor::NoteMap::WhiteKeys);
        p.setAccent(64);
        if (s.allBuses) {
            auto layout = p.getBusesLayout();
            for (int b = 1; b < MdOneProcessor::kNumBuses; ++b)
                layout.outputBuses.getReference(b) = b < MdOneProcessor::kReverbBus ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            p.setBusesLayout(layout);
            for (int t = 0; t < 16; t += 3) p.setTrackOutput(t, true);
        }
        p.setRateAndBufferSizeDetails(s.rate, s.block);
        p.prepareToPlay(s.rate, s.block);
        FixedPlayHead head;
        p.setPlayHead(&head);
        juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), s.block);
        juce::MidiBuffer midi;
        midi.ensureSize(4096);
        const auto period = int64_t(kRtPeriod * s.rate), total = int64_t(kRtSeconds * s.rate);
        const auto stepSamples = int64_t(double(kStepFrames) * s.rate / 44100.0);
        AllocCounts pass[2];
        uint64_t callbacks[2] = {}, jitCallbacks[2] = {};
        int traced = 0;
        for (int64_t pos = 0; pos + s.block <= total; pos += s.block) {
            const int64_t at = pos % period;   // the sequence: the same every period
            // host MIDI: every track on each 16th, full velocity (accented) on the downbeats, a note-off for each
            midi.clear();
            for (int64_t step = (at + stepSamples - 1) / stepSamples * stepSamples; step < at + s.block; step += stepSamples) {
                const int vel = (step / stepSamples) % 4 == 0 ? 127 : 90;
                for (int t = 0; t < 16; ++t) {
                    const int note = md::plugin::one::params::noteForTrack(t, MdOneProcessor::NoteMap::WhiteKeys);
                    midi.addEvent(juce::MidiMessage::noteOn(10, note, uint8_t(vel)), int(step - at));
                    midi.addEvent(juce::MidiMessage::noteOff(10, note), int(step - at));
                }
            }
            // the editor and the host between blocks: a key on the editor's keyboard, automation, machine changes
            const auto crossed = [&](double sec) { const auto x = int64_t(sec * s.rate); return at <= x && x < at + s.block; };
            if (crossed(0.3) || crossed(2.1)) p.keyboardState().noteOn(1, 40, 1.0f);
            if (crossed(0.4) || crossed(2.2)) p.keyboardState().noteOff(1, 40, 0.0f);
            if (crossed(1.0)) { p.setTrackParam(2, 12, 90); p.setTrackParam(1, 21, 100); p.setTempo(133.0); head.bpm = 0.0; }
            if (crossed(3.0)) { p.setTrackParam(2, 12, 30); p.setTrackParam(1, 21, 64); p.setTempo(120.0); head.bpm = 120.0; }
            if (crossed(1.5)) { p.setMachine(0, 32); p.setMachine(5, 17); }
            if (crossed(3.5)) { p.setMachine(0, 16); p.setMachine(5, 49); }
            // the master effects: edits (the OS's smoothing and parameter code), a kit load (the master primed on the audio
            // thread), master_fx off and on again (the buses' alignment and the converter's read position switch)
            if (crossed(0.7)) { p.setMasterFx(8, 64); p.setMasterFx(11, 90); p.setMasterFx(7, 127); p.setMasterFx(26, 20); }
            if (crossed(2.6)) { p.setMasterFx(8, 16); p.setMasterFx(11, 30); p.setMasterFx(26, 127); }
            if (crossed(1.2)) p.setMasterOn(false);
            if (crossed(1.8)) p.setMasterOn(true);
            if (crossed(2.4)) p.loadDefaultKit();

            AllocCounter::begin();
            p.processBlock(buf, midi);
            const auto c = AllocCounter::end();
            const int k = pos >= period ? 1 : 0;
            pass[k] += c;
            ++callbacks[k];
            jitCallbacks[k] += c.jit > 0 ? 1 : 0;
            if (trace && c.outsideJit() > 0 && traced++ < 4)
                std::printf("  callback at %.4f s, %llu allocations outside the JIT:\n%s", double(pos) / s.rate,
                            (unsigned long long) c.outsideJit(), AllocCounter::report().c_str());
        }
        p.setPlayHead(nullptr);
        const auto what = juce::String(s.rate / 1000.0, 1) + " kHz, " + juce::String(s.block) + "-sample blocks" + (s.allBuses ? ", every bus on" : "");
        for (int k = 0; k < 2; ++k)
            std::printf("       %s, %s pass, %llu callbacks: %llu allocations outside the JIT; it compiled in %llu callbacks, %llu allocations (all: %llu, %llu by operator new)\n",
                        what.toRawUTF8(), k ? "second" : "first", (unsigned long long) callbacks[k], (unsigned long long) pass[k].outsideJit(),
                        (unsigned long long) jitCallbacks[k], (unsigned long long) pass[k].jit, (unsigned long long) pass[k].all, (unsigned long long) pass[k].news);
        const auto outside = pass[0].outsideJit() + pass[1].outsideJit();
        check(outside == 0, what + ": no allocation in processBlock outside the JIT's compiles"
                                + (outside > 0 && !trace ? " (MD_RT_ALLOC_TRACE=1 prints where)" : ""));
    }
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// ---- The editor (--snapshot, --check-editor) ----

using md::plugin::one::KnobCell;
using md::plugin::one::KnobPage;
using md::plugin::one::OneView;

// The kit snapshot: --kits, else $MD_KITS_SYX
juce::String kitsFileOf(const juce::String& kitsPath)
{
    if (kitsPath.isNotEmpty()) return kitsPath;
    const char* env = std::getenv("MD_KITS_SYX");
    return env != nullptr ? juce::String(env) : juce::String();
}

// Runs the processor for n host blocks of 512 at 44.1 kHz (the engine, the notes the editor's keyboard queued)
void pump(MdOneProcessor& p, int n)
{
    juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < n; ++b) p.processBlock(buf, midi);
}

// A track's LFO page set to something worth looking at: onto track 3's SYN3, exponential and saw, retriggered
void setLfoExample(MdOneProcessor& p, int track)
{
    p.setTrackLfo(track, 0, 2);
    p.setTrackLfo(track, 1, 2);
    p.setTrackLfo(track, 2, 4);
    p.setTrackLfo(track, 3, 1);
    p.setTrackLfo(track, 4, 1);
    p.setTrackParam(track, 21, 96);
    p.setTrackParam(track, 22, 40);
    p.setTrackParam(track, 23, 20);
}

int snapshot(const juce::File& outDir, const juce::String& os, const juce::String& kitsPath)
{
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-snapshot");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    outDir.createDirectory();
    MdOneProcessor p;
    using md::plugin::scale::ScaledEditor;
    // windowScale: the editor resized to that much of its base size, as the corner grip or a host does it; `setup` puts
    // the view in the state to show (a tab, the picker, an edit) before the capture
    const auto shot = [&](const juce::String& name, double windowScale, const std::function<void(OneView&)>& setup = {}) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        const auto base = dynamic_cast<ScaledEditor&>(*editor).baseSize();
        editor->setSize(juce::roundToInt(base.x * windowScale), juce::roundToInt(base.y * windowScale));
        auto* view = md::plugin::one::oneViewOf(editor.get());
        if (view != nullptr && setup) { setup(*view); view->refresh(); }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile(name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream);
        check(ok, "wrote " + file.getFullPathName() + " (" + juce::String(image.getWidth()) + "x" + juce::String(image.getHeight()) + ", " + p.statusText() + ")");
    };
    // each state at the base size (3 screen px per LCD px) and at 200 %
    const auto both = [&](const juce::String& name, const std::function<void(OneView&)>& setup = {}) {
        shot(name, 1.0, setup);
        shot(name + "-200", 2.0, setup);
    };
    shot("unconfigured", 1.0);
    const auto wrong = settingsDir.getChildFile("not-an-os.syx");
    settingsDir.createDirectory();
    wrong.replaceWithText("not a Machinedrum OS file\n");
    p.setFirmwarePath(wrong.getFullPathName());
    shot("wrong-file", 1.0);
    juce::String last = "wrong-file";
    if (os.isNotEmpty() && juce::File(os).existsAsFile()) {
        p.setFirmwarePath(os);
        runSilent(p, 44100.0, 512, 100);
        // the default kit's track 1 (TRX-BD) with an LFO set up, every page of a track; a track 3 just triggered
        setLfoExample(p, 0);
        both("track", [&](OneView& v) {
            v.selectTrack(0);
            v.trackStrip().triggered(1u << 2);
        });
        both("picker", [](OneView& v) { v.selectTrack(0); v.openMachinePicker(false); });
        both("edit", [](OneView& v) {   // AMD's value typed: the inline editor
            v.selectTrack(0);
            v.page(OneView::Effects).beginEdit(0);
            v.page(OneView::Effects).valueEditor().setText("99", false);
        });
        shot("global-empty", 1.0, [](OneView& v) { v.showGlobal(); });   // no kit file yet
        // the MASTER tab: the default kit's master settings (a cleared MD kit's), then switched off (the pages say so)
        both("master", [](OneView& v) { v.showMaster(); });
        shot("master-off", 1.0, [&p](OneView& v) { p.setMasterOn(false); v.showMaster(); });
        p.setMasterOn(true);
        // the user's kit snapshot in the kit folder, its kit 4 loaded from the browser
        const auto kits = kitsFileOf(kitsPath);
        if (kits.isNotEmpty() && juce::File(kits).existsAsFile()) {
            juce::File(kits).copyFileTo(md::plugin::sharedKitFolder().getChildFile(juce::File(kits).getFileName()));
            both("global", [](OneView& v) {
                v.kitLibrary().refresh(true);
                v.showGlobal();
                v.refresh();
                v.kitsPage().loadKit(0, 4);
            });
            both("kit-track", [](OneView& v) { v.selectTrack(4); });   // a track of that kit
            both("kit-master", [](OneView& v) { v.showMaster(); });     // that kit's master effects
        }
        last = "track";
        shot("status", 1.0, [](OneView& v) { v.setStatusShown(true); });   // MENU > Show Engine Status: the view grows
        // MENU > Skin: every preset on a track page, then the custom skin dialog (over the default skin again)
        for (int i = 0; i < md::plugin::skin::kNumPresets; ++i) {
            const auto preset = md::plugin::skin::Preset(i);
            if (preset == md::plugin::skin::Preset::Custom) continue;
            shot("skin-" + juce::String(md::plugin::skin::kPresetNames[i]).toLowerCase().replace(" ", "-"), 1.0,
                 [preset](OneView& v) { v.selectTrack(4); v.applySkin(md::plugin::skin::presetSkin(preset)); });
        }
        shot("skin-dialog", 1.0, [](OneView& v) { v.selectTrack(4); v.applySkin(md::plugin::skin::presetSkin(md::plugin::skin::Preset::Machinedrum)); v.showSkinDialog(); });
    }
    shot(last + "-50", 0.5);
    {   // the size: 50-200 % of the base, aspect ratio locked, and the last size the window was left at is shared
        check(md::plugin::scale::load() == 0.5, "the last window size is the shared setting: uiScale " + juce::String(md::plugin::scale::load()));
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        const auto base = dynamic_cast<ScaledEditor&>(*editor).baseSize();
        auto* c = editor->getConstrainer();
        const auto constrained = [&](int w, int h) {
            juce::Rectangle<int> r(0, 0, w, h);
            c->checkBounds(r, editor->getBounds(), {}, false, false, true, true);
            return r;
        };
        const auto big = constrained(base.x * 3, base.y * 3), small = constrained(base.x / 4, base.y / 4), skewed = constrained(base.x * 3 / 2, base.y);
        const double ratio = double(base.x) / base.y;
        check(c != nullptr && big.getWidth() == base.x * 2 && big.getHeight() == base.y * 2, "300 % is held at 200 %: " + big.toString());
        check(std::abs(small.getWidth() - base.x * 0.5) <= 1 && std::abs(small.getHeight() - base.y * 0.5) <= 1, "25 % is held at 50 %: " + small.toString());
        check(std::abs(double(skewed.getWidth()) / skewed.getHeight() - ratio) < 0.01, "a stretched size keeps the aspect ratio: " + skewed.toString()
              + ", base " + juce::String(base.x) + "x" + juce::String(base.y));
    }
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

// A mouse event on `c` as the window would deliver it (local coordinates)
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> pos, juce::Point<float> down, juce::ModifierKeys mods, int clicks, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f,
                            0.0f, &c, &c, now, down, now, clicks, dragged);
}

// A press at `at` (local), dragged by `by` in four moves, released
void dragCell(juce::Component& c, juce::Point<float> at, juce::Point<float> by, juce::ModifierKeys mods = {})
{
    const auto held = mods.withFlags(juce::ModifierKeys::leftButtonModifier);
    c.mouseDown(mouseAt(c, at, at, held, 1, false));
    for (int i = 1; i <= 4; ++i) c.mouseDrag(mouseAt(c, at + by * (float(i) / 4.0f), at, held, 1, true));
    c.mouseUp(mouseAt(c, at + by, at, mods, 1, true));
}

void clickAt(juce::Component& c, juce::Point<float> at)
{
    const auto held = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    c.mouseDown(mouseAt(c, at, at, held, 1, false));
    c.mouseUp(mouseAt(c, at, at, {}, 1, false));
}

// Gestures the editor makes on the parameters, as the host sees them
struct GestureCounter final : juce::AudioProcessorListener {
    int begins = 0, ends = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++ends; }
};

int checkEditor(const juce::String& os, const juce::String& kitsPath)
{
    const auto settingsDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-plugin-check-editor");
    settingsDir.deleteRecursively();
    setenv("MD_SETTINGS_DIR", settingsDir.getFullPathName().toRawUTF8(), 1);
    const auto kitFolder = md::plugin::sharedKitFolder();
    const auto renders = [](juce::AudioProcessorEditor& e) {
        const auto image = e.createComponentSnapshot(e.getLocalBounds(), true, 1.0f);
        return image.isValid() && image.getWidth() == e.getWidth();
    };

    // Without an OS file: the editor opens, the SYN page says why it is empty, there is nothing to pick, the other
    // pages work; the kit folder is created
    {
        MdOneProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        auto* view = md::plugin::one::oneViewOf(editor.get());
        if (view == nullptr) { check(false, "the editor shows a OneView"); return 1; }
        check(!view->page(OneView::Syn).message().isEmpty() && view->machineBar().isVisible() && !view->machinePicker().canOpen(),
              "no OS file: the SYN page says so (\"" + view->page(OneView::Syn).message()[0] + "\"), no machine to pick");
        view->page(OneView::Effects).cell(0).setValue(20.0, juce::sendNotificationSync);
        check(p.trackParam(0, 8) == 20, "no OS file: AMD's knob still sets T1 AMD (" + juce::String(p.trackParam(0, 8)) + ")");
        check(kitFolder.isDirectory() && kitFolder.getParentDirectory() == settingsDir, "the kit folder is created beside the settings: " + kitFolder.getFullPathName());
        check(renders(*editor), "renders without an OS file");
        if (os.isNotEmpty() && juce::File(os).existsAsFile()) {   // the OS file dropped on the editor (no kit in it) becomes the OS file
            view->filesDropped({os}, 0, 0);
            check(p.engineReady() && p.firmwarePath() == os && !view->globalShown() && view->page(OneView::Syn).message().isEmpty(),
                  "the OS file dropped on the editor is selected as the OS file: " + p.statusText());
        }
    }
    if (os.isEmpty() || !juce::File(os).existsAsFile()) {
        std::printf("[SKIP] no Machinedrum OS file (set MD_OS_SYX or -DMD_OS_SYX=..., or pass --fw)\n");
        settingsDir.deleteRecursively();
        return g_failures ? 1 : 77;
    }
    md::plugin::saveSharedOsPath(os);
    MdOneProcessor p;
    if (!p.engineReady()) { check(false, "engine: " + p.statusText()); return 1; }
    GestureCounter gestures;
    p.addListener(&gestures);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto& view = *md::plugin::one::oneViewOf(editor.get());
    view.refresh();
    {   // MENU > Show Engine Status: the status lines get their own room under the strip; the editor follows at its scale
        const int viewH = view.getHeight(), editorH = editor->getHeight();
        const double scale = double(editor->getWidth()) / view.getWidth();
        view.setStatusShown(true);
        const bool grown = view.statusShown() && view.getHeight() == viewH + OneView::kStatusH
                        && std::abs(editor->getHeight() - (viewH + OneView::kStatusH) * scale) <= 1.0;
        view.setStatusShown(false);
        check(grown && view.getHeight() == viewH && editor->getHeight() == editorH,
              "the engine status shown adds " + juce::String(OneView::kStatusH) + " px to the view and the editor follows; hidden again, back to "
              + juce::String(editorH) + " px");
    }
    const auto labelOf = [&](int machine, int k) {
        for (const auto& m : p.machines()) if (m.id == machine) return juce::String(m.params[size_t(k)]);
        return juce::String("?");
    };
    const auto shortOf = [&](int machine) {
        for (const auto& m : p.machines()) if (m.id == machine) return juce::String(md::uispec::machineShortName(m.name));
        return juce::String("?");
    };

    // Track select: every page's cells on the track's parameters
    view.selectTrack(2);
    bool bound = true;
    for (int k = 0; k < 8; ++k) {
        bound = bound && view.page(OneView::Syn).parameter(k) == &p.parameter(2, params::kKit + k)
                      && view.page(OneView::Effects).parameter(k) == &p.parameter(2, params::kKit + 8 + k)
                      && view.page(OneView::Routing).parameter(k) == &p.parameter(2, params::kKit + 16 + k)
                      && view.page(OneView::Lfo).parameter(k) == &p.parameter(2, k < 5 ? params::kLfo + k : params::kKit + 21 + k - 5);
    }
    check(bound && view.selectedTrack() == 2 && view.trackStrip().selected() == 2 && !view.globalShown(),
          "track 3 selected: SYN, AMP/EFX, ROUTE and LFO cells on T3's parameters (LFO SPEED DEPTH SHMIX = ROUTE's LFOS LFOD LFOM)");
    bool namesOk = true;
    for (int t = 0; t < 16; ++t) namesOk = namesOk && view.trackStrip().machineName(t) == shortOf(p.machine(t));
    check(namesOk, "the strip names each track's machine: " + view.trackStrip().machineName(0) + " " + view.trackStrip().machineName(1) + " .. "
          + view.trackStrip().machineName(15));
    auto& effects = view.page(OneView::Effects);
    auto& amd = effects.cell(0);
    p.setTrackParam(2, 8, 10);   // as the host or automation sets it (message thread here)
    check(juce::roundToInt(amd.getValue()) == 10 && effects.cellText(0) == "10", "a host change of T3 AMD moves its knob (" + effects.cellText(0) + ")");

    // Knob interaction (Monomodule's): drag up or right turns it up, 160 px for the whole range; shift 8x finer; one
    // gesture per drag; the wheel; a click on the value types it; a double-click on the dial resets it
    const auto centre = amd.getLocalBounds().getCentre().toFloat();
    gestures.begins = gestures.ends = 0;
    dragCell(amd, centre, {0.0f, -20.0f});
    const int afterUp = p.trackParam(2, 8);
    dragCell(amd, centre, {0.0f, -40.0f}, juce::ModifierKeys::shiftModifier);
    const int afterFine = p.trackParam(2, 8);
    dragCell(amd, centre, {10.0f, 0.0f});
    const int afterRight = p.trackParam(2, 8);
    dragCell(amd, centre, {0.0f, 400.0f});
    check(afterUp == 26 && afterFine == 30 && afterRight == 38 && p.trackParam(2, 8) == 0,
          "drag: 20 px up +16 (" + juce::String(afterUp) + "), with shift 40 px +4 (" + juce::String(afterFine) + "), 10 px right +8 ("
          + juce::String(afterRight) + "), far down stops at 0 (" + juce::String(p.trackParam(2, 8)) + ")");
    check(gestures.begins == 4 && gestures.ends == 4, "a gesture per drag (" + juce::String(gestures.begins) + " begun, " + juce::String(gestures.ends) + " ended)");
    juce::MouseWheelDetails wheel{0.0f, 0.25f, false, false, false};
    amd.mouseWheelMove(mouseAt(amd, centre, centre, {}, 0, false), wheel);
    const int afterWheel = p.trackParam(2, 8);
    amd.mouseWheelMove(mouseAt(amd, centre, centre, juce::ModifierKeys::shiftModifier, 0, false), wheel);
    check(afterWheel > 0 && p.trackParam(2, 8) == afterWheel + 1, "wheel: up " + juce::String(afterWheel) + ", with shift one step (" + juce::String(p.trackParam(2, 8)) + ")");
    const auto valueRow = juce::Point<float>(centre.x, float(amd.getHeight() - md::plugin::one::kScale * 4));
    clickAt(amd, valueRow);
    const bool opened = effects.editing() == 0 && effects.valueEditor().isVisible();
    effects.valueEditor().setText("99", false);
    effects.endEdit(true);
    const int typed = p.trackParam(2, 8);
    effects.beginEdit(0);
    effects.valueEditor().setText("200", false);
    effects.endEdit(true);
    const int clamped = p.trackParam(2, 8);
    effects.beginEdit(3);   // EQG, bipolar: -10 is raw 54
    effects.valueEditor().setText("-10", false);
    effects.endEdit(true);
    effects.beginEdit(0);
    effects.valueEditor().setText("55", false);
    effects.endEdit(false);   // Escape: dropped
    check(opened && typed == 99 && clamped == 127 && p.trackParam(2, 11) == 54 && effects.cellText(3) == "-10" && p.trackParam(2, 8) == 127,
          "a click on the value opens the editor; typed 99 -> 99, 200 -> 127, EQG -10 -> raw 54 (shown " + effects.cellText(3) + "), Escape drops it");
    amd.mouseDoubleClick(mouseAt(amd, centre, centre, {}, 2, false));
    check(p.trackParam(2, 8) == 0, "a double-click on the dial resets AMD to its default (" + juce::String(p.trackParam(2, 8)) + ")");

    // The machine picker: a greyed machine cannot be picked; a pick sets the machine parameter, and the SYN page
    // relabels and shows the machine's defaults; the host changing the machine relabels it too
    auto& picker = view.machinePicker();
    view.openMachinePicker(false);
    const bool open = picker.isOpen() && picker.isVisible();
    const int before = p.machine(2);
    const bool romRefused = !picker.pick(128) && !picker.playable(96) && !picker.playable(80) && !picker.playable(160) && p.machine(2) == before;
    gestures.begins = gestures.ends = 0;
    const bool picked = picker.pick(33);   // EFM-SD
    view.refresh();
    auto& syn = view.page(OneView::Syn);
    bool relabelled = true, defaults = true;
    juce::StringArray shown;
    for (int k = 0; k < 8; ++k) {
        relabelled = relabelled && juce::String(syn.descriptor(k).label) == labelOf(33, k);
        defaults = defaults && juce::roundToInt(syn.cell(k).getValue()) == p.synDefault(33, k) && p.trackParam(2, k) == p.synDefault(33, k);
        shown.add(juce::String(syn.descriptor(k).label) + " " + syn.cellText(k));
    }
    check(open && romRefused, "the picker opens; ROM, MID, INP and RAM machines are greyed and cannot be picked");
    check(picked && p.machine(2) == 33 && !picker.isOpen() && gestures.begins == 1 && gestures.ends == 1 && view.trackStrip().machineName(2) == "SD",
          "picking EFM-SD sets T3 MACHINE to 33 in one gesture and closes the picker; the strip says " + view.trackStrip().machineName(2));
    check(relabelled && defaults && syn.badge().isEmpty(), "the SYN page relabels and shows EFM-SD's defaults: " + shown.joinIntoString(", "));
    auto& machine = p.parameter(2, params::kMachine);
    machine.setValueNotifyingHost(machine.convertTo0to1(49.0f));   // E12-SD, as a host sets it
    p.handleParameterChanges();
    view.refresh();
    check(juce::String(syn.descriptor(3).label) == labelOf(49, 3) && juce::String(syn.descriptor(7).label) == labelOf(49, 7) && view.trackStrip().machineName(2) == "SD",
          "the host sets E12-SD: the SYN page follows (" + juce::String(syn.descriptor(3).label) + ", " + juce::String(syn.descriptor(7).label) + ")");
    p.setMachine(2, 140);   // a kit may hold a ROM machine: shown, marked SILENT
    view.refresh();
    check(syn.badge() == "SILENT" && view.trackStrip().machineName(2) == "13", "a ROM machine (from a kit): the SYN page says SILENT, the strip " + view.trackStrip().machineName(2));
    p.setMachine(2, 33);

    // The LFO page: the destination track and parameter by name, the parameter's name following that track's machine
    auto& lfo = view.page(OneView::Lfo);
    p.setTrackLfo(2, 0, 4);
    p.setTrackLfo(2, 1, 3);   // SYN4
    p.setTrackLfo(2, 4, 1);
    view.refresh();
    const auto lfoBefore = lfo.cellText(1);
    const int destBefore = p.machine(4);
    p.setMachine(4, 64);   // P-I BD on the destination track
    view.refresh();
    const auto lfoAfter = lfo.cellText(1);
    p.setTrackLfo(2, 1, 17);
    view.refresh();
    check(lfo.cellText(0) == "05-" + shortOf(64) && lfoBefore == labelOf(destBefore, 3) && lfoAfter == labelOf(64, 3) && lfoBefore != lfoAfter
              && lfo.cellText(1) == "VOL" && lfo.cellText(4) == "TRIG",
          "LFO: TRACK " + lfo.cellText(0) + ", PARAM " + lfoBefore + " -> " + lfoAfter + " when track 5 becomes P-I BD, then " + lfo.cellText(1)
              + "; UPDTE " + lfo.cellText(4));

    // The selected track's key pressed again plays the track (the editor's keyboard); triggered tracks flash in the
    // strip, whatever played them (the timer polls the processor)
    auto& strip = view.trackStrip();
    view.selectTrack(6);
    const auto key7 = (strip.keyArea(6).getCentre() * md::plugin::one::kScale).toFloat();
    clickAt(strip, key7);
    pump(p, 4);
    view.refresh();
    const auto lit = strip.flashing();
    for (int i = 0; i < md::plugin::one::TrackStrip::kFlashTicks; ++i) view.refresh();
    check(view.selectedTrack() == 6 && lit == 1u << 6 && strip.flashing() == 0,
          "track 7's key pressed again plays it: the key flashes (" + juce::String::toHexString(int(lit)) + "), then goes dark");
    view.selectTrack(2);

    // GLOBAL: the LEV faders, TEMPO (whole BPM per drag step, tenths with shift, typed), CHAN, NOTES, ACCNT
    view.showGlobal();
    check(view.globalShown() && view.trackStrip().selected() == md::plugin::one::TrackStrip::kGlobal && view.page(OneView::Mixer).isVisible()
              && view.kitsPage().isVisible() && !view.page(OneView::Syn).isVisible(),
          "the GLOBAL tab: MIXER, GLOBAL and KITS shown, the track pages hidden");
    auto& mixer = view.page(OneView::Mixer);
    bool levelsBound = true;
    for (int t = 0; t < 16; ++t) levelsBound = levelsBound && mixer.parameter(t) == &p.parameter(t, params::kLevel);
    dragCell(mixer.cell(5), mixer.cell(5).getLocalBounds().getCentre().toFloat(), {0.0f, 20.0f});
    check(levelsBound && p.trackLevel(5) == 84 && mixer.cellText(5) == "84", "MIXER: 16 faders on the LEV parameters; T6 dragged down 20 px: 100 -> " + juce::String(p.trackLevel(5)));
    auto& global = view.page(OneView::Global);
    global.beginEdit(0);
    global.valueEditor().setText("133.5", false);
    global.endEdit(true);
    const double typedTempo = p.tempo();
    dragCell(global.cell(0), global.cell(0).getLocalBounds().getCentre().toFloat(), {0.0f, -20.0f});
    const double draggedTempo = p.tempo();
    dragCell(global.cell(0), global.cell(0).getLocalBounds().getCentre().toFloat(), {0.0f, -20.0f}, juce::ModifierKeys::shiftModifier);
    check(std::abs(typedTempo - 133.5) < 1e-3 && std::abs(draggedTempo - 149.0) < 1e-3 && std::abs(p.tempo() - 150.9) < 1e-3 && global.cellText(0) == "150.9",
          "TEMPO: typed 133.5 (" + juce::String(typedTempo, 2) + "), 20 px up +15 to a whole BPM: 149 (" + juce::String(draggedTempo, 2)
              + "), with shift +1.9 to a tenth: 150.9 (" + global.cellText(0) + ")");
    p.setMidiChannel(0);
    const auto omni = global.cellText(1);
    p.setMidiChannel(10);
    p.setNoteMap(MdOneProcessor::NoteMap::Chromatic);
    check(omni == "OMNI" && global.cellText(1) == "10" && global.cellText(2) == "CHROM" && global.parameter(3) == &p.accentParameter(),
          "CHAN " + omni + " / " + global.cellText(1) + ", NOTES " + global.cellText(2) + ", ACCNT on the accent");
    p.setNoteMap(MdOneProcessor::NoteMap::WhiteKeys);

    // The editor's timer beside the audio thread: processBlock skips any host block whose engine lock it cannot take,
    // so nothing the timer polls (the status, the parameters, the triggers, the kit) may hold it. The timer runs flat
    // out on this (the message) thread while another thread plays 4000 blocks of 64 samples: every one renders.
    {
        p.setRateAndBufferSizeDetails(44100.0, 64);
        p.prepareToPlay(44100.0, 64);
        const auto before = p.engineBlocks();
        constexpr int kBlocks = 4000;   // 2 engine blocks each
        std::atomic<bool> done{false};
        std::thread audio([&] {
            juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), 64);
            juce::MidiBuffer midi;
            for (int b = 0; b < kBlocks; ++b) p.processBlock(buf, midi);
            done = true;
        });
        int ticks = 0;
        while (!done) { view.refresh(); ++ticks; }
        audio.join();
        const auto rendered = p.engineBlocks() - before;
        check(rendered == uint64_t(kBlocks) * 2, "the editor's timer, " + juce::String(ticks) + " ticks during " + juce::String(kBlocks)
              + " host blocks on another thread: " + juce::String(rendered) + " engine blocks of " + juce::String(kBlocks * 2) + " (none skipped)");
    }

    // The kit folder and browser: a dropped file without kits is refused; the user's snapshot dropped is copied in and
    // lists its 16 used kits only; a click on kit 4 loads it; the same file dropped again is not copied twice; the
    // folder is rescanned when files change; EXPORT writes the kit with the parameters' values
    auto& lib = view.kitLibrary();
    auto& kitsPage = view.kitsPage();
    view.filesDropped({os}, 10, 10);
    check(lib.banks().empty() && kitFolder.findChildFiles(juce::File::findFiles, false).isEmpty(), "the OS file dropped: refused, nothing copied");
    const auto kitsFile = kitsFileOf(kitsPath);
    if (kitsFile.isEmpty() || !juce::File(kitsFile).existsAsFile()) {
        std::printf("[SKIP] kits: no kit snapshot (set MD_KITS_SYX to a Machinedrum .syx with kits, or pass --kits)\n");
    } else {
        const auto bank = md::library::loadSyxFile(kitsFile.toStdString());
        std::vector<const md::library::Kit*> used;
        for (const auto& k : bank.kits) if (!k.isFree()) used.push_back(&k);
        view.selectTrack(0);
        view.filesDropped({kitsFile}, 10, 10);
        view.refresh();
        bool sameKits = lib.banks().size() == 1 && lib.banks()[0].kits.size() == used.size();
        for (size_t i = 0; sameKits && i < used.size(); ++i) sameKits = lib.banks()[0].kits[i].name == used[i]->name;
        check(sameKits && used.size() == 16 && view.globalShown() && kitsPage.browsedBank() == 0 && lib.banks()[0].name == juce::String(bank.name),
              "the kit snapshot dropped: copied into the kit folder, bank " + lib.banks()[0].name + " lists its " + juce::String(int(used.size())) + " used kits of "
                  + juce::String(int(bank.kits.size())) + " (" + juce::String(lib.banks()[0].kits[0].name) + " .. " + juce::String(lib.banks()[0].kits.back().name) + ")");
        check(!kitsPage.kitArea(15).isEmpty() && kitsPage.kitArea(16).isEmpty(), "the browser shows the 16 kits in its two columns of eight, nothing after them");
        const auto at = (kitsPage.kitArea(4).getCentre() * md::plugin::one::kScale).toFloat();
        clickAt(kitsPage, at);
        bool machinesOk = true;
        juce::StringArray names;
        for (int t = 0; t < 16; ++t) {
            machinesOk = machinesOk && p.machine(t) == used[4]->machine[size_t(t)];
            names.add(p.machineName(p.machine(t)));
        }
        view.refresh();
        check(machinesOk && p.kitName() == juce::String(used[4]->name) && p.kitBank() == juce::String(bank.name) && kitsPage.loadedKit() == 4,
              "a click on kit 4 loads " + p.kitName() + " / " + p.kitBank() + ": " + names.joinIntoString(" "));
        check(view.trackStrip().machineName(0) == shortOf(used[4]->machine[0]) && view.kitBox().isVisible(), "the strip and the header follow the kit");
        juce::String message;
        const bool again = lib.import(juce::File(kitsFile), message);
        check(again && lib.banks().size() == 1 && kitFolder.findChildFiles(juce::File::findFiles, false, "*.syx").size() == 1,
              "the same file dropped again is not copied twice (\"" + message + "\")");
        const auto exported = kitFolder.getChildFile("EXPORTED.syx");
        const bool written = view.exportKit(exported);
        const bool rescanned = lib.refresh();
        md::library::Kit back;
        juce::MemoryBlock bytes;
        exported.loadFileAsData(bytes);
        const bool decoded = md::library::decodeKit(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize(), back);
        check(written && rescanned && lib.banks().size() == 2 && lib.indexOf("EXPORTED") >= 0 && decoded && back.name == used[4]->name
                  && back.machine == used[4]->machine && md::library::encodeKit(back) == md::library::encodeKit(p.parametersToKit()),
              "EXPORT writes the kit as a dump (decodes to " + juce::String(back.name) + "); the folder is rescanned: 2 banks");
        check(!lib.refresh(), "nothing changed: no rescan");
        const auto other = md::library::encodeKit(*used[1]);
        exported.replaceWithData(other.data(), other.size());
        exported.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
        const bool changed = lib.refresh();
        const int e = lib.indexOf("EXPORTED");
        check(changed && e >= 0 && lib.banks()[size_t(e)].kits.size() == 1 && lib.banks()[size_t(e)].kits[0].name == used[1]->name,
              "a file rewritten in the folder is read again (" + (e >= 0 ? juce::String(lib.banks()[size_t(e)].kits[0].name) : juce::String("-")) + ")");
        kitsPage.update();
        clickAt(kitsPage, (kitsPage.bankArea(e).getCentre() * md::plugin::one::kScale).toFloat());
        check(kitsPage.browsedBank() == e && kitsPage.loadedKit() == -1, "a click on a bank browses it");
    }

    // The MASTER tab (.clew increment 0009): the strip's MASTER key shows the master effects' four pages in the hardware's
    // order with the hardware's labels, each cell on its master byte (a drag turns that byte and no other one); a host
    // change moves a knob; the GLOBAL page's MASTER switch turns master_fx, and the master pages say OFF while it is off
    {
        using md::plugin::one::TrackStrip;
        constexpr int kS = md::plugin::one::kScale;
        view.selectTrack(3);
        clickAt(strip, (strip.keyArea(TrackStrip::kMaster).getCentre() * kS).toFloat());
        bool shown = view.masterShown() && !view.globalShown() && strip.selected() == TrackStrip::kMaster && !view.page(OneView::Syn).isVisible()
                  && !view.page(OneView::Mixer).isVisible() && !view.machineBar().isVisible();
        for (auto pg : OneView::kMasterPages) shown = shown && view.page(pg).isVisible();
        static const char* const kTitles[4] = {"ECHO", "REV", "EQ", "DYN"};
        static const char* const kLabels[4][8] = {{"TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV"},
                                                  {"DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"},
                                                  {"LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"},
                                                  {"ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"}};
        static constexpr int kBlockOfPage[4] = {1, 0, 2, 3};   // the pages' blocks in the kit's order (reverb, echo, EQ, dynamix)
        bool labels = true, bound2 = true, turned = true;
        int cells = 0;
        juce::StringArray moves;
        for (int b = 0; b < 4; ++b) {
            auto& pg = view.page(OneView::kMasterPages[b]);
            labels = labels && pg.title() == kTitles[b];
            for (int k = 0; k < 8; ++k) {
                const int i = kBlockOfPage[b] * 8 + k;
                labels = labels && juce::String(pg.descriptor(k).label) == kLabels[b][k];
                bound2 = bound2 && pg.parameter(k) == &p.masterParameter(i);
                std::array<int, 32> before{};
                for (int j = 0; j < 32; ++j) before[size_t(j)] = p.masterFx(j);
                const int v0 = before[size_t(i)], dir = v0 < 64 ? 1 : -1;   // 20 px up (+16) from the lower half, down (-16) from the upper
                dragCell(pg.cell(k), pg.cell(k).getLocalBounds().getCentre().toFloat(), {0.0f, -20.0f * float(dir)});
                bool only = p.masterFx(i) == v0 + 16 * dir;
                for (int j = 0; j < 32; ++j) only = only && (j == i || p.masterFx(j) == before[size_t(j)]);
                turned = turned && only;
                cells += only ? 1 : 0;
                if (k == 0) moves.add(juce::String(kTitles[b]) + " " + kLabels[b][k] + " " + juce::String(v0) + " -> " + juce::String(p.masterFx(i)));
            }
        }
        check(shown && labels && bound2, "the MASTER key shows ECHO | REV over EQ | DYN, the hardware's labels (TIME .. LEV, DVOL .. LEV, LF .. GAIN, ATCK .. MIX), "
              "each cell on its master byte (ECHO on fx_echo_*, REV on fx_rev_*, ...)");
        check(turned, "every master cell turns its byte and no other (" + juce::String(cells) + " of 32): " + moves.joinIntoString(", "));
        auto& eq = view.page(OneView::Eq);
        p.setMasterFx(17, 64);
        p.setMasterFx(21, 70);
        p.setMasterFx(23, 100);
        check(eq.cellText(1) == "0" && eq.cellText(5) == "+6" && eq.cellText(7) == "100" && juce::roundToInt(eq.cell(7).getValue()) == 100,
              "a host change moves the knobs; the EQ's gains are shown around their flat 64: LG " + eq.cellText(1) + ", PG " + eq.cellText(5) + ", GAIN " + eq.cellText(7));

        auto& global = view.page(OneView::Global);
        auto& sw = global.cell(OneView::kMasterCell);
        p.setMasterOn(false);
        view.refresh();
        bool badges = true;
        for (auto pg : OneView::kMasterPages) badges = badges && view.page(pg).badge() == "OFF";
        view.showGlobal();
        const auto offText = global.cellText(OneView::kMasterCell);
        gestures.begins = gestures.ends = 0;
        dragCell(sw, sw.getLocalBounds().getCentre().toFloat(), {0.0f, -30.0f});
        const bool onAgain = p.masterOn() && global.cellText(OneView::kMasterCell) == "ON" && gestures.begins == 1 && gestures.ends == 1;
        view.refresh();
        bool cleared = true;
        for (auto pg : OneView::kMasterPages) cleared = cleared && view.page(pg).badge().isEmpty();
        dragCell(sw, sw.getLocalBounds().getCentre().toFloat(), {0.0f, 30.0f});
        const bool offAgain = !p.masterOn() && p.getLatencySamples() == 0;
        const int offLatency = p.getLatencySamples();
        sw.mouseDoubleClick(mouseAt(sw, sw.getLocalBounds().getCentre().toFloat(), sw.getLocalBounds().getCentre().toFloat(), {}, 2, false));
        check(global.parameter(OneView::kMasterCell) == &p.masterOnParameter() && juce::String(global.descriptor(OneView::kMasterCell).label) == "MASTER"
                  && offText == "OFF" && badges && onAgain && cleared && offAgain && p.masterOn() && p.getLatencySamples() == MdOneProcessor::kMasterLatency,
              "GLOBAL's MASTER switch: OFF while master_fx is off (the master pages say OFF), dragged up: ON in one gesture (the badges go), down: off (latency "
              + juce::String(offLatency) + "), a double-click: back to its default, on (latency " + juce::String(p.getLatencySamples()) + ")");
        clickAt(strip, (strip.keyArea(3).getCentre() * kS).toFloat());
        check(view.selectedTrack() == 3 && !view.masterShown() && view.page(OneView::Syn).isVisible() && !view.page(OneView::Echo).isVisible(),
              "a track's key leaves the MASTER tab for that track's pages");
    }

    // Every page renders: the track pages, the picker open, the GLOBAL tab, the MASTER tab
    view.selectTrack(1);
    const bool trackRenders = renders(*editor);
    view.openMachinePicker(false);
    const bool pickerRenders = renders(*editor);
    view.showGlobal();
    const bool globalRenders = renders(*editor);
    view.showMaster();
    check(trackRenders && pickerRenders && globalRenders && renders(*editor), "every page renders (a track, the picker, GLOBAL, MASTER)");
    p.removeListener(&gestures);
    editor.reset();
    settingsDir.deleteRecursively();
    return g_failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::String mode, os, outDir, kits;
    double benchSeconds = 20.0;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        if (a == "--fw" && i + 1 < argc) os = argv[++i];
        else if (a == "--kits" && i + 1 < argc) kits = argv[++i];
        else if (a == "--snapshot" && i + 1 < argc) { mode = a; outDir = argv[++i]; }
        else if (mode == "--bench" && a.containsOnly("0123456789.")) benchSeconds = a.getDoubleValue();
        else mode = a;
    }
    if (os.isEmpty()) if (const char* env = std::getenv("MD_OS_SYX"); env && *env) os = env;
    if (os.isEmpty()) os = MD_OS_SYX;
    if (mode == "--check-os-load") return checkOsLoad(os);
    if (mode == "--check-render") return checkRender(os);
    if (mode == "--check-state") return checkState(os, kits);
    if (mode == "--check-master") return checkMaster(os, kits);
    if (mode == "--bench") return bench(os, std::max(1.0, benchSeconds));
    if (mode == "--check-rt") return checkRt(os);
    if (mode == "--check-editor") return checkEditor(os, kits);
    if (mode == "--snapshot") return snapshot(juce::File::getCurrentWorkingDirectory().getChildFile(outDir), os, kits);
    std::fprintf(stderr, "usage: md-plugin-check --check-os-load [--fw os.syx]\n"
                         "       md-plugin-check --check-render [--fw os.syx]\n"
                         "       md-plugin-check --check-state [--fw os.syx] [--kits kits.syx]\n"
                         "       md-plugin-check --check-master [--fw os.syx] [--kits kits.syx]\n"
                         "       md-plugin-check --check-editor [--fw os.syx] [--kits kits.syx]\n"
                         "       md-plugin-check --bench [seconds] [--fw os.syx]\n"
                         "       md-plugin-check --check-rt [--fw os.syx]\n"
                         "       md-plugin-check --snapshot <dir> [--fw os.syx] [--kits kits.syx]\n");
    return 2;
}
