// OS file decoding: what a wrong file does (no OS needed), and the shape of OS 1.63 (needs the OS file).
#include "mini_test.h"
#include <cstdint>
#include <vector>
#include "Firmware.h"
#include "MachineRunner.h"
using namespace md;

TEST_CASE(firmware_rejects_wrong_files)
{
    // no sysex at all
    const std::vector<uint8_t> text = {'h', 'e', 'l', 'l', 'o'};
    CHECK_THROWS(fw::parseSysex(text));
    // a well-formed Elektron OS data message, but for the Monomachine (device 3)
    std::vector<uint8_t> mnm = {0xF0, 0x00, 0x20, 0x3C, fw::kDeviceMonomachine, 0x00, 0x7E, 0, 0, 0, 0, 0, 4, 0, 0, 1, 2, 3, 0xF7};
    CHECK_THROWS(fw::parseSysex(mnm));
    // the same data as a Machinedrum message parses, but holds no OS sections
    mnm[4] = fw::kDeviceMachinedrum;
    CHECK(fw::parseContainer(fw::parseSysex(mnm)).sections.empty());
    CHECK_THROWS(fw::loadFirmware("/nonexistent/Elektron_SPS1-1UW_OS1.63.syx"));
}

TEST_CASE(firmware_os163_sections)
{
    const auto path = mt::requireOsFile();
    const auto c = fw::parseContainer(fw::parseSysex(fw::readFile(path)));
    CHECK_MSG(c.sections.size() >= 3, "sections: " << c.sections.size());
    for (int k = 1; k <= 2; ++k) CHECK(c.sections[size_t(k)].sumOk);
    const auto f = fw::loadFirmware(path);
    CHECK(!f.dspA.records.empty());
    CHECK(!f.dspB.records.empty());
    // the machine table of OS 1.63 (increment 0001: 135 machines)
    engine::MachineRunner runner(c.sections.at(0).data);
    CHECK_EQ(runner.machines().size(), size_t(135));
}
