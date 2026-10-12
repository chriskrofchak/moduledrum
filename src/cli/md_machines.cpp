// md-machines: list the OS machine table, or compute one machine's voice-slot words.
// Ported from sd88me/mpc-vst-machinedrum tools/mdmachine/mdmachine.cpp (AGPL-3.0).
// usage: md-machines OS.syx                         list machines
//        md-machines OS.syx ID P1..P8               compute (P = 0-127 SYN values, scaled << 7)
//        md-machines OS.syx ID -raw W1..W8          compute from raw 16-bit parameter words (hex)
//        md-machines                                list the machines of $MD_OS_SYX, else of the MD_OS_SYX CMake option
#include "MachineRunner.h"
#include "Firmware.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
	std::string os = argc > 1 ? argv[1] : "";
	if(os.empty() && std::getenv("MD_OS_SYX")) os = std::getenv("MD_OS_SYX");
	if(os.empty()) os = MD_OS_SYX;
	if(os.empty()) { std::fprintf(stderr, "usage: md-machines OS.syx [ID P1..P8 | ID -raw W1..W8]   (or set MD_OS_SYX)\n"); return 2; }
	try
	{
		auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(os)));
		if(c.sections.empty()) { std::fprintf(stderr, "md-machines: no OS sections in %s\n", os.c_str()); return 1; }
		md::engine::MachineRunner runner(std::move(c.sections.at(0).data));
		if(argc <= 2)
		{
			for(const auto& m : runner.machines())
				std::printf("%3d %-6s fn=$%06x %s %s %s %s %s %s %s %s\n", m.id, m.name.c_str(), m.function,
					m.params[0].c_str(), m.params[1].c_str(), m.params[2].c_str(), m.params[3].c_str(),
					m.params[4].c_str(), m.params[5].c_str(), m.params[6].c_str(), m.params[7].c_str());
			return 0;
		}
		const auto id = static_cast<uint8_t>(std::atoi(argv[2]));
		const bool raw = argc > 3 && !std::strcmp(argv[3], "-raw");
		uint16_t params[8] = {};
		for(int k = 0; k < 8 && 3 + (raw ? 1 : 0) + k < argc; ++k)
		{
			const char* s = argv[3 + (raw ? 1 : 0) + k];
			params[k] = raw ? static_cast<uint16_t>(std::strtoul(s, nullptr, 16)) : static_cast<uint16_t>(std::atoi(s) << 7);
		}
		uint32_t out[32];
		const int n = runner.compute(id, params, out, 32);
		if(n < 0) { std::fprintf(stderr, "fault: %s\n", runner.faultReason().c_str()); return 1; }
		std::printf("count=%d instructions=%llu words:", n, static_cast<unsigned long long>(runner.lastInstructions()));
		for(int k = 1; k < n && k < 32; ++k) std::printf(" %06x", out[k] & 0xffffff);
		std::printf("\n");
	}
	catch(const std::exception& e) { std::fprintf(stderr, "md-machines: %s\n", e.what()); return 1; }
	return 0;
}
