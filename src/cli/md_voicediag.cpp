// md-voicediag: look at the voice DSP as VoiceEngine sets it up - the OS's voice program with the harness patched in -
// and step it from its entry, to see where a build (the JIT, a new harness) goes astray.
// usage: md-voicediag [OS.syx] dis <from> <to> [<from> <to>...]   disassemble P memory (hex addresses)
//        md-voicediag [OS.syx] trace [count] [skip]                 run from the entry: one line per exec() (one block
//                                                                     with the JIT, one instruction or DO loop with the
//                                                                     interpreter): PC, instructions run, SR, LA/LC,
//                                                                     host words sent so far, and the first instruction
//   OS.syx defaults to $MD_OS_SYX, then to the MD_OS_SYX CMake option
// The output is disassembled firmware: never commit it.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "Firmware.h"
#include "VoiceEngine.h"

#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kEmu/peripherals.h"

using namespace dsp56k;

namespace
{
	int usage()
	{
		std::fprintf(stderr, "usage: md-voicediag [OS.syx] dis <from> <to> [<from> <to>...]\n"
			"       md-voicediag [OS.syx] trace [count] [skip]   (OS.syx defaults to $MD_OS_SYX)\n");
		return 2;
	}

	bool isMode(const char* _s) { return std::string(_s) == "dis" || std::string(_s) == "trace"; }
}

int main(int argc, char** argv)
{
	int arg = 1;
	std::string os;
	if(arg < argc && !isMode(argv[arg]))
		os = argv[arg++];
	if(os.empty() && std::getenv("MD_OS_SYX")) os = std::getenv("MD_OS_SYX");
	if(os.empty()) os = MD_OS_SYX;
	if(os.empty() || arg >= argc)
		return usage();
	const std::string mode = argv[arg++];
	try
	{
		const auto fwv = md::fw::loadFirmware(os);
		md::engine::VoiceEngine ve(fwv, false);	// loaded and patched, PC at the entry, nothing run
		auto& dsp = ve.dsp();
		Opcodes opcodes;
		Disassembler dis(opcodes);
		auto disasm = [&](const TWord _pc, std::string& _out)
		{
			const TWord a = dsp.memory().get(MemArea_P, _pc), b = dsp.memory().get(MemArea_P, _pc + 1);
			const auto len = dis.disassemble(_out, a, b, 0, 0, _pc);
			return len ? len : 1u;
		};

		if(mode == "dis")
		{
			if(arg + 1 >= argc)
				return usage();
			for(; arg + 1 < argc; arg += 2)
			{
				const TWord from = static_cast<TWord>(std::strtoul(argv[arg], nullptr, 16));
				const TWord to = static_cast<TWord>(std::strtoul(argv[arg + 1], nullptr, 16));
				for(TWord pc = from; pc < to;)
				{
					std::string s;
					const auto len = disasm(pc, s);
					std::printf("p:%06x  %06x %06x  %s\n", pc, dsp.memory().get(MemArea_P, pc),
						len > 1 ? dsp.memory().get(MemArea_P, pc + 1) : 0u, s.c_str());
					pc += len;
				}
			}
			return 0;
		}

		const uint64_t count = arg < argc ? std::strtoull(argv[arg++], nullptr, 0) : 1000;
		const uint64_t skip = arg < argc ? std::strtoull(argv[arg++], nullptr, 0) : 0;
		auto& hi = static_cast<Peripherals56303*>(dsp.getPeriph(0))->getHI08();
		std::printf("%s\n", g_useJIT ? "JIT: one line per JIT block" : "interpreter: one line per instruction (a DO loop: per loop)");
		for(uint64_t i = 0; i < skip + count; ++i)
		{
			const TWord pc = dsp.getPC().toWord();
			const auto before = dsp.getInstructionCounter();
			const auto sr = dsp.getSR().var;
			const auto la = dsp.regs().la.var, lc = dsp.regs().lc.var;
			dsp.exec();
			if(i < skip)
				continue;
			std::string s;
			disasm(pc, s);
			std::printf("%8llu  pc=%06x n=%-7llu sr=%06x la=%06x lc=%06x tx=%-4zu %s\n", static_cast<unsigned long long>(i), pc,
				static_cast<unsigned long long>(dsp.getInstructionCounter() - before), sr, la, lc, hi.txData().size(), s.c_str());
		}
		return 0;
	}
	catch(const std::exception& e)
	{
		std::fprintf(stderr, "md-voicediag: %s\n", e.what());
		return 1;
	}
}
