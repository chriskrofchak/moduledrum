// md-kits: look into Machinedrum kit files (.syx) and hear what they decode to.
// usage: md-kits list <file.syx> [OS.syx]
//          every kit dump in the file: index, slot, name and the 16 machines (named from the OS, else by id); the number
//          of patterns, songs and global settings in the file are counted and skipped
//        md-kits render [options] [OS.syx] <file.syx> <kit-index> <out.wav>
//          loads the kit into the engine (machines, parameters, levels, LFOs), plays each track in turn for 0.6 s and
//          then all tracks together for 2 s, and writes the main output as a 24-bit stereo WAV at 44.1 kHz
//   OS.syx defaults to $MD_OS_SYX, then to the MD_OS_SYX CMake option
//   options, for listening to the master section (the kit's rhythm echo, gate box reverb, master EQ and dynamix):
//     --master          apply the kit's 32 master bytes (HostModel::setMasterFx, then primeMaster) and write the master
//                       output instead of the dry main mix
//     --ab              write dry and master back to back: the take is cut into sections (a bar of a pattern, a solo of
//                       the sweep) and each section is played twice, the dry mix and then the master output of the same
//                       frames, so that the ear compares them directly
//     --pattern NAME    sweep (the default, above), beat (8 bars at 120 BPM: BD on 1 and 3, SD on 2 and 4, hats in
//                       eighths, a fill from the toms and the clap at the end of bars 4 and 8, a crash and a kick on the
//                       downbeat of bar 9, then 6 s of silence), echo-glide (a kick on 1 and 3 and one hit of the first
//                       track with a delay send of 64 or more on 2 of each bar; the echo TIME is changed, then the tempo
//                       goes 120 -> 90, then TIME back: the echo glides), reverb-tail (one hit of the first track with a
//                       reverb send of 64 or more, then 10 s of silence: the gate and the tail), eq-gain (the beat with
//                       master EQ GAIN 64 for four bars, then 127)
//     --fx INDEX=VALUE  set master byte INDEX (0-31, the kit dump's order: 0-7 reverb DVOL PRED DEC DAMP HP LP GATE LEV,
//                       8-15 echo TIME MOD MFRQ FB FILTF FILTW MONO LEV, 16-23 EQ LF LG HF HG PF PG PQ GAIN, 24-31
//                       dynamix ATCK REL TRHD RTIO KNEE HP OUTG MIX) to VALUE (0-127) before the first block; repeatable;
//                       needs --master or --ab
//   A pattern other than the sweep, and any --master or --ab file, comes with the dry mix delayed by the master section's
//   own 18 frames (engine::master::kDryLatency) so that the dry and the master file line up sample for sample, and with
//   the kit's levels settled before the first hit. It prints the peak and RMS of both in dBFS (the RMS over the music, up
//   to a second after the last hit) and the tail: the time from the last hit until the output stays below -90 dBFS
//   (rendered on past the file's end where needed; the same to -60 dBFS next to it), also as one "stats" line.
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
			"       md-kits render [--master] [--ab] [--pattern sweep|beat|echo-glide|reverb-tail|eq-gain] [--fx INDEX=VALUE ...]\n"
			"                      [OS.syx] <file.syx> <kit-index> <out.wav>   (OS.syx defaults to $MD_OS_SYX)\n");
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

	using md::engine::Engine;
	constexpr int kRate = 44100, kBlock = Engine::kBlock;
	constexpr int kWarmUp = 300;								// the OS smooths levels and parameters: let them settle (~0.2 s)
	constexpr int64_t kWarmUpFrames = int64_t(kWarmUp) * kBlock;
	constexpr int kDryLatency = md::engine::master::kDryLatency;
	constexpr int kGuardFrames = 441;							// an A/B section is cut 10 ms before its start, ahead of the first hit,
	constexpr int kFadeFrames = 88;								// with 2 ms fades at the cuts (no clicks, no touched attack)
	constexpr double kSilenceDb = -90.0;						// the tail ends when the output stays below this
	constexpr double kAudibleDb = -60.0;						// a second figure: where the tail stops being plainly audible
	constexpr int kMasterBytes = md::engine::master::kParams;
	constexpr int kEqGain = 23;									// master byte: EQ GAIN
	constexpr int kEchoTime = 8;								// master byte: echo TIME
	constexpr int kDelSend = 19, kRevSend = 20;					// track parameters: the delay and reverb sends

	// The Machinedrum's track order, as in the factory kits
	constexpr int kBD = 0, kSD = 1, kHT = 2, kMT = 3, kLT = 4, kCP = 5, kCH = 8, kOH = 9, kCC = 11;

	struct Options
	{
		bool master = false;					// write the master output
		bool ab = false;						// write dry, then master, section by section
		std::string pattern = "sweep";
		std::vector<std::pair<int, int>> fx;	// master byte overrides: index, value
	};

	// What happens when: triggers, master byte changes and tempo changes at frames of the render (44.1 kHz, from the first
	// block; an event is applied before the block that holds its frame)
	struct Event
	{
		enum class Kind { Trigger, MasterFx, Tempo };
		int64_t frame = 0;
		Kind kind = Kind::Trigger;
		int a = 0, b = 0;		// Trigger: track, velocity. MasterFx: index, value
		double bpm = 0;			// Tempo
	};

	struct Script
	{
		std::vector<Event> events;
		std::vector<std::pair<int, int>> fx;	// master bytes set before the first block, over the kit's
		double bpm = 0;							// tempo set before the first block, 0 = the host's own
		int64_t writeFrom = 0;					// the file starts at this frame of the render
		int64_t frames = 0;						// the render's length, a whole number of blocks
		int64_t lastHit = 0;					// the last trigger
		int64_t window = 0;						// RMS is measured up to here
		std::vector<int64_t> sections;			// A/B: the frame where each section ends (the last is `frames`)
		bool aligned = true;					// the dry mix is delayed to line up with the master output
	};

	class Builder
	{
	public:
		explicit Builder(const md::library::Kit& _kit) : m_kit(_kit) {}

		void trigger(const int64_t _frame, const int _track, const int _velocity)
		{
			if(m_kit.machine[static_cast<size_t>(_track)] == 0)		// GND-EMPTY makes no sound
				return;
			Event e;
			e.frame = _frame;
			e.kind = Event::Kind::Trigger;
			e.a = _track;
			e.b = _velocity;
			add(e);
			m_s.lastHit = std::max(m_s.lastHit, _frame);
		}
		void masterFx(const int64_t _frame, const int _index, const int _value)
		{
			Event e;
			e.frame = _frame;
			e.kind = Event::Kind::MasterFx;
			e.a = _index;
			e.b = _value;
			add(e);
		}
		void tempo(const int64_t _frame, const double _bpm)
		{
			Event e;
			e.frame = _frame;
			e.kind = Event::Kind::Tempo;
			e.bpm = _bpm;
			add(e);
		}
		Script& script() { return m_s; }

		// The render ends _tail seconds after the last trigger; the last section runs to there
		Script finish(const double _tailSeconds)
		{
			std::stable_sort(m_s.events.begin(), m_s.events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
			const int64_t end = m_s.lastHit + std::llround(_tailSeconds * kRate);
			m_s.frames = (end + kBlock - 1) / kBlock * kBlock;
			m_s.window = std::min(m_s.frames, m_s.lastHit + kRate);
			m_s.sections.push_back(m_s.frames);
			return m_s;
		}

	private:
		void add(const Event& _e) { m_s.events.push_back(_e); }

		const md::library::Kit& m_kit;
		Script m_s;
	};

	// The first sweep: each track on its own for 0.6 s, then all together for 2 s, from the warm-up on
	Script sweepScript(const md::library::Kit& _kit)
	{
		constexpr int64_t kSolo = int64_t(kRate * 6 / 10 / kBlock) * kBlock, kTogether = int64_t(kRate * 2 / kBlock) * kBlock;
		Builder b(_kit);
		std::vector<int> playing;
		for(size_t t = 0; t < _kit.machine.size(); ++t)
			if(_kit.machine[t] != 0)
				playing.push_back(static_cast<int>(t));
		int64_t f = kWarmUpFrames;
		for(const int t : playing)
		{
			b.trigger(f, t, 100);
			f += kSolo;
			b.script().sections.push_back(f);
		}
		for(const int t : playing)
			b.trigger(f, t, 100);
		Script s = b.finish(0);
		s.frames = f + kTogether;
		s.window = s.frames;
		s.sections.back() = s.frames;
		s.writeFrom = 0;			// the file holds the warm-up too
		s.aligned = false;
		return s;
	}

	// Eight bars at 120 BPM from the music's start _t0: the groove, two fills, a crash and a kick on the downbeat of bar 9.
	// _plain: the two four-bar phrases are the same (no crash on bar 5, the short fill twice)
	void addBeat(Builder& _b, const int64_t _t0, const bool _plain)
	{
		struct Hit { int step; int track; int vel; };
		static constexpr Hit kFillShort[] = {{12, kSD, 110}, {13, kHT, 100}, {14, kMT, 100}, {15, kLT, 105}};
		static constexpr Hit kFillLong[] = {{8, kBD, 110}, {10, kSD, 100}, {11, kSD, 105}, {12, kHT, 105}, {13, kHT, 100},
			{14, kMT, 105}, {14, kCP, 95}, {15, kLT, 110}};
		const double sixteenth = kRate * 60.0 / 120.0 / 4.0;			// 5512.5 frames
		const auto at = [&](const int _bar, const int _step) { return _t0 + static_cast<int64_t>((_bar * 16 + _step) * sixteenth); };
		for(int bar = 0; bar < 8; ++bar)
		{
			const bool fillShort = bar == 3 || (bar == 7 && _plain);
			const bool fillLong = bar == 7 && !_plain;
			const int grooveEnd = fillShort ? 12 : fillLong ? 8 : 16;	// the groove gives way to the fill from this step
			for(int s = 0; s < grooveEnd; ++s)
			{
				if(s == 0 || s == 8)
					_b.trigger(at(bar, s), kBD, s == 0 ? 115 : 105);
				if(s == 4 || s == 12)
				{
					_b.trigger(at(bar, s), kSD, 110);
					if(bar >= 4 && bar <= 6)
						_b.trigger(at(bar, s), kCP, 90);				// the second phrase doubles the backbeat with the clap
				}
				if(s % 2 == 0)
				{
					if(s == 14 && (bar == 1 || bar == 5))
						_b.trigger(at(bar, s), kOH, 95);
					else
						_b.trigger(at(bar, s), kCH, s % 4 == 0 ? 100 : 70);
				}
			}
			if(fillShort)
				for(const Hit& h : kFillShort)
					_b.trigger(at(bar, h.step), h.track, h.vel);
			if(fillLong)
				for(const Hit& h : kFillLong)
					_b.trigger(at(bar, h.step), h.track, h.vel);
			if(bar == 4 && !_plain)
				_b.trigger(at(bar, 0), kCC, 110);
			_b.script().sections.push_back(at(bar + 1, 0));
		}
		_b.trigger(at(8, 0), kCC, 115);									// the end: crash and kick, then the tail
		_b.trigger(at(8, 0), kBD, 120);
	}

	// The track a demonstration plays into an effect: the first sounding one whose send (kit parameter _param) is at least
	// 64, else the one with the biggest send, else _fallback
	int sendTrack(const md::library::Kit& _kit, const int _param, const int _fallback)
	{
		int best = _fallback, value = 0;
		for(int t = 0; t < md::library::kTracks; ++t)
		{
			const int send = _kit.params[static_cast<size_t>(t)][static_cast<size_t>(_param)];
			if(_kit.machine[static_cast<size_t>(t)] == 0)
				continue;
			if(send >= 64)
				return t;
			if(send > value)
			{
				value = send;
				best = t;
			}
		}
		return best;
	}

	Script buildScript(const std::string& _pattern, const md::library::Kit& _kit)
	{
		const int64_t t0 = kWarmUpFrames;
		if(_pattern == "sweep")
			return sweepScript(_kit);
		Builder b(_kit);
		if(_pattern == "beat" || _pattern == "eq-gain")
		{
			const bool eq = _pattern == "eq-gain";
			addBeat(b, t0, eq);
			if(eq)
			{
				b.script().fx.emplace_back(kEqGain, 64);
				b.masterFx(t0 + static_cast<int64_t>(4 * 16 * (kRate * 60.0 / 120.0 / 4.0)), kEqGain, 127);	// the downbeat of bar 5
			}
			b.script().bpm = 120;
		}
		else if(_pattern == "echo-glide")
		{
			// A kick on 1 and 3 and one echoed hit on 2 of each bar. Right after the echoed hit of bar 3 the echo TIME changes,
			// after the one of bar 5 the tempo goes from 120 to 90 BPM (the echo's TIME is tempo-relative), after the one of
			// bar 7 TIME goes back: each makes the echo in flight glide, and so sweep in pitch.
			const int source = sendTrack(_kit, kDelSend, kSD);
			const int time0 = _kit.masterFx.delay[0];
			const int time1 = time0 >= 40 ? 16 : 48;
			double bpm = 120;
			int64_t t = t0;
			for(int bar = 0; bar < 8; ++bar)
			{
				for(int beat = 0; beat < 4; ++beat)
				{
					const double beatFrames = kRate * 60.0 / bpm;
					if(beat == 0 || beat == 2)
						b.trigger(t, kBD, 110);
					if(beat == 1)
					{
						b.trigger(t, source, 110);
						const int64_t change = t + kRate * 15 / 100;
						if(bar == 2)
							b.masterFx(change, kEchoTime, time1);
						if(bar == 4)
						{
							b.tempo(change, 90);
							bpm = 90;
						}
						if(bar == 6)
							b.masterFx(change, kEchoTime, time0);
					}
					t += static_cast<int64_t>(beatFrames);
				}
				b.script().sections.push_back(t);
			}
			b.script().bpm = 120;
		}
		else if(_pattern == "reverb-tail")
		{
			b.trigger(t0 + kRate / 2, sendTrack(_kit, kRevSend, kSD), 110);
			b.script().bpm = 120;
			Script s = b.finish(10);
			s.writeFrom = t0;
			return s;
		}
		else
			throw std::runtime_error("unknown pattern '" + _pattern + "' (sweep, beat, echo-glide, reverb-tail, eq-gain)");
		Script s = b.finish(6);
		s.writeFrom = t0;
		return s;
	}

	struct Level
	{
		int32_t peak = 0;
		double peakDb = -999, rmsDb = -999;
	};

	double toDb(const double _x) { return _x > 0 ? 20.0 * std::log10(_x / 8388608.0) : -999.0; }

	// Peak over the whole take, RMS (both channels) over its first _window frames
	Level measure(const std::vector<int32_t>& _s, const int64_t _window)
	{
		int32_t peak = 0;
		for(const int32_t v : _s)
			peak = std::max(peak, std::abs(v));
		double sum = 0;
		const size_t n = std::min<size_t>(_s.size(), static_cast<size_t>(_window) * 2);
		for(size_t i = 0; i < n; ++i)
			sum += double(_s[i]) * double(_s[i]);
		Level l;
		l.peak = peak;
		l.peakDb = toDb(peak);
		l.rmsDb = n ? toDb(std::sqrt(sum / double(n))) : -999.0;
		return l;
	}

	// The frames [_from, _to) of the take _s (interleaved stereo; frames outside it are silence), with a short fade in at the
	// start (if _fadeIn) and out at the end
	void appendSection(std::vector<int32_t>& _out, const std::vector<int32_t>& _s, const int64_t _from, const int64_t _to, const bool _fadeIn)
	{
		const int64_t have = static_cast<int64_t>(_s.size() / 2);
		for(int64_t f = _from; f < _to; ++f)
		{
			int64_t gain = 1, of = 1;
			if(_fadeIn && f - _from < kFadeFrames)
			{
				gain = f - _from;
				of = kFadeFrames;
			}
			if(_to - 1 - f < kFadeFrames && _to - _from > 2 * kFadeFrames)
			{
				gain = gain * (_to - 1 - f);
				of *= kFadeFrames;
			}
			for(size_t ch = 0; ch < 2; ++ch)
			{
				const int64_t v = f < have ? _s[static_cast<size_t>(f) * 2 + ch] : 0;
				_out.push_back(static_cast<int32_t>(v * gain / of));
			}
		}
	}

	// dry then master for every section, the cuts 10 ms ahead of a section's start
	std::vector<int32_t> composeAb(const std::vector<int32_t>& _dry, const std::vector<int32_t>& _wet, const Script& _sc)
	{
		std::vector<int32_t> out;
		int64_t from = 0;
		for(size_t k = 0; k < _sc.sections.size(); ++k)
		{
			const int64_t end = _sc.sections[k] - _sc.writeFrom;
			const int64_t to = k + 1 < _sc.sections.size() ? std::max(from, end - kGuardFrames) : end;
			appendSection(out, _dry, from, to, from > 0);
			appendSection(out, _wet, from, to, from > 0);
			from = to;
		}
		return out;
	}

	int render(const std::string& _os, const std::string& _file, const int _index, const std::string& _out, const Options& _opt)
	{
		const auto bank = md::library::loadSyxFile(_file);
		if(_index < 0 || static_cast<size_t>(_index) >= bank.kits.size())
		{
			std::fprintf(stderr, "md-kits: kit index %d out of range (the file has %zu kits)\n", _index, bank.kits.size());
			return 1;
		}
		const auto& kit = bank.kits[static_cast<size_t>(_index)];
		const bool withMaster = _opt.master || _opt.ab;
		if(!_opt.fx.empty() && !withMaster)
		{
			std::fprintf(stderr, "md-kits: --fx needs --master or --ab\n");
			return 2;
		}
		Script sc = buildScript(_opt.pattern, kit);
		sc.aligned = sc.aligned || withMaster;

		const auto fwv = md::fw::loadFirmware(_os);
		auto c = md::fw::parseContainer(md::fw::parseSysex(md::fw::readFile(_os)));
		Engine eng(fwv, std::move(c.sections.at(0).data), md::engine::EngineOptions{withMaster});	// without --master/--ab: the dry tracks only
		if(withMaster && !eng.hasMaster())
		{
			std::fprintf(stderr, "md-kits: this OS file has no master section\n");
			return 1;
		}
		auto& host = eng.host();
		md::library::applyKit(host, kit);
		auto bytes = kit.masterFx.bytes();
		if(withMaster)
		{
			for(const auto& f : sc.fx)
				bytes[static_cast<size_t>(f.first)] = static_cast<uint8_t>(f.second);
			for(const auto& f : _opt.fx)
				bytes[static_cast<size_t>(f.first)] = static_cast<uint8_t>(f.second);
			for(int i = 0; i < kMasterBytes; ++i)
				host.setMasterFx(i, bytes[static_cast<size_t>(i)]);
		}
		if(sc.bpm > 0)
			host.setTempo(sc.bpm);
		if(withMaster)
			eng.primeMaster();		// start at the kit's settings and the tempo: no glide in from the cleared kit's

		std::vector<int32_t> dry, wet;		// the whole render, interleaved L, R
		Engine::Output out;
		bool ok = true;
		size_t next = 0;
		const double threshold = 8388608.0 * std::pow(10.0, kSilenceDb / 20.0), audible = 8388608.0 * std::pow(10.0, kAudibleDb / 20.0);
		int64_t lastLoudDry = -1, lastLoudWet = -1;		// the last frame of the render above the threshold
		int64_t lastAudibleDry = -1, lastAudibleWet = -1;	// the same for -60 dBFS
		const bool measureTail = withMaster || _opt.pattern != "sweep";	// the plain sweep stops where it always did
		const int64_t cap = measureTail ? sc.frames + 120 * int64_t(kRate) : sc.frames;
		int64_t frame = 0;
		while(ok && frame < cap)
		{
			if(frame >= sc.frames && frame - std::max({lastLoudDry, lastLoudWet, sc.lastHit}) > 4 * int64_t(kRate))
				break;		// past the file's end and quiet for 4 s: the tail has ended (measured, not written)
			while(next < sc.events.size() && sc.events[next].frame < frame + kBlock)
			{
				const Event& e = sc.events[next++];
				if(e.kind == Event::Kind::Trigger)
					host.trigger(e.a, e.b);
				else if(e.kind == Event::Kind::Tempo)
					host.setTempo(e.bpm);
				else if(withMaster)
					host.setMasterFx(e.a, e.b);
			}
			ok = eng.render(out);
			for(size_t f = 0; f < kBlock; ++f)
			{
				const int32_t d[2] = {std::clamp(out.mix.main[f][0], -8388608, 8388607), std::clamp(out.mix.main[f][1], -8388608, 8388607)};
				const int32_t m[2] = {std::clamp(out.master[f][0], -8388608, 8388607), std::clamp(out.master[f][1], -8388608, 8388607)};
				if(std::abs(d[0]) > threshold || std::abs(d[1]) > threshold)
					lastLoudDry = frame + static_cast<int64_t>(f);
				if(std::abs(m[0]) > threshold || std::abs(m[1]) > threshold)
					lastLoudWet = frame + static_cast<int64_t>(f);
				if(std::abs(d[0]) > audible || std::abs(d[1]) > audible)
					lastAudibleDry = frame + static_cast<int64_t>(f);
				if(std::abs(m[0]) > audible || std::abs(m[1]) > audible)
					lastAudibleWet = frame + static_cast<int64_t>(f);
				if(frame < sc.frames)
				{
					dry.push_back(d[0]);
					dry.push_back(d[1]);
					wet.push_back(m[0]);
					wet.push_back(m[1]);
				}
			}
			frame += kBlock;
		}
		if(!ok)
		{
			std::fprintf(stderr, "md-kits: DSP fault: %s\n", eng.fault().c_str());
			return 1;
		}

		// The takes as the file has them: from writeFrom, the dry one delayed by the master section's own latency
		const auto take = [&](const std::vector<int32_t>& _s, const int _delay)
		{
			std::vector<int32_t> t;
			const int64_t n = (sc.frames - sc.writeFrom);
			t.reserve(static_cast<size_t>(n) * 2);
			for(int64_t f = 0; f < n; ++f)
			{
				const int64_t src = sc.writeFrom + f - _delay;
				for(size_t ch = 0; ch < 2; ++ch)
					t.push_back(src >= 0 ? _s[static_cast<size_t>(src) * 2 + ch] : 0);
			}
			return t;
		};
		const std::vector<int32_t> dryTake = take(dry, sc.aligned ? kDryLatency : 0);
		const std::vector<int32_t> wetTake = take(wet, 0);

		const std::vector<int32_t>& single = _opt.master ? wetTake : dryTake;
		const std::vector<int32_t> abTake = _opt.ab ? composeAb(dryTake, wetTake, sc) : std::vector<int32_t>();
		const std::vector<int32_t>& file = _opt.ab ? abTake : single;
		if(!md::util::writeWav24(_out, file, 2, kRate))
		{
			std::fprintf(stderr, "md-kits: cannot write %s\n", _out.c_str());
			return 1;
		}

		std::vector<int> playing;
		for(size_t t = 0; t < kit.machine.size(); ++t)
			if(kit.machine[t] != 0)
				playing.push_back(static_cast<int>(t));
		const double seconds = double(file.size() / 2) / kRate;
		const int64_t window = sc.window - sc.writeFrom;
		const Level ld = measure(dryTake, window), lw = measure(wetTake, window);
		// the tails: seconds from the last hit to the last frame at or above the threshold (the dry one in file time)
		const double hit = double(sc.lastHit);
		const double skew = sc.aligned ? kDryLatency : 0;
		const double tailDry = std::max(0.0, double(lastLoudDry + 1) + skew - hit) / kRate;
		const double tailWet = std::max(0.0, double(lastLoudWet + 1) - hit) / kRate;
		const double audibleDry = std::max(0.0, double(lastAudibleDry + 1) + skew - hit) / kRate;
		const double audibleWet = std::max(0.0, double(lastAudibleWet + 1) - hit) / kRate;

		if(_opt.pattern == "sweep" && !withMaster)
		{
			std::printf("kit %d \"%s\" (slot %d): %zu tracks play; %s: %.1f s, peak %d of 8388607 (%.1f dBFS)\n", _index, kit.name.c_str(),
				kit.position, playing.size(), _out.c_str(), seconds, ld.peak, ld.peakDb);
			return 0;
		}
		std::printf("kit %d \"%s\" (slot %d): pattern %s%s%s: %s: %.1f s\n", _index, kit.name.c_str(), kit.position, _opt.pattern.c_str(),
			_opt.master ? ", master" : "", _opt.ab ? ", A/B (each section dry, then master)" : "", _out.c_str(), seconds);
		if(withMaster)
		{
			std::printf("  master bytes:");
			for(int i = 0; i < kMasterBytes; ++i)
				std::printf("%s%d", i % 8 == 0 ? " | " : " ", bytes[static_cast<size_t>(i)]);
			std::printf("\n");
		}
		std::printf("  dry     peak %6.1f dBFS  rms %6.1f dBFS  tail %5.2f s (to -60 dBFS: %5.2f s)\n", ld.peakDb, ld.rmsDb, tailDry, audibleDry);
		if(withMaster)
		{
			std::printf("  master  peak %6.1f dBFS  rms %6.1f dBFS  tail %5.2f s (to -60 dBFS: %5.2f s)\n", lw.peakDb, lw.rmsDb, tailWet, audibleWet);
			std::printf("  master - dry: peak %+.1f dB  rms %+.1f dB\n", lw.peakDb - ld.peakDb, lw.rmsDb - ld.rmsDb);
			std::printf("stats kit=%d name=\"%s\" pattern=%s dry_peak=%.2f dry_rms=%.2f dry_tail=%.2f dry_tail60=%.2f master_peak=%.2f master_rms=%.2f master_tail=%.2f master_tail60=%.2f\n",
				_index, kit.name.c_str(), _opt.pattern.c_str(), ld.peakDb, ld.rmsDb, tailDry, audibleDry, lw.peakDb, lw.rmsDb, tailWet, audibleWet);
		}
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
		if(cmd == "render")
		{
			Options opt;
			std::vector<std::string> pos;
			for(int i = 2; i < argc; ++i)
			{
				const std::string a = argv[i];
				if(a == "--master")
					opt.master = true;
				else if(a == "--ab")
					opt.ab = true;
				else if(a == "--pattern" && i + 1 < argc)
					opt.pattern = argv[++i];
				else if(a == "--fx" && i + 1 < argc)
				{
					int index = -1, value = -1;
					if(std::sscanf(argv[++i], "%d=%d", &index, &value) != 2 || index < 0 || index >= kMasterBytes || value < 0 || value > 127)
					{
						std::fprintf(stderr, "md-kits: --fx takes INDEX=VALUE, INDEX 0-31 and VALUE 0-127\n");
						return 2;
					}
					opt.fx.emplace_back(index, value);
				}
				else if(a.rfind("--", 0) == 0)
					return usage();
				else
					pos.push_back(a);
			}
			if(pos.size() != 3 && pos.size() != 4)
				return usage();
			const bool withOs = pos.size() == 4;
			const std::string os = withOs ? pos[0] : defaultOs();
			if(os.empty())
				return usage();
			return render(os, pos[withOs ? 1 : 0], std::atoi(pos[withOs ? 2 : 1].c_str()), pos[withOs ? 3 : 2], opt);
		}
	}
	catch(const std::exception& e)
	{
		std::fprintf(stderr, "md-kits: %s\n", e.what());
		return 1;
	}
	return usage();
}
