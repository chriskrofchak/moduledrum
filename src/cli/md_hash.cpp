// md-hash: a hash of 12 s of a fixed 16-track pattern (random machines incl. ROM, routing, triggers) through the whole
// engine: equal hashes = identical audio. Compare builds (interpreter vs JIT, arm64 vs x86, skip paths on/off).
// Ported from sd88me/mpc-vst-machinedrum tools/mdhash/mdhash.cpp (AGPL-3.0); the pattern is src/core/golden/GoldenPattern.h.
// usage: md-hash [OS.syx] [ROM_SAMPLES.bin]   (with the ROM samples, the pattern plays two ROM machines too)
//   OS.syx defaults to $MD_OS_SYX, then to the MD_OS_SYX CMake option
//   MD_GROUPS=n   voices split over n DSP2 instances (must give the same hash)
//   MD_SWEEP=1    plays every machine on a fresh engine and prints "SWEEP id name peak" (none of the offered ones silent)
//   MD_BUDGET=n   voice budget n; prints the max/mean number of voices that rendered
//   MD_RELEASE=n  silence release after n quiet blocks (HostModel::setSilenceRelease)
//   MD_RAW=file   main L/R as int32, for comparing two runs
//   MD_T1=1       voice budget 1 walkthrough (prints the active voice count)
//   MD_TIMING=1   also prints the render's wall-clock time and its stages (voice DSP, OS tick, track fx, mix)
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Firmware.h"
#include "MdEngine.h"
#include "golden/GoldenPattern.h"

// ROM machines' sample memory (tools/mdkits/mdsamples in the MPC port): "MDSM" + [addr][count] + count words, repeated
template<class E>
static void loadSamples(E& _eng, const char* _path)
{
	FILE* rf = fopen(_path, "rb");
	if(!rf)
		return;
	char magic[4]; std::vector<uint32_t> words; uint32_t head[2];
	if(fread(magic, 1, 4, rf) == 4)
		while(fread(head, 4, 2, rf) == 2 && head[1] > 0 && head[1] < 0x800000)
		{
			words.resize(head[1]);
			if(fread(words.data(), 4, head[1], rf) != head[1]) break;
			_eng.voices().writeP(head[0], words.data(), words.size());
		}
	fclose(rf);
}

template<class E>
static void run(E& eng, const char* romSamples)
{
	if(romSamples)
		loadSamples(eng, romSamples);
	auto& h = eng.host();
	if(getenv("MD_T1"))
	{
		md::golden::Pattern p;
		p.setup(h);
		typename E::Output out;
		h.setMaxActiveVoices(1);
		auto run = [&](const char* tag, int n){ for(int i = 0; i < n; ++i) eng.render(out); printf("%s: active %d\n", tag, eng.voices().activeVoicesLastBlock()); };
		run("boot", 10);
		h.trigger(0, 100); run("track0 triggered", 20);
		h.trigger(1, 100); run("track1 triggered (budget 1 cuts track0)", 20);
		run("later", 100);
		return;
	}
	int maxActive = 0; long sumActive = 0, nb = 0;
	if(getenv("MD_BUDGET")) h.setMaxActiveVoices(atoi(getenv("MD_BUDGET")));
	if(getenv("MD_RELEASE")) h.setSilenceRelease(128, atoi(getenv("MD_RELEASE")));	// silence release, hold in blocks
	FILE* raw = getenv("MD_RAW") ? fopen(getenv("MD_RAW"), "wb") : nullptr;
	const bool timing = getenv("MD_TIMING") != nullptr;
	if(timing) { h.timingOn = true; eng.timingOn = true; }
	const auto t0 = std::chrono::steady_clock::now();
	const uint64_t hash = md::golden::render(eng, [&](int b, const typename E::Output& out)
	{
		if(b > 2000) { const int a = eng.voices().activeVoicesLastBlock(); maxActive = a > maxActive ? a : maxActive; sumActive += a; ++nb; }
		if(raw) fwrite(out.mix.main.data(), sizeof(int32_t), 64, raw);
	});
	const double renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	if(raw) fclose(raw);
	if(!eng.fault().empty()) fprintf(stderr, "md-hash: DSP fault: %s\n", eng.fault().c_str());
	printf("hash %016llx\n", (unsigned long long)hash);
	if(timing)	// the 12 s pattern (engine already booted): the whole render and its stages, wall clock
		printf("timing: render %.0f ms (%.1f %% of real time); voice DSP %.0f ms, OS tick %.0f ms, track fx %.0f ms, mix %.0f ms\n",
			renderMs, renderMs / (1000.0 * md::golden::kBlocks * 32 / 44100.0) * 100.0, h.dspUs / 1000.0, h.tickUs / 1000.0, eng.fxUs / 1000.0, eng.mixUs / 1000.0);
	if(getenv("MD_BUDGET") || getenv("MD_RELEASE")) printf("budget %s release %s: max rendered voices %d, mean %.2f, released %u\n", getenv("MD_BUDGET") ? getenv("MD_BUDGET") : "-", getenv("MD_RELEASE") ? getenv("MD_RELEASE") : "-", maxActive, double(sumActive) / (nb ? nb : 1), h.silenceReleases());
}

int main(int argc, char** argv)
{
	std::string os = argc > 1 ? argv[1] : "";
	if(os.empty() && getenv("MD_OS_SYX")) os = getenv("MD_OS_SYX");
	if(os.empty()) os = MD_OS_SYX;
	if(os.empty()) { fprintf(stderr, "usage: md-hash OS.syx [ROM_SAMPLES.bin]   (or set MD_OS_SYX)\n"); return 2; }
	const char* romSamples = argc > 2 ? argv[2] : nullptr;
	try
	{
		const auto fwv = md::fw::loadFirmware(os);
		auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(os)));
		if(getenv("MD_SWEEP"))	// every machine on a fresh engine: does it make sound? (MD_SWEEP=1)
		{
			std::vector<int> ids;
			{
				md::engine::Engine e0(fwv, std::vector<uint8_t>(c.sections.at(0).data));
				for(const auto& mc : e0.os().machines()) ids.push_back(mc.id);
			}
			for(const int id : ids)
			{
				md::engine::Engine e(fwv, std::vector<uint8_t>(c.sections.at(0).data));
				if(romSamples)
					loadSamples(e, romSamples);
				auto& hh = e.host();
				const auto* mc = e.os().machine(uint8_t(id));
				hh.setMachine(2, uint8_t(id));
				for(int q = 0; q < 8; ++q) hh.setParam(2, q, mc->defaults[q]);
				hh.setParam(2, 13, 127); hh.setParam(2, 17, 100);
				md::engine::Engine::Output o;
				for(int b = 0; b < 40; ++b) e.render(o);
				hh.trigger(2, 100);
				int peak = 0;
				for(int b = 0; b < 700; ++b) { e.render(o); for(int f = 0; f < 32; ++f) peak = std::max(peak, std::abs(int(o.tracks[2][f] >> 8))); }
				printf("SWEEP %3d %-8s peak %d\n", id, mc->name.c_str(), peak);
				fflush(stdout);
			}
			return 0;
		}
		// MD_GROUPS=n (n>1): the voices split across n DSP2 instances on persistent threads; must give the same hash
		const int groups = getenv("MD_GROUPS") ? atoi(getenv("MD_GROUPS")) : 1;
		if(groups > 1)
		{
			md::engine::ParallelEngine eng(fwv, std::move(c.sections.at(0).data), groups);
			run(eng, romSamples);
		}
		else
		{
			md::engine::Engine eng(fwv, std::move(c.sections.at(0).data));
			run(eng, romSamples);
		}
	}
	catch(const std::exception& e) { fprintf(stderr, "md-hash: %s\n", e.what()); return 1; }
	return 0;
}
