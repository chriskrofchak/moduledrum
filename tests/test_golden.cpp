// The golden hash: md-hash's fixed pattern through the whole engine must give the reference hash of OS 1.63 on the
// interpreter, with one voice DSP and with the voices split over two. Needs the OS file.
#include "mini_test.h"
#include <cstdint>
#include <iomanip>
#include <vector>
#include "Firmware.h"
#include "MdEngine.h"
#include "golden/GoldenPattern.h"
using namespace md;

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
std::string hex(uint64_t v) { std::ostringstream o; o << std::hex << std::setw(16) << std::setfill('0') << v; return o.str(); }
}

TEST_CASE(golden_hash_os163)
{
    engine::Engine eng(os().firmware, os().image);
    const uint64_t h = golden::render(eng);
    CHECK_MSG(eng.fault().empty(), "DSP fault: " << eng.fault());
    CHECK_MSG(h == golden::kHashOs163, "hash " << hex(h) << ", expected " << hex(golden::kHashOs163));
}

TEST_CASE(golden_hash_two_voice_groups)
{
    engine::ParallelEngine eng(os().firmware, os().image, 2);
    const uint64_t h = golden::render(eng);
    CHECK_MSG(eng.fault().empty(), "DSP fault: " << eng.fault());
    CHECK_MSG(h == golden::kHashOs163, "hash " << hex(h) << ", expected " << hex(golden::kHashOs163));
}
