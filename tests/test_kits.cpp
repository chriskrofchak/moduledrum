// Kit files: the 7-bit packing and the kit dump layout on a synthetic kit (no user files needed), parsing the user's
// 64-kit snapshot (MD_KITS_SYX), and loading its first kit into the engine (also needs the OS file, MD_OS_SYX).
#include "mini_test.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "Firmware.h"
#include "MachineRunner.h"
#include "MdEngine.h"
#include "golden/GoldenPattern.h"
#include "md/library/KitApply.h"
#include "md/library/MdSysex.h"
using namespace md;

namespace {
// The hash of the golden pattern's triggers on the first kit of the user's snapshot (OS 1.63, interpreter, no ROM
// samples): the kit's machines, parameters, levels and LFOs, then golden::Pattern's random triggers for 12 s.
constexpr uint64_t kKit0Hash = 0xf5c618b7df5550faull;

std::string kitsFile()
{
    const char* env = std::getenv("MD_KITS_SYX");
    const std::string p = env ? env : "";
    if (p.empty() || !std::filesystem::exists(p)) throw mt::Skip{"no kit snapshot (set MD_KITS_SYX to a Machinedrum .syx with kits)"};
    return p;
}

std::string hex(uint64_t v) { std::ostringstream o; o << std::hex << std::setw(16) << std::setfill('0') << v; return o.str(); }

// A kit whose every field has a distinct, checkable value; some bytes have bit 7 set so the 7-bit packing matters
library::Kit syntheticKit()
{
    static constexpr uint8_t ids[16] = {16, 176, 130, 140, 0, 127, 128, 159, 191, 64, 96, 112, 1, 2, 3, 80};
    library::Kit k;
    k.version = 4;
    k.revision = 1;
    k.position = 37;
    k.setName("SYNTH KIT 7");
    for (int t = 0; t < 16; ++t) {
        k.machine[size_t(t)] = ids[t];
        for (int p = 0; p < 24; ++p) k.params[size_t(t)][size_t(p)] = uint8_t((t * 37 + p * 5 + 11) & 0x7f);
        k.level[size_t(t)] = uint8_t((t * 9 + 3) & 0x7f);
        auto& l = k.lfo[size_t(t)];
        l.destTrack = uint8_t((t * 5 + 1) & 15);
        l.destParam = uint8_t((t * 7) % 24);
        l.shape1 = uint8_t(t % 6);
        l.shape2 = uint8_t((t + 2) % 6);
        l.type = uint8_t(t % 3);
        for (size_t i = 0; i < l.state.size(); ++i) l.state[i] = uint8_t(t * 31 + int(i) * 17 + ((i & 1) ? 0x80 : 0));
        k.trigGroup[size_t(t)] = (t % 3 == 0) ? 0xff : uint8_t(15 - t);
        k.muteGroup[size_t(t)] = (t % 4 == 1) ? uint8_t(t + 1) : 0xff;
    }
    for (size_t i = 0; i < 8; ++i) {
        k.masterFx.reverb[i] = uint8_t(1 + i);
        k.masterFx.delay[i] = uint8_t(20 + i);
        k.masterFx.eq[i] = uint8_t(40 + i);
        k.masterFx.dynamics[i] = uint8_t(120 + i);
    }
    return k;
}

std::vector<uint8_t> message(uint8_t type, size_t payload)
{
    std::vector<uint8_t> m = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, type};
    for (size_t i = 0; i < payload; ++i) m.push_back(uint8_t(i * 5 % 128));
    m.push_back(0xf7);
    return m;
}

void append(std::vector<uint8_t>& to, const std::vector<uint8_t>& from) { to.insert(to.end(), from.begin(), from.end()); }

// "TRX-BD" for an id, from the OS when there is one
struct MachineNames {
    std::unique_ptr<engine::MachineRunner> runner;
    MachineNames()
    {
        const auto os = mt::osFile();
        if (os.empty()) return;
        auto c = fw::parseContainer(fw::parseSysex(fw::readFile(os)));
        runner = std::make_unique<engine::MachineRunner>(std::move(c.sections.at(0).data));
    }
    std::string operator()(int id) const
    {
        const auto* m = runner ? runner->machine(uint8_t(id)) : nullptr;
        return m ? m->name : "#" + std::to_string(id);
    }
};
}

TEST_CASE(kits_pack7_known_vectors)
{
    // the group header holds the top bits of the 7 bytes that follow it, the first byte's in bit 6
    const std::vector<uint8_t> plain = {0x80, 0x01, 0xff, 0x7f, 0x00, 0x81, 0x40};
    const std::vector<uint8_t> packed = {0x52, 0x00, 0x01, 0x7f, 0x7f, 0x00, 0x01, 0x40};
    CHECK(library::encode7(plain.data(), plain.size()) == packed);
    CHECK(library::decode7(packed.data(), packed.size()) == plain);
    // a short last group: header and one byte
    const std::vector<uint8_t> one = {0xff};
    const std::vector<uint8_t> onePacked = {0x40, 0x7f};
    CHECK(library::encode7(one.data(), one.size()) == onePacked);
    CHECK(library::decode7(onePacked.data(), onePacked.size()) == one);
    // the machine words of a real kit dump: 16, 17, 18 as 32-bit big-endian words, packed
    const std::vector<uint8_t> words = {0, 0, 0, 0x10, 0, 0, 0, 0x11, 0, 0, 0, 0x12};
    const std::vector<uint8_t> wordsPacked = {0, 0, 0, 0, 0x10, 0, 0, 0, /**/ 0, 0x11, 0, 0, 0, 0x12};
    CHECK(library::decode7(wordsPacked.data(), wordsPacked.size()) == words);
    // the sizes the kit dump relies on
    CHECK_EQ(library::packed7Size(64), size_t(74));
    CHECK_EQ(library::packed7Size(576), size_t(659));
    CHECK_EQ(library::packed7Size(32), size_t(37));
    CHECK_EQ(library::packed7Size(0), size_t(0));
    // any bytes survive the round trip, at any length
    unsigned s = 12345;
    for (size_t n = 0; n < 100; ++n) {
        std::vector<uint8_t> data(n);
        for (auto& b : data) { s = s * 1664525u + 1013904223u; b = uint8_t(s >> 24); }
        const auto p = library::encode7(data.data(), data.size());
        CHECK_EQ(p.size(), library::packed7Size(n));
        for (auto b : p) CHECK(b < 0x80);
        CHECK(library::decode7(p.data(), p.size()) == data);
    }
}

TEST_CASE(kits_synthetic_layout)
{
    const library::Kit k = syntheticKit();
    const auto m = library::encodeKit(k);
    CHECK_EQ(m.size(), size_t(1233));
    const uint8_t header[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x52, 4, 1, 37};
    CHECK(std::equal(std::begin(header), std::end(header), m.begin()));
    CHECK(std::string(m.begin() + 0x0a, m.begin() + 0x0a + 11) == "SYNTH KIT 7");
    CHECK_EQ(int(m[0x0a + 11]), 0);
    // plain fields
    for (int t = 0; t < 16; ++t) {
        for (int p = 0; p < 24; ++p) CHECK_EQ(int(m[size_t(0x1a + t * 24 + p)]), int(k.params[size_t(t)][size_t(p)]));
        CHECK_EQ(int(m[size_t(0x19a + t)]), int(k.level[size_t(t)]));
    }
    for (size_t i = 0; i < 8; ++i) {
        CHECK_EQ(int(m[0x487 + i]), int(k.masterFx.reverb[i]));
        CHECK_EQ(int(m[0x487 + 8 + i]), int(k.masterFx.delay[i]));
        CHECK_EQ(int(m[0x487 + 16 + i]), int(k.masterFx.eq[i]));
        CHECK_EQ(int(m[0x487 + 24 + i]), int(k.masterFx.dynamics[i]));
    }
    // 7-bit packed machine words: track 1 = 176 is byte 7 of the stream, the first data byte of the second group
    // (header at 0x1b2, bit 6); track 2 = 130 is byte 11, its fifth (bit 2)
    CHECK_EQ(int(m[0x1b2]), 0x44);
    CHECK_EQ(int(m[0x1b3]), 0x30);
    CHECK_EQ(int(m[0x1b2 + 5]), 0x02);
    // the first LFO: state byte 1 has bit 7 set and is the stream's seventh byte, the last of the first group
    CHECK_EQ(int(m[0x1f4]), 0x01);
    CHECK_EQ(int(m[0x1f4 + 7]), 0x11);
    // checksum over the bytes from the kit slot, length of the message after the 10-byte header, F7
    unsigned sum = 0;
    for (size_t i = 9; i < 0x4cc; ++i) sum += m[i];
    sum &= 0x3fff;
    CHECK_EQ(int(m[0x4cc]), int(sum >> 7));
    CHECK_EQ(int(m[0x4cd]), int(sum & 0x7f));
    CHECK_EQ(int(m[0x4ce]), 1223 >> 7);
    CHECK_EQ(int(m[0x4cf]), 1223 & 0x7f);
    CHECK_EQ(int(m[0x4d0]), 0xf7);
    for (size_t i = 1; i + 1 < m.size(); ++i) CHECK(m[i] < 0x80);
}

TEST_CASE(kits_synthetic_roundtrip)
{
    const library::Kit k = syntheticKit();
    const auto m = library::encodeKit(k);
    library::Kit d;
    std::string error;
    CHECK_MSG(library::decodeKit(m.data(), m.size(), d, &error), error);
    CHECK(d.checksumOk);
    CHECK_EQ(d.name, std::string("SYNTH KIT 7"));
    CHECK_EQ(int(d.position), 37);
    CHECK_EQ(int(d.version), 4);
    CHECK(!d.isFree());
    for (size_t t = 0; t < 16; ++t) {
        CHECK_EQ(int(d.machine[t]), int(k.machine[t]));
        CHECK_EQ(uint64_t(d.model[t]), uint64_t(k.machine[t]));
        CHECK(d.params[t] == k.params[t]);
        CHECK_EQ(int(d.level[t]), int(k.level[t]));
        CHECK_EQ(int(d.lfo[t].destTrack), int(k.lfo[t].destTrack));
        CHECK_EQ(int(d.lfo[t].destParam), int(k.lfo[t].destParam));
        CHECK_EQ(int(d.lfo[t].shape1), int(k.lfo[t].shape1));
        CHECK_EQ(int(d.lfo[t].shape2), int(k.lfo[t].shape2));
        CHECK_EQ(int(d.lfo[t].type), int(k.lfo[t].type));
        CHECK(d.lfo[t].state == k.lfo[t].state);
        CHECK_EQ(int(d.trigGroup[t]), int(k.trigGroup[t]));
        CHECK_EQ(int(d.muteGroup[t]), int(k.muteGroup[t]));
    }
    CHECK(d.masterFx.reverb == k.masterFx.reverb);
    CHECK(d.masterFx.delay == k.masterFx.delay);
    CHECK(d.masterFx.eq == k.masterFx.eq);
    CHECK(d.masterFx.dynamics == k.masterFx.dynamics);
    CHECK(library::encodeKit(d) == m);

    // a changed byte: the kit still decodes (the MD would refuse it, a file from a tool may be sloppy) but is flagged
    auto bad = m;
    bad[0x1a + 5] ^= 1;
    library::Kit b;
    CHECK(library::decodeKit(bad.data(), bad.size(), b));
    CHECK(!b.checksumOk);

    // what is not a kit dump
    auto truncated = m;
    truncated.pop_back();
    CHECK(!library::decodeKit(truncated.data(), truncated.size(), b, &error));
    auto pattern = m;
    pattern[6] = 0x67;
    CHECK(!library::decodeKit(pattern.data(), pattern.size(), b, &error));
    auto highBit = m;
    highBit[0x100] = 0x80;
    CHECK(!library::decodeKit(highBit.data(), highBit.size(), b, &error));
    auto otherDevice = m;
    otherDevice[4] = 0x03;
    CHECK(!library::decodeKit(otherDevice.data(), otherDevice.size(), b, &error));
}

TEST_CASE(kits_free_slot_and_names)
{
    library::Kit k = syntheticKit();
    k.setName("");
    CHECK(k.isFree());
    CHECK_EQ(k.name, std::string());
    k.rawName = {0x7f, 'O', 'M', 'B', 'I', ' ', 'B', 'I', 'T', 'T', 0, 0x7f, 0x17, 0x18, 0x7f, 0x19};
    k.name = "";
    CHECK(k.isFree());
    // a name ends at the first NUL; what is left behind it in the buffer is not part of it
    k.setName("TRX");
    k.rawName[4] = 'M';
    k.rawName[5] = 'D';
    CHECK(!k.isFree());
    const auto m = library::encodeKit(k);
    library::Kit d;
    CHECK(library::decodeKit(m.data(), m.size(), d));
    CHECK_EQ(d.name, std::string("TRX"));
    CHECK_EQ(int(d.rawName[4]), int('M'));
}

TEST_CASE(kits_mixed_stream_skips_other_messages)
{
    library::Kit k = syntheticKit();
    std::vector<uint8_t> stream = {0x00, 0x13, 0xfe};		// junk outside any message
    append(stream, library::encodeKit(k));
    append(stream, message(0x67, 300));						// pattern
    append(stream, message(0x67, 300));
    append(stream, message(0x69, 30));						// song
    append(stream, message(0x50, 150));						// global
    append(stream, message(0x54, 0));						// a Machinedrum message of a type nobody reads
    append(stream, {0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7});	// a universal sysex (identity request)
    k.position = 38;
    k.setName("");
    append(stream, library::encodeKit(k));					// a free slot is still a kit
    append(stream, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x52, 4, 1, 3, 1, 2, 3});	// cut short by the end of the stream
    const auto bank = library::parseSyx(stream);
    CHECK_EQ(bank.kits.size(), size_t(2));
    CHECK_EQ(bank.usedKits(), 1);
    CHECK_EQ(bank.kits[0].name, std::string("SYNTH KIT 7"));
    CHECK_EQ(int(bank.kits[1].position), 38);
    CHECK_EQ(bank.patterns, 2);
    CHECK_EQ(bank.songs, 1);
    CHECK_EQ(bank.globals, 1);
    CHECK_EQ(bank.otherMachinedrum, 1);
    CHECK_EQ(bank.foreign, 1);
    CHECK_EQ(bank.rejected, 1);
    CHECK_EQ(bank.errors.size(), size_t(1));
    CHECK_EQ(bank.badChecksums, 0);

    // a kit message of the wrong size is rejected, not skipped silently
    auto wrongSize = library::encodeKit(k);
    wrongSize.erase(wrongSize.begin() + 0x100);
    const auto bank2 = library::parseSyx(wrongSize);
    CHECK_EQ(bank2.kits.size(), size_t(0));
    CHECK_EQ(bank2.rejected, 1);

    // an empty file and a file without sysex are just empty
    CHECK_EQ(library::parseSyx(std::vector<uint8_t>{}).kits.size(), size_t(0));
    CHECK_EQ(library::parseSyx(std::vector<uint8_t>{'h', 'i'}).rejected, 0);
}

TEST_CASE(kits_user_snapshot_parses)
{
    const auto path = kitsFile();
    const auto bank = library::loadSyxFile(path);
    CHECK_EQ(bank.kits.size(), size_t(64));
    CHECK_EQ(bank.patterns, 128);
    CHECK_EQ(bank.songs, 32);
    CHECK_EQ(bank.globals, 8);
    CHECK_EQ(bank.rejected, 0);
    CHECK_EQ(bank.badChecksums, 0);
    CHECK_EQ(bank.otherMachinedrum, 0);
    CHECK_EQ(bank.foreign, 0);
    CHECK(bank.errors.empty());

    int named = 0;
    for (size_t i = 0; i < bank.kits.size(); ++i) {
        const auto& k = bank.kits[i];
        CHECK_EQ(int(k.position), int(i));
        CHECK(k.checksumOk);
        for (size_t t = 0; t < 16; ++t) {
            CHECK_MSG(k.machine[t] <= library::kMaxMachineId, "kit " << i << " track " << t << " machine " << int(k.machine[t]));
            CHECK_EQ(uint64_t(k.model[t]), uint64_t(k.machine[t]));
            for (auto v : k.params[t]) CHECK(v < 128);
            CHECK(k.level[t] < 128);
            CHECK_MSG(k.lfo[t].destTrack < 16 && k.lfo[t].destParam < 24 && k.lfo[t].shape1 < 6 && k.lfo[t].shape2 < 6 && k.lfo[t].type < 3,
                "kit " << i << " lfo " << t);
        }
        if (k.isFree()) {
            // a free slot is an empty kit: every track on GND-EMPTY
            CHECK(std::all_of(k.machine.begin(), k.machine.end(), [](uint8_t id) { return id == 0; }));
            CHECK(k.name.empty());
        } else {
            CHECK_MSG(!k.name.empty(), "kit " << i);
            ++named;
        }
    }
    CHECK_EQ(named, bank.usedKits());
    CHECK(named >= 16);

    // a few known values of this file: the first kit is "TRX", with TRX-BD (id 16) on track 1 and 127 on its LEV
    const auto& k0 = bank.kits[0];
    CHECK_EQ(k0.name, std::string("TRX"));
    CHECK_EQ(int(k0.machine[0]), 16);
    CHECK_EQ(int(k0.machine[15]), 0x1c);
    CHECK_EQ(int(k0.level[0]), 127);
    CHECK_EQ(int(k0.params[0][17]), 103);					// VOL
    for (size_t t = 0; t < 16; ++t) CHECK_EQ(int(k0.lfo[t].destTrack), int(t));	// every LFO on its own track
    CHECK_EQ(bank.kits[1].name, std::string("EFM"));
    CHECK_EQ(bank.kits[4].name, std::string("FRANSISCO"));

    // the first three kits, for a human to read
    const MachineNames name;
    for (size_t i = 0; i < 3; ++i) {
        std::printf("kit %zu \"%s\":", i, bank.kits[i].name.c_str());
        for (auto id : bank.kits[i].machine) std::printf(" %s", name(id).c_str());
        std::printf("\n");
    }
}

TEST_CASE(kits_user_snapshot_reencodes_byte_for_byte)
{
    // decode + encode gives back every kit message of the file: no byte of the dump is unaccounted for
    const auto path = kitsFile();
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int kits = 0;
    for (const auto& m : library::splitMessages(data.data(), data.size())) {
        CHECK(m.complete);
        if (!library::isMachinedrumMessage(&data[m.offset], m.size) || data[m.offset + 6] != 0x52) continue;
        library::Kit k;
        std::string error;
        CHECK_MSG(library::decodeKit(&data[m.offset], m.size, k, &error), error);
        const auto again = library::encodeKit(k);
        CHECK_MSG(again.size() == m.size && std::equal(again.begin(), again.end(), data.begin() + std::ptrdiff_t(m.offset)),
            "kit " << kits << " (" << k.name << ") does not re-encode to the same bytes");
        ++kits;
    }
    CHECK_EQ(kits, 64);
}

namespace {
struct Rendered { uint64_t hash; int32_t peak; };

// The kit applied to a fresh engine, then golden::Pattern's triggers (four random tracks every 300 blocks, 12 s) on the
// kit instead of the pattern's own machines. (One DSP instance: with the voices split over two ParallelEngine instances this
// kit renders differently, from block 301 on in track 12 (0-based, a TRX-MA) -- probably DSP state the voices of an instance
// share, such as a noise source; not investigated -- whereas golden::Pattern's own machines render the same for any split.)
Rendered renderKit(engine::Engine& eng, const library::Kit& kit)
{
    auto& host = eng.host();
    library::applyKit(host, kit);
    // everything the kit holds is in the host model (the SYN values too, over the machines' defaults)
    for (int t = 0; t < 16; ++t)
        for (int p = 0; p < 24; ++p)
            CHECK_MSG(host.param(t, p) == kit.params[size_t(t)][size_t(p)], "track " << t << " param " << p);
    golden::Pattern pattern;
    engine::Engine::Output out;
    int32_t peak = 0;
    for (int b = 0; b < golden::kBlocks; ++b) {
        pattern.beforeBlock(host, b);
        CHECK(eng.render(out));
        pattern.hashBlock(out.mix);
        for (const auto& frame : out.mix.main)
            for (const int32_t s : frame) peak = std::max(peak, std::abs(s));
    }
    CHECK_MSG(eng.fault().empty(), "DSP fault: " << eng.fault());
    return {pattern.hash(), peak};
}

struct Os {
    fw::Firmware firmware;
    std::vector<uint8_t> image;   // section 0, the ColdFire OS
};
Os loadOs()
{
    const auto path = mt::requireOsFile();
    return {fw::loadFirmware(path), fw::parseContainer(fw::parseSysex(fw::readFile(path))).sections.at(0).data};
}
}

TEST_CASE(kits_apply_and_render_pattern)
{
    const auto os = loadOs();
    const auto bank = library::loadSyxFile(kitsFile());
    CHECK(!bank.kits.empty());
    engine::Engine eng(os.firmware, os.image);
    const auto r = renderKit(eng, bank.kits[0]);
    CHECK_MSG(r.peak > 0, "the kit rendered silence");
    std::printf("kit 0 golden-trigger hash %s, peak %d\n", hex(r.hash).c_str(), int(r.peak));
    CHECK_MSG(r.hash == kKit0Hash, "hash " << hex(r.hash) << ", expected " << hex(kKit0Hash));
}
