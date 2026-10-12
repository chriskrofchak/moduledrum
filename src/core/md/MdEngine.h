// Machinedrum One's engine: the OS's own control code (HostModel + MachineRunner), the voice DSP (VoiceEngine,
// emulated) and the mixer DSP's per-track effects and mix as native C++ (TrackFx, Mixer), then the master effects
// (MasterFx: the mixer DSP's own master section on a second emulated DSP, behind the mix; optional). One 32-sample block
// at a time: all 16 tracks, the dry main mix, the reverb/delay sends, each track's own output and the master output.
#pragma once
#include <algorithm>
#include <chrono>
#include <array>
#include <memory>
#include <vector>

#include <type_traits>
#include <vector>
#include "HostModel.h"
#include "MachineRunner.h"
#include "MasterFx.h"
#include "Mixer.h"
#include "ParallelVoiceEngine.h"
#include "TrackFx.h"
#include "VoiceEngine.h"

namespace md::engine
{
	struct EngineOptions
	{
		// The master effects need a second emulated DSP (about 215 MB in an engine, 55 ms to build) and, per block, 12 us.
		// False: no MasterFx (Output::master stays zero); the dry outputs are the same either way.
		bool master = true;
	};

	// TVoices: VoiceEngine (default, one DSP2 instance) or ParallelVoiceEngine (splits the 16 voices across
	// threads - see its header and HANDOFF.md, "per-machine cost profiled"). Template so both share this same
	// code; defined inline here (not in Engine.cpp, which is now just an include of this header, kept so
	// existing build commands that name engine/Engine.cpp as a source file still work) so each consumer gets
	// its own instantiation without needing whole-class explicit instantiation, which would force every
	// TVoices to support every constructor overload below.
	template<class TVoices = VoiceEngine>
	class EngineT
	{
	public:
		static constexpr int kTracks = 16;
		static constexpr int kBlock = 32;

		using Options = EngineOptions;

		// _osImage: the OS file's section 0 (the ColdFire OS), decompressed. Constructs TVoices with just the
		// firmware (VoiceEngine's own constructor). The master effects are built when the firmware has them (MasterFx::available).
		EngineT(const fw::Firmware& _fw, std::vector<uint8_t> _osImage, Options _options = {})
			: m_tables(_fw), m_mixer(m_tables)
		{
			m_os = std::make_unique<MachineRunner>(std::move(_osImage));
			m_voices = std::make_unique<TVoices>(_fw);
			init(_fw, _options);
		}

		// As above, but forwards _voicesArg to TVoices' constructor too (e.g. ParallelVoiceEngine's group
		// count). Only ever instantiated for a TVoices whose constructor actually takes (firmware, _voicesArg).
		template<class TVoicesArg>
		EngineT(const fw::Firmware& _fw, std::vector<uint8_t> _osImage, TVoicesArg _voicesArg, Options _options = {})
			: m_tables(_fw), m_mixer(m_tables)
		{
			m_os = std::make_unique<MachineRunner>(std::move(_osImage));
			m_voices = std::make_unique<TVoices>(_fw, _voicesArg);
			init(_fw, _options);
		}

		~EngineT() = default;

		HostModel<TVoices>& host() { return *m_host; }
		const MachineRunner& os() const { return *m_os; }
		TVoices& voices() { return *m_voices; }

		// The voice DSP's 16 outputs of the last render, before the per-track effects (what the mixer DSP's own loop takes in:
		// tests/Dsp1Reference runs that loop on them)
		const typename TVoices::Block& voiceOutput() const { return m_voiceOut; }

		// The master effects, or null (Options::master false, or a firmware without the section)
		MasterFx* master() { return m_master.get(); }
		bool hasMaster() const { return m_master != nullptr; }

		// Settle the master section at the host's current master bytes and tempo: HostModel::primeMasterFx(), the words to the
		// DSP at once and the echo's delay offset snapped to its target. Use it after loading a kit (or setting the tempo) when
		// the sound should start at the new settings and not glide there as the hardware does. Between two blocks.
		void primeMaster()
		{
			m_host->primeMasterFx();
			flushMasterWords();
			if(m_master)
				m_master->snapEcho();
		}

		struct Output
		{
			Mixer::Output mix;													// main, sends, individual outputs
			std::array<std::array<int32_t, kBlock>, kTracks> tracks{};			// each track after its effects
			// The master effects' stereo output (frame channels 2 and 5 after the master pass: the dry main mix through the
			// echo, reverb, EQ and dynamix, plus tracks routed to outputs A and B), 24-bit. Not level-neutral against mix.main.
			// Zero without a master section; the other four channels of the output frame are mix.frame's.
			Mixer::Stereo master{};
		};

		// Optional stage timing (microseconds, accumulated): track effects, and the mix. The OS tick and the voice DSP
		// are HostModel's (host().tickUs / dspUs).
		uint32_t dryMute = 0;	// tracks kept out of the dry main mix (see Mixer::process); set by the caller between renders
		// true: the master pass is not run (12 us a block saved) and Output::master is silent. The DSP stands where it was and
		// the host's words keep reaching it, so switching back on continues from the old echo and reverb contents, no glide.
		bool masterBypass = false;
		bool timingOn = false;
		double fxUs = 0, mixUs = 0, masterUs = 0;

		static constexpr bool kParallel = std::is_same_v<TVoices, ParallelVoiceEngine>;

		// Word sets the master DSP is warmed up with when the engine is made (MasterFx::warmUp): 140 blocks of synthetic signal
		// each, about 1 ms per set once compiled, and the compile itself
		static constexpr int kMasterWarmUpSets = 24;

		// One track's effects: a settled silent track's chain gives the same again while its input stays silent and
		// its state (which holds its params) stays the same, so it is skipped. Exact - see HANDOFF.md, "fixed
		// per-block cost". Touches only track _t's state, so tracks can run on different threads (each with its own
		// TrackFx: its scratch buffers are per instance).
		void fxTrack(TrackFx& _fx, const int _t, const int32_t* _src, int32_t* _dst)
		{
			TrackFx::setParams(m_state[_t], m_host->mixerInput(_t).fx.data());
			const bool silentIn = std::all_of(_src, _src + kBlock, [](int32_t s) { return s == 0; });
			if(skipSettled && silentIn && m_settled[_t] && m_state[_t].y == m_settledState[_t].y)
			{
				std::fill(_dst, _dst + kBlock, 0);
				return;
			}
			const auto before = m_state[_t];
			_fx.process(m_state[_t], _src, _dst);
			m_settled[_t] = silentIn && m_state[_t].y == before.y && std::all_of(_dst, _dst + kBlock, [](int32_t s) { return s == 0; });
			if(m_settled[_t])
				m_settledState[_t] = m_state[_t];
		}

		bool render(Output& _out)
		{
			m_host->timingOn = timingOn;
			m_out = &_out;
			if(!m_host->renderBlock(m_voiceOut))
				return false;
			const auto tf0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
			std::array<const int32_t*, kTracks> in{};
			std::array<std::array<uint32_t, 5>, kTracks> mix{};
			for(int t = 0; t < kTracks; ++t)
			{
				// Parallel: the groups' threads already ran their tracks' effects (see init()).
				if constexpr(!kParallel)
					fxTrack(*m_fxs[0], t, m_voiceOut[t].data(), _out.tracks[t].data());
				in[t] = _out.tracks[t].data();
				mix[t] = m_host->mixerInput(t).mix;
			}
			if(!timingOn)
			{
				m_mixer.process(in.data(), mix.data(), _out.mix, dryMute);
				return renderMaster(_out);
			}
			const auto tf1 = std::chrono::steady_clock::now();
			m_mixer.process(in.data(), mix.data(), _out.mix, dryMute);
			const auto tf2 = std::chrono::steady_clock::now();
			const bool ok = renderMaster(_out);
			const auto tf3 = std::chrono::steady_clock::now();
			fxUs += std::chrono::duration<double, std::micro>(tf1 - tf0).count();
			mixUs += std::chrono::duration<double, std::micro>(tf2 - tf1).count();
			masterUs += std::chrono::duration<double, std::micro>(tf3 - tf2).count();
			return ok;
		}

		// The voice DSP's fault, else the master DSP's
		const std::string& fault() const { return m_voices->faultReason().empty() && m_master ? m_master->faultReason() : m_voices->faultReason(); }

		bool skipSettled = true;	// false: run every track's effects every block (for verifying the skip)

	private:
		// The words the OS worked out since the last block go to the master DSP (HostModel::takeMasterGroups), then the master pass
		void flushMasterWords()
		{
			const unsigned groups = m_host->takeMasterGroups();
			if(!m_master)
				return;
			for(int g = 0; g < master::kGroups; ++g)
				if(groups & (1u << g))
					m_master->writeGroup(g, m_host->masterWords().data() + master::kGroup[g].offset);
		}

		bool renderMaster(Output& _out)
		{
			if(!m_master)
				return true;
			flushMasterWords();
			if(masterBypass)
			{
				_out.master = {};
				return true;
			}
			return m_master->process(_out.mix, _out.master);
		}

		// Compile the master section ahead of the first live block, on settings that take its data-dependent paths (see
		// MasterFx::warmUp): synthetic master bytes through the OS's own parameter code, 24 x 140 blocks of synthetic signal
		void warmUpMaster()
		{
			std::vector<uint32_t> words(static_cast<size_t>(kMasterWarmUpSets) * master::kWords);
			m_host->masterWarmUpWords(kMasterWarmUpSets, words.data());
			m_master->warmUp(words.data(), kMasterWarmUpSets);
		}

		void init(const fw::Firmware& _fw, const Options& _options)
		{
			m_host = std::make_unique<HostModel<TVoices>>(*m_os, *m_voices);
			if(_options.master && MasterFx::available(_fw))
			{
				m_master = std::make_unique<MasterFx>(_fw);
				warmUpMaster();
				flushMasterWords();		// the host primed its words when it was made: all four groups are due
			}
			for(auto& s : m_state)
				TrackFx::init(s);
			m_fxs.push_back(std::make_unique<TrackFx>(m_tables));
			if constexpr(kParallel)
			{
				// Each group's thread runs the effects of its own tracks right after its voices render.
				for(int g = 1; g < m_voices->groupCount(); ++g)
					m_fxs.push_back(std::make_unique<TrackFx>(m_tables));
				m_voices->setPost([this](int _g, const typename TVoices::Block& _blk)
				{
					for(int t = 0; t < kTracks; ++t)
						if(m_voices->groupOf(t) == _g)
							fxTrack(*m_fxs[static_cast<size_t>(_g)], t, _blk[t].data(), m_out->tracks[t].data());
				});
			}
		}

		std::unique_ptr<MachineRunner> m_os;
		std::unique_ptr<TVoices> m_voices;
		std::unique_ptr<HostModel<TVoices>> m_host;
		std::unique_ptr<MasterFx> m_master;
		TrackFx::Tables m_tables;
		std::vector<std::unique_ptr<TrackFx>> m_fxs;	// one per voice group (one when not parallel)
		Output* m_out = nullptr;
		Mixer m_mixer;
		std::array<TrackFx::State, kTracks> m_state{};
		std::array<TrackFx::State, kTracks> m_settledState{};
		std::array<bool, kTracks> m_settled{};
		typename TVoices::Block m_voiceOut{};
	};

	using Engine = EngineT<VoiceEngine>;
	// Constructed with (firmware, osImage, groupCount) - see ParallelVoiceEngine.h.
	using ParallelEngine = EngineT<ParallelVoiceEngine>;
}
