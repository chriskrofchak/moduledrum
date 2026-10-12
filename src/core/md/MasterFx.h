// The Machinedrum's master effects (rhythm echo, gate box reverb, master EQ, dynamix) by running the mixer DSP's (DSP1)
// own program, master section only, on a second emulated DSP56303. Everything before the master section (the per-track
// effects and the mix) is the native TrackFx and Mixer; this class takes the Mixer's dry main mix, reverb send, delay
// send and 6-channel output frame for one 32-sample block, runs DSP1's master pass (OS section 2, P:$342-$9a7) over them
// and hands back the stereo master output (frame channels 2 and 5 after the pass). Exact by construction: the original
// program, unchanged, with a harness around it that replaces what the hardware's loop supplies. See .clew increment 0009
// (section 3 there is the boot experiment this follows).
//
// The harness patches no word of the master code. DSP1 boots (init at P:$24 -> $100000, 0.8 M instructions), DMA0 is
// switched off (its block-complete interrupt feeds a deadline watchdog that halts the DSP), the interrupts are masked and
// each block starts at P:$342; the program's block end ($9a8, bra $3c) is redirected to a stub that sends one host word,
// and the host stops there. The parameters (38 words at Y:$150-$18c) are the ColdFire's job on the hardware; here
// HostModel computes them with the OS's own code and the caller hands them to setWords()/writeGroup().
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "Firmware.h"
#include "Mixer.h"

namespace dsp56k { class DSP; class Memory; class Peripherals56303; class PeripheralsNop; class DefaultMemoryValidator; }

namespace md::engine
{
	namespace master
	{
		constexpr int kParams = 32;		// a kit's master bytes: reverb 0-7, echo 8-15, EQ 16-23, dynamix 24-31 (the kit dump's order)
		constexpr int kWords = 38;		// the DSP words the OS computes from them
		constexpr int kGroups = 4;

		// The dry mix comes out of the master pass this many frames (of 44.1 kHz) late, whatever the settings (the dynamix reads
		// the block back from its 96-frame look-ahead ring): later than the track buses and the send buses, which do not pass
		// through it. A plugin that plays them side by side delays those by as much.
		constexpr int kDryLatency = 18;

		// The OS sends the words in four groups, one per control tick (tick counter & 3), each into a stretch of DSP1's Y
		// memory. Index = the OS's group number; offset = where the group's words sit in the 38-word array, which runs in
		// ascending Y address: echo Y:$150-$158, EQ $170-$179, dynamix $17a-$184, reverb $185-$18c.
		struct Group { uint32_t y; int offset; int count; };
		inline constexpr Group kGroup[kGroups] = {
			{0x150, 0, 9},		// 0 rhythm echo
			{0x17a, 19, 11},	// 1 dynamix
			{0x170, 9, 10},		// 2 master EQ
			{0x185, 30, 8},		// 3 gate box reverb
		};

		// A cleared kit's master effects on the MD (OS 1.63): gate box reverb, rhythm echo (TIME 32, LEV 96 ...), EQ flat,
		// dynamix open
		inline constexpr std::array<uint8_t, kParams> kDefaultParams = {
			0, 0, 64, 0, 0, 127, 127, 96,
			32, 0, 32, 0, 0, 127, 0, 96,
			64, 64, 64, 64, 64, 64, 64, 127,
			127, 127, 127, 127, 127, 127, 0, 0,
		};
	}

	class MasterFx
	{
	public:
		static constexpr int kBlockFrames = 32;
		using Stereo = Mixer::Stereo;	// [frame][L, R], 24-bit

		// Whether the firmware has the master section (OS section 2 with the code at P:$342)
		static bool available(const fw::Firmware& _fw);

		// Loads the program, installs the harness and runs DSP1's own init (22 ms). Throws on failure. The Firmware must
		// outlive this object. _interpreter: run dsp56300's interpreter even in a JIT build (the reference for the tests
		// that compare the two; a build without a JIT always interprets).
		explicit MasterFx(const fw::Firmware& _fw, bool _interpreter = false);
		~MasterFx();
		MasterFx(const MasterFx&) = delete;
		MasterFx& operator=(const MasterFx&) = delete;

		// Back to the state after construction (the program reloaded, DSP1's init run again, no words): the echo and the
		// reverb are empty. A compiled JIT block stays, so this is also how warmUp() leaves the object.
		void reset();

		// The master parameter words (master::kWords, in master::kGroup order and ascending Y address) or one group of
		// them (the OS's group number, master::kGroup[g].count words), written into DSP1's Y memory now, which is between
		// two blocks. The first echo words after construction or reset() also set the echo's delay offset (Y:$15a) to its
		// target, so the echo does not glide in from DSP1's power-on offset (the hardware does, for a second or so).
		void setWords(const uint32_t* _words);
		void writeGroup(int _group, const uint32_t* _words);

		// Y:$15a = Y:$150 again: the echo's delay offset snaps to its target instead of gliding there (a tempo or TIME
		// change already running its glide stops; whatever the echo holds stays)
		void snapEcho();

		// One block: _in = the Mixer's output (dry main, reverb and delay sends, frame); _out = the master L/R. Frame
		// channels 2 and 5 are the master pass's output (it adds the processed main mix to what the mixer put there:
		// tracks routed to outputs A and B); the other four channels do not pass through it and stay _in.frame's. False on
		// a fault (the DSP did not reach the end of its block, or the JIT failed): sticky until reset().
		bool process(const Mixer::Output& _in, Stereo& _out);

		// Compile the program ahead of the first live block: runs _sets word sets (master::kWords each) over a fixed
		// synthetic signal (noise rising or falling through every level, steady noise, squares, impulses and silence, a kind per
		// bus and set; 140 blocks per set), long enough to take the data-dependent paths (echo glide up and down, gate open
		// and closed, dynamix above and below its threshold and in its knee ...) so that dsp56300's JIT has compiled every
		// block, then puts DSP1's memory back
		// as it was (the instance is as a fresh one: no words, empty echo and reverb) and keeps the compiled code. Without
		// this the JIT compiles a block (~100 us to 14 ms) the first time the program reaches it, on whatever thread
		// renders. Off the audio thread: a few ms per set and a 10 MB scratch copy of DSP memory. The word sets should
		// cover the extremes of the parameters (HostModel::computeMasterWords makes valid ones from kit bytes).
		void warmUp(const uint32_t* _wordSets, int _sets);

		// Entry points of the JIT blocks compiled in the program's address range so far (0 for the interpreter): a live block
		// that adds one is the stall warmUp() is there to avoid
		size_t compiledBlocks() const;

		// Y memory of DSP1, for tests and tools (the whole master state lives in Y:$000-$1ff and the X rings)
		uint32_t readY(uint32_t _addr) const;
		uint32_t readX(uint32_t _addr) const;
		dsp56k::DSP& dsp() { return *m_dsp; }

		const std::string& faultReason() const { return m_fault; }
		uint64_t instructionsLastBlock() const { return m_lastInstructions; }
		uint64_t blocks() const { return m_blocks; }

	private:
		void configureJit();
		void loadImage();
		void installStub();
		bool step();		// one exec() of the DSP (the JIT's or the interpreter's); false on a JIT failure
		bool runPass();

		const fw::Firmware& m_fw;
		const bool m_interpreter;
		std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
		std::unique_ptr<dsp56k::Memory> m_mem;
		std::unique_ptr<dsp56k::Peripherals56303> m_periphX;
		std::unique_ptr<dsp56k::PeripheralsNop> m_periphY;
		std::unique_ptr<dsp56k::DSP> m_dsp;
		uint32_t* m_x = nullptr;	// the DSP's X memory: the block's inputs and outputs are poked and read here
		uint64_t m_lastInstructions = 0;
		uint64_t m_blocks = 0;
		bool m_primed = false;		// Y:$15a has been set to the first echo words' target
		std::string m_fault;
	};
}
