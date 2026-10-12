// md-kits: look into Machinedrum kit files (.syx) and hear what they decode to.
// usage: md-kits list <file.syx> [OS.syx]
//          every kit dump in the file: index, slot, name and the 16 machines (named from the OS, else by id); the number
//          of patterns, songs and global settings in the file are counted and skipped
//        md-kits render [OS.syx] <file.syx> <kit-index> <out.wav>
//          loads the kit into the engine (machines, parameters, levels, LFOs), plays each track in turn for 0.6 s and
//          then all tracks together for 2 s, and writes the main output as a 24-bit stereo WAV at 44.1 kHz
//   OS.syx defaults to $MD_OS_SYX, then to the MD_OS_SYX CMake option
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Firmware.h"
#include "MachineRunner.h"
#include "MdEngine.h"
#include "md/library/KitApply.h"
#include "md/library/MdSysex.h"
#include "util/Wav.h"

namespace
{
	std::string defaultOs()
	{
		std::string os = std::getenv("MD_OS_SYX") ? std::getenv("MD_OS_SYX") : "";
		return os.empty() ? std::string(MD_OS_SYX) : os;
	}

	int usage()
	{
		std::fprintf(stderr, "usage: md-kits list <file.syx> [OS.syx]\n"
			"       md-kits render [OS.syx] <file.syx> <kit-index> <out.wav>   (OS.syx defaults to $MD_OS_SYX)\n");
		return 2;
	}

	void printSummary(const md::library::Bank& _bank)
	{
		std::printf("%s: %zu kits (%d used), skipped: %d patterns, %d songs, %d globals", _bank.name.c_str(), _bank.kits.size(),
			_bank.usedKits(), _bank.patterns, _bank.songs, _bank.globals);
		if(_bank.otherMachinedrum) std::printf(", %d other Machinedrum messages", _bank.otherMachinedrum);
		if(_bank.foreign) std::printf(", %d other sysex", _bank.foreign);
		if(_bank.badChecksums) std::printf("; %d kits with a wrong checksum", _bank.badChecksums);
		if(_bank.rejected) std::printf("; %d messages rejected", _bank.rejected);
		std::printf("\n");
		for(const auto& e : _bank.errors)
			std::printf("  %s\n", e.c_str());
	}

	std::unique_ptr<md::engine::MachineRunner> openRunner(const std::string& _os)
	{
		if(_os.empty())
			return nullptr;
		auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(_os)));
		if(c.sections.empty())
			throw std::runtime_error("no OS sections in " + _os);
		return std::make_unique<md::engine::MachineRunner>(std::move(c.sections.at(0).data));
	}

	std::string machineName(const md::engine::MachineRunner* _runner, const int _id)
	{
		const auto* m = _runner ? _runner->machine(static_cast<uint8_t>(_id)) : nullptr;
		return m ? m->name : "#" + std::to_string(_id);
	}

	int list(const std::string& _file, const std::string& _os)
	{
		const auto bank = md::library::loadSyxFile(_file);
		const auto runner = openRunner(_os);
		if(!runner)
			std::fprintf(stderr, "md-kits: no OS file (argument, $MD_OS_SYX), machines are shown by id\n");
		printSummary(bank);
		for(size_t i = 0; i < bank.kits.size(); ++i)
		{
			const auto& k = bank.kits[i];
			if(k.isFree())
			{
				size_t last = i;	// a run of free slots is one line
				while(last + 1 < bank.kits.size() && bank.kits[last + 1].isFree())
					++last;
				if(last == i)
					std::printf("%3zu  slot %2d  (free)\n", i, k.position);
				else
					std::printf("%3zu-%zu  slots %d-%d  (free)\n", i, last, k.position, bank.kits[last].position);
				i = last;
				continue;
			}
			std::printf("%3zu  slot %2d  %-16s ", i, k.position, k.name.c_str());
			for(const auto id : k.machine)
				std::printf(" %-6s", machineName(runner.get(), id).c_str());
			std::printf("\n");
		}
		return 0;
	}

	int render(const std::string& _os, const std::string& _file, const int _index, const std::string& _out)
	{
		const auto bank = md::library::loadSyxFile(_file);
		if(_index < 0 || static_cast<size_t>(_index) >= bank.kits.size())
		{
			std::fprintf(stderr, "md-kits: kit index %d out of range (the file has %zu kits)\n", _index, bank.kits.size());
			return 1;
		}
		const auto& kit = bank.kits[static_cast<size_t>(_index)];

		const auto fwv = md::fw::loadFirmware(_os);
		auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(_os)));
		md::engine::Engine eng(fwv, std::move(c.sections.at(0).data), md::engine::EngineOptions{false});	// the dry tracks only
		auto& host = eng.host();
		md::library::applyKit(host, kit);

		constexpr int kRate = 44100, kBlock = md::engine::Engine::kBlock;
		constexpr int kWarmUp = 300;							// the OS smooths levels and parameters: let them settle (~0.2 s)
		constexpr int kSolo = kRate * 6 / 10 / kBlock;			// 0.6 s per track
		constexpr int kTogether = kRate * 2 / kBlock;
		std::vector<int32_t> samples;
		md::engine::Engine::Output out;
		bool ok = true;
		const auto run = [&](const int _blocks)
		{
			for(int b = 0; b < _blocks && ok; ++b)
			{
				ok = eng.render(out);
				for(size_t f = 0; f < kBlock; ++f)
					for(size_t ch = 0; ch < 2; ++ch)
						samples.push_back(std::clamp(out.mix.main[f][ch], -8388608, 8388607));
			}
		};

		run(kWarmUp);
		std::vector<int> playing;
		for(size_t t = 0; t < kit.machine.size(); ++t)
			if(kit.machine[t] != 0)								// GND-EMPTY makes no sound
				playing.push_back(static_cast<int>(t));
		for(const int t : playing)
		{
			host.trigger(t, 100);
			run(kSolo);
		}
		for(const int t : playing)
			host.trigger(t, 100);
		run(kTogether);
		if(!ok)
		{
			std::fprintf(stderr, "md-kits: DSP fault: %s\n", eng.fault().c_str());
			return 1;
		}

		int32_t peak = 0;
		for(const int32_t s : samples)
			peak = std::max(peak, std::abs(s));
		if(!md::util::writeWav24(_out, samples, 2, kRate))
		{
			std::fprintf(stderr, "md-kits: cannot write %s\n", _out.c_str());
			return 1;
		}
		std::printf("kit %d \"%s\" (slot %d): %zu tracks play; %s: %.1f s, peak %d of 8388607 (%.1f dBFS)\n", _index, kit.name.c_str(),
			kit.position, playing.size(), _out.c_str(), double(samples.size() / 2) / kRate, peak, peak ? 20.0 * std::log10(peak / 8388608.0) : -999.0);
		return 0;
	}
}

int main(int argc, char** argv)
{
	if(argc < 3)
		return usage();
	const std::string cmd = argv[1];
	try
	{
		if(cmd == "list" && argc <= 4)
			return list(argv[2], argc > 3 ? argv[3] : defaultOs());
		if(cmd == "render" && (argc == 5 || argc == 6))
		{
			const bool withOs = argc == 6;
			const std::string os = withOs ? argv[2] : defaultOs();
			if(os.empty())
				return usage();
			return render(os, argv[withOs ? 3 : 2], std::atoi(argv[withOs ? 4 : 3]), argv[withOs ? 5 : 4]);
		}
	}
	catch(const std::exception& e)
	{
		std::fprintf(stderr, "md-kits: %s\n", e.what());
		return 1;
	}
	return usage();
}
