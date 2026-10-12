// The oracle for the master effects (.clew increment 0009): the mixer DSP's (DSP1) whole main loop, its own program running
// unchanged in the dsp56300 emulator. One block takes the 16 voice blocks and the OS's per-track effect and mix words in,
// runs the 16 track functions (TrackFx), the mix and the master section as the hardware's loop does, and leaves the
// 6-channel output frame and the master section's state in DSP memory. The engine's MasterFx runs only the master section
// (P:$342 on), fed with the native TrackFx and Mixer's output; the tests compare the two, word for word. (Approach A of the
// spike; approach A', the engine's, is the same code run from $342.)
//
// What the hardware's loop waits for is replaced, as in the spike: the codec DMA position ($3c: always the X:$100/$400
// half), the voice link DMA ($79, $7d: the host fills Y:$600 + $20 k), the track function's timing pad ($25e), and the block
// end ($9a8, bra $3c: a stub sends one host word and the host starts the next block at $3c itself). Not engine code, so it
// lives here and not in src/core.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "Firmware.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }

namespace md::test
{
	class Dsp1Reference
	{
	public:
		static constexpr int kTracks = 16, kBlock = 32;
		using Voices = std::array<std::array<int32_t, kBlock>, kTracks>;		// the voice DSP's output (24-bit signed)
		using FxWords = std::array<std::array<uint32_t, 9>, kTracks>;			// HostModel::MixerInput::fx
		using MixWords = std::array<std::array<uint32_t, 5>, kTracks>;			// HostModel::MixerInput::mix

		// Boots DSP1 (its init, 22 ms) and installs the harness. _interpreter: dsp56300's interpreter even in a JIT build.
		explicit Dsp1Reference(const fw::Firmware& _fw, bool _interpreter = false);
		~Dsp1Reference();

		// The loop's first pass, on silence and the init's default parameters (the pass the hardware runs before any
		// parameter reached the DSP). Call once, before the first words; MasterFx::process on silence is its counterpart.
		bool startUp();

		// One block of the loop. False on a fault (see fault()).
		bool runBlock(const Voices& _voices, const FxWords& _fx, const MixWords& _mix);

		uint32_t x(uint32_t _addr) const;
		uint32_t y(uint32_t _addr) const;
		void setY(uint32_t _addr, uint32_t _value);
		const std::string& fault() const { return m_fault; }

	private:
		bool pass(uint32_t _pc);
		bool step();

		const bool m_interpreter;
		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		std::string m_fault;
	};
}
