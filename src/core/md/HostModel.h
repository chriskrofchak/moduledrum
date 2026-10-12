// The Machinedrum's main-processor side, for the voice DSP: per-track kit parameters, machine assignment,
// triggers and the control tick. The maths is the OS's own code run in MachineRunner (parameter smoothing
// $10002e0, LFO oscillator $1000088, LFO apply $1000332, machine coefficient functions); this class does what
// the OS's tick routine ($20ad9a) and slot pump ($10004d0) do around them. See docs/PROTOCOL.md.
//
// It also produces each track's inputs to the mixer DSP (DSP1): the 9 effect words (Y:$200+$40k) and the
// 5 mix words (Y:$100+5k), computed as the tick routine does ($20b1f6-$20b302).
//
// The master effects' parameters are the OS's too: the 32 kit bytes are smoothed by the OS's slew ($100029e, with the
// track levels) and turned into the 38 words DSP1's master section reads (Y:$150-$18c) by the tail of its tick routine
// ($20b4a2-$20ba4c), one of four groups per tick, as on the hardware. The words are for MasterFx.
//
// Not yet modelled: parameter locks and trigger groups (sequencer features).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "MachineRunner.h"
#include "MasterFx.h"
#include "VoiceEngine.h"

namespace md::engine
{
	// TVoices: VoiceEngine (default) or any type with the same setSlot/renderBlock/Block/kSlotWords surface
	// (e.g. ParallelVoiceEngine, which splits the 16 slots across threads - see its header).
	template<class TVoices = VoiceEngine>
	class HostModel
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kParams = 24;	// SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, VOL PAN DEL REV, LFOS LFOD LFOM

		HostModel(MachineRunner& _os, TVoices& _voices);

		// Assign a machine (0-191, MachineInfo::id). SYN1-8 take the machine's defaults. As on the MD, the voice
		// switches machine at its next trigger, where all 24 parameters take effect immediately (no glide).
		void setMachine(int _track, uint8_t _machineId);
		void setParam(int _track, int _param, int _value);	// 0-127, smoothed like the MD
		int param(int _track, int _param) const { return m_raw[_track][_param]; }
		void setTempo(double _bpm);							// tempo factor for tempo-synced LFOs and E12/ROM retrig
		// velocity 1-127; accent: the sequencer's accented step (velocity becomes 128 + 2 x accent amount)
		void trigger(int _track, int _velocity = 100, bool _accent = false);
		void setAccentAmount(int _amount) { m_accentAmount = std::clamp(_amount, 0, 127); }

		// Caps how many tracks can be simultaneously active (most-recently-triggered N; a new distinct
		// trigger past the cap steals the least-recently-triggered one, hard-cutting it to silence - not a
		// real MD behavior, which always runs all 16 track slots, but a performance safety valve for patterns
		// that never actually need all 16 at once). Default kTracks = disabled (every track its own voice, as
		// on real hardware). 1-kTracks; out-of-range clamps.
		// The voice budget, in units: most machines cost 1 and the ROM (sample) machines 2, after what one voice costs the
		// voice DSP on the Force (measured: ROM +397 us, the others +213 us on average per 128-frame block).
		void setMaxActiveVoices(int _n) { m_maxActive = std::clamp(_n, 1, kTracks * 2); }
		static int voiceCost(const uint8_t _machine) { return (_machine >= 128 && _machine < 160) || (_machine >= 176 && _machine < 192) ? 2 : 1; }
		int maxActiveVoices() const { return m_maxActive; }

		// Silence release: a sounding voice whose DSP output stays within +-_threshold (24-bit) for _blocks blocks in a row is
		// freed as a budget cut is (back to the empty machine; it stops costing DSP time and leaves the voice budget). Once
		// triggered, a voice otherwise costs its full DSP time for good, even after decaying to digital silence. 128 (-96 dBFS)
		// is below one LSB of the plugin's 16-bit output. Not the MD's behaviour (it runs every slot always): off by default,
		// so the bit-exact tests are unchanged. The next trigger starts the voice afresh.
		void setSilenceRelease(int _threshold, int _blocks) { m_releaseThreshold = _threshold; m_releaseBlocks = _blocks; }
		uint32_t silenceReleases() const { return m_releases; }	// voices freed this way so far (stats)

		// Track level (kit LEV, 0-127; smoothed by the OS's level slew $100029e), mute, output routing
		// (DSP1's per-track route word; 6 = the main outputs, the MD's default).
		void setLevel(int _track, int _level);
		void setMute(int _track, bool _mute) { m_mute[_track] = _mute; }
		void setRouting(int _track, int _route) { m_route[_track] = static_cast<uint8_t>(_route); }

		// ---- Master effects (rhythm echo, gate box reverb, master EQ, dynamix) ----
		// The kit's 32 master bytes, 0-127, in the kit dump's order (master::kParams: reverb DVOL PRED DEC DAMP HP LP GATE LEV,
		// echo TIME MOD MFRQ FB FILTF FILTW MONO LEV, EQ LF LG HF HG PF PG PQ GAIN, dynamix ATCK REL TRHD RTIO KNEE HP OUTG
		// MIX). They are the OS's smoothing targets: the smoothed values follow them each tick as the hardware's do (the
		// slew ends 3 units below a target it approaches from below, exact from above; a TIME change makes the echo glide).
		// A new HostModel starts on the cleared kit's values (master::kDefaultParams), already settled.
		void setMasterFx(int _index, int _value);
		int masterFx(int _index) const { return m_masterRaw[static_cast<size_t>(_index)]; }

		// The DSP1 words for the smoothed values as of the last tick (master::kWords, ascending Y address, see
		// master::kGroup). One OS group is worked out per tick (tick counter & 3): takeMasterGroups() returns a bit per group
		// that has changed since the last call (bit g = master::kGroup[g]) and clears them; the engine hands those to
		// MasterFx::writeGroup() before the block it renders next, the hardware's cadence.
		const std::array<uint32_t, master::kWords>& masterWords() const { return m_masterWords; }
		unsigned takeMasterGroups() { const unsigned g = m_masterPending; m_masterPending = 0; return g; }

		// Settle the master section at its targets: the smoothed values run through the OS's slew to its fixed point (the
		// track levels' smoothing is left alone), all four groups are worked out, and all four are marked changed. The
		// constructor does this; call it again to start from a kit's (or the tempo's) settings without the glide that a
		// change over the next ticks gives (the engine's primeMaster() also snaps the echo's delay offset).
		void primeMasterFx();

		// What the OS would send for these exact values: _params = 32 bytes (0-127) taken as settled (smoothed value = v << 7,
		// without the slew's offset), _bpm = the tempo. Worked out with the OS's own code on the side: the smoothed values
		// and the tempo are put back afterwards. For warming up the master DSP and for tests.
		void computeMasterWords(const uint8_t* _params, double _bpm, uint32_t* _words);

		// _sets word sets (master::kWords each) for MasterFx::warmUp: the cleared kit's, all-0 and all-127 bytes, then
		// pseudo-random ones (a byte is 0 or 127 one time in four), tempi 60-240 BPM, in a fixed order, so that the warm-up
		// is the same everywhere. The host's own state is left as it was.
		void masterWarmUpWords(int _sets, uint32_t* _words);

		// Per-track inputs to the mixer DSP, as of the last tick.
		struct MixerInput
		{
			std::array<uint32_t, 9> fx{};	// AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST (smoothed + LFO, value << 7)
			std::array<uint32_t, 5> mix{};	// route, VOL gain, PAN << 16, REV send, DEL send
		};
		const MixerInput& mixerInput(int _track) const { return m_mixer[_track]; }

		// A track's LFO (LFO k belongs to track k; its speed, depth and mix are track parameters 21-23):
		// destination track and parameter (0-23), two shapes, type (bit 0 TRIG: restart on the track's trigger,
		// bit 1 HOLD: output only updated at a trigger; 0 = FREE).
		void setLfo(int _track, int _destTrack, int _destParam, int _shape1, int _shape2, int _type);

		// Control ticks: the MD runs its tick as fast as the ColdFire gets through it (~120 Hz measured in
		// gearmulator-md-mm, varying with load). Default: one tick every 11 blocks of 32 samples (125.3 Hz).
		void setBlocksPerTick(int _n) { m_blocksPerTick = _n; }

		// Render one 32-sample block of the 16 voices (runs a tick first when due).
		bool renderBlock(typename TVoices::Block& _out);

		// Optional stage timing (microseconds, accumulated): the OS tick + trigger updates, and the voice DSP.
		bool timingOn = false;
		double tickUs = 0, dspUs = 0, voiceLoopUs = 0, osCallsUs = 0;	// tickUs = voiceLoopUs (machine functions) + osCallsUs (smoothing, LFOs) + pokes
		void tick();
		void updateVoice(int _track);	// machine function on the voice's current array -> voice slot
		void updateMixer(int _track);	// the track's DSP1 words from its current array, level and velocity

		const uint16_t* voiceParams(int _track) const;	// the per-voice 24-value array after smoothing and LFO

	private:
		void silenceVoice(int _track);	// forces a voice to the OS's empty machine (GND--), for voice stealing
		void runMasterGroup(int _group, uint32_t* _words);	// the OS's master tail for one group, from the smoothed values and the tempo

		MachineRunner& m_os;
		TVoices& m_voices;
		std::array<std::array<uint8_t, kParams>, kTracks> m_raw{};
		std::array<uint8_t, kTracks> m_machine{};
		std::array<int, kTracks> m_pendingMachine{};	// -1 = none
		std::array<bool, kTracks> m_trigger{};
		std::array<uint8_t, kTracks> m_velocity{};	// last trigger's velocity (0 until triggered, as on the MD)
		std::array<bool, kTracks> m_accent{};
		std::array<bool, kTracks> m_mute{};
		std::array<uint8_t, kTracks> m_route{};
		std::array<MixerInput, kTracks> m_mixer{};
		int m_accentAmount = 0;
		std::array<uint8_t, master::kParams> m_masterRaw{};
		std::array<uint32_t, master::kWords> m_masterWords{};
		unsigned m_masterPending = 0;
		std::array<uint16_t, kParams> m_scratch{};
		int m_blocksPerTick = 11;
		int m_blockCount = 0;
		uint32_t m_tickCount = 0;
		int m_maxActive = kTracks * 2;
		std::array<bool, kTracks> m_silenceNext{};	// budget victims to silence on their next tick
		std::array<float, kTracks> m_costEma{};	// DSP instructions per block a track costs while it sounds (moving average), for group balancing
		std::vector<int> m_activeOrder;	// least- to most-recently-triggered; each track at most once (capacity kTracks, reserved)
		int m_releaseThreshold = -1, m_releaseBlocks = 0;	// silence release, off (see setSilenceRelease)
		std::array<int, kTracks> m_quiet{};	// consecutive quiet blocks per sounding voice
		uint32_t m_releases = 0;
	};
}
