// See MasterFx.h. The harness is the one of the master-effects research spike (.clew increment 0009, section 3), whose
// results (boot, JIT = interpreter, master-only = the whole DSP1 loop) the tests in tests/test_master.cpp reproduce.
#include "MasterFx.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "Dsp56.h"
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::engine
{
	using namespace dsp56k;

	namespace
	{
		// DSP1's memory as gearmulator-md-mm models the Machinedrum, the same as VoiceEngine's DSP2 (external SRAM from $020000
		// shared by P, X and Y; the master section's tables and rings sit at $100000-$143e7f)
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x800000, kBridge = 0x020000;

		// DSP1's state after the ColdFire's first-stage loader: the memory map (AAR0-3) and OMR (MixerRef's value; DSP2's is $00498d)
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00490d;

		constexpr TWord kEntry = 0x24;			// reset vector area: jsr $100000 (init), then on to the main loop
		constexpr TWord kInitDone = 0x2d;		// where the init returns to
		constexpr TWord kPass = 0x342;			// the master section: echo, reverb, sum, EQ, dynamix, add to the frame, meters
		constexpr TWord kPassEnd = 0x9a8;		// 'bra $3c': the main loop's block end
		constexpr TWord kStub = 0x0c00;			// free internal P RAM

		// X memory: the pass's buffers (Mixer::Output as DSP1 holds it)
		constexpr TWord kMain = 0x180, kRev = 0x1c0, kDel = 0x600, kFrame = 0x400;
		constexpr TWord kFramePtr = 0x640, kAdcPtr = 0x641;	// X:$640 = the frame's base, X:$641 = the codec ADC buffer the meters read
		constexpr TWord kFrameBase = 0x400, kAdcBase = 0x100;
		constexpr int kChL = 2, kChR = 5;	// the main outputs' channels in the 6-channel frame

		constexpr TWord kEchoOffset = 0x15a;	// echo delay offset: moves toward Y:$150 one sample per sample

		constexpr uint64_t kMaxInstrInit = 100'000'000;	// the init takes 0.82 M
		constexpr uint64_t kMaxInstrBlock = 2'000'000;	// a block takes 10 k

		constexpr int kWarmBlocks = 140;	// warm-up: blocks per word set
	}

	bool MasterFx::available(const fw::Firmware& _fw)
	{
		// the master section's code ($342-$9a9) comes in several records: every word of it must be there
		std::array<bool, kPassEnd + 2 - kPass> have{};
		for(const auto& r : _fw.dspB.records)
			if(r.space == fw::Space::P)
				for(size_t k = 0; k < r.words.size(); ++k)
				{
					const size_t a = r.addr + k;
					if(a >= kPass && a < kPassEnd + 2)
						have[a - kPass] = true;
				}
		return std::all_of(have.begin(), have.end(), [](const bool b) { return b; });
	}

	MasterFx::MasterFx(const fw::Firmware& _fw, const bool _interpreter) : m_fw(_fw), m_interpreter(_interpreter)
	{
		if(!available(_fw))
			throw std::runtime_error("the OS file has no master effects section (DSP program 2)");
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		if(g_useJIT && !m_interpreter)
			configureJit();
		else
			m_dsp->setInterpreterEnabled(true);	// the interpreter's per-address opcode cache is only built on request
		m_x = m_mem->getMemAreaPtr(MemArea_X);

		// The init starts both ESSI ports (the codec and the link to the voice DSP): feed silence, discard output, never block
		for(auto* essi : {&m_periphX->getEssi0(), &m_periphX->getEssi1()})
		{
			essi->setReadRxCallback([](uint64_t& _frameIndex, Audio::RxFrame& _frame)
			{
				_frame.resize(2);
				_frame[0] = Audio::RxSlot{};
				_frame[1] = Audio::RxSlot{};
				++_frameIndex;
			});
			essi->setWriteTxCallback([](uint64_t& _frameIndex, const Audio::TxFrame&) { ++_frameIndex; });
		}
		reset();
	}

	MasterFx::~MasterFx() = default;

	void MasterFx::configureJit()
	{
		// As VoiceEngine::configureJit(): DSP1's program too keeps ordinary code in the interrupt vector area (its main loop
		// is at $24-$a3), so a block there is a fast-interrupt block only while the DSP is in a fast interrupt. Blocks return
		// to exec() rather than jumping into each other (the pass ends at the stub's host word, which exec()'s caller must
		// see right after the block that sent it).
		auto cfg = m_dsp->getJit().getConfig();
		cfg.interruptRegionIsCode = true;
		cfg.dynamicFastInterrupts = true;
		cfg.aguSupportBitreverse = true;
		cfg.linkJitBlocks = false;
		m_dsp->getJit().setConfig(cfg);
	}

	void MasterFx::loadImage()
	{
		for(const auto& r : m_fw.dspB.records)
		{
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				// P space, and anything in the shared external range, must go through memWriteP so the JIT sees it
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		}
	}

	void MasterFx::installStub()
	{
		Assembler as;
		TWord pc = kStub;
		auto emit = [&](const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("master harness: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(pc++, r.word[1]);
		};
		std::ostringstream wait;
		wait << "jclr #0,x:<<$ffffc3,$" << std::hex << kStub + 2;
		emit("movep #>1,x:<<$ffffc7");	// "block done" to the host (the host stops right here)
		emit(wait.str());				// the real loop would wait for the host's "go" word ...
		emit("movep x:<<$ffffc6,a");
		emit("jmp $3c");				// ... and run the next block (the host starts each pass at P:$342 itself instead)

		m_dsp->memWriteP(kPassEnd, 0x0af080);		// jmp >kStub (2 words, as the bra it replaces)
		m_dsp->memWriteP(kPassEnd + 1, kStub);

		auto& hi = m_periphX->getHI08();
		hi.setRXRateLimit(0);
		hi.setTransmitDataAlwaysEmpty(true);
		hi.clearRX();
		while(hi.hasTX())
			hi.readTX();
	}

	void MasterFx::reset()
	{
		m_fault.clear();
		m_primed = false;
		m_dsp->resetHW();
		loadImage();
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;

		// The init (jsr $100000 from the reset vector area) comes back to $2d, where the real program goes on to wait for its
		// DMA; park it there
		m_dsp->memWriteP(kInitDone, 0x0c0000 | kInitDone);	// jmp $2d
		m_dsp->setPC(kEntry);
		const auto start = m_dsp->getInstructionCounter();
		while(m_dsp->getPC().toWord() != kInitDone)
		{
			if(!step())
				throw std::runtime_error("master DSP init: " + m_fault);
			if(m_dsp->getInstructionCounter() - start > kMaxInstrInit)
			{
				std::ostringstream o;
				o << "master DSP init did not complete: instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord();
				m_fault = o.str();
				throw std::runtime_error(m_fault);
			}
		}
		// The block-complete interrupt of DMA0 (codec input) feeds a deadline watchdog that halts the DSP if the loop is late
		m_dsp->memWrite(MemArea_X, 0xffffec, 0);
		installStub();
	}

	void MasterFx::setWords(const uint32_t* _words)
	{
		for(int g = 0; g < master::kGroups; ++g)
			writeGroup(g, _words + master::kGroup[g].offset);
	}

	void MasterFx::writeGroup(const int _group, const uint32_t* _words)
	{
		const auto& g = master::kGroup[_group];
		for(int i = 0; i < g.count; ++i)
			m_dsp->memWrite(MemArea_Y, g.y + static_cast<TWord>(i), _words[i] & 0xffffff);
		if(_group == 0 && !m_primed)
		{
			snapEcho();
			m_primed = true;
		}
	}

	void MasterFx::snapEcho()
	{
		m_dsp->memWrite(MemArea_Y, kEchoOffset, readY(master::kGroup[0].y));
	}

	uint32_t MasterFx::readY(const uint32_t _addr) const { return m_mem->get(MemArea_Y, _addr) & 0xffffff; }
	uint32_t MasterFx::readX(const uint32_t _addr) const { return m_mem->get(MemArea_X, _addr) & 0xffffff; }

	bool MasterFx::step()
	{
		if(m_interpreter)
			m_dsp->execInterpreter();
		else
			m_dsp->exec();
		if constexpr(g_useJIT)
		{
			// A block the JIT could not generate (no JIT memory) is a sticky failure: the DSP makes no progress
			if(!m_interpreter && m_dsp->getJit().hasFailed())
			{
				m_fault = "DSP JIT failed: " + m_dsp->getJit().failReason();
				return false;
			}
		}
		return true;
	}

	bool MasterFx::runPass()
	{
		// Every pass starts clean: the loop's address modifiers linear ($ffffff) and interrupts masked (the JIT's blocks,
		// and the program's own, never rely on a peripheral interrupt in the middle of it)
		for(int i = 0; i < 8; ++i)
			m_dsp->writeReg(static_cast<EReg>(Reg_M0 + i), TReg24(0xffffff));
		m_dsp->writeReg(Reg_SR, TReg24(m_dsp->getSR().toWord() | 0x300));
		auto& hi = m_periphX->getHI08();
		while(hi.hasTX())
			hi.readTX();
		m_dsp->setPC(kPass);
		const auto start = m_dsp->getInstructionCounter();
		while(!hi.hasTX())
		{
			if(!step())
				return false;
			if(m_dsp->getInstructionCounter() - start > kMaxInstrBlock)
			{
				std::ostringstream o;
				o << "master DSP: instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord();
				m_fault = o.str();
				return false;
			}
		}
		while(hi.hasTX())
			hi.readTX();
		m_lastInstructions = m_dsp->getInstructionCounter() - start;
		return true;
	}

	bool MasterFx::process(const Mixer::Output& _in, Stereo& _out)
	{
		if(!m_fault.empty())
			return false;
		auto* x = m_x;
		for(int f = 0; f < kBlockFrames; ++f)
		{
			const auto u = static_cast<size_t>(f);
			for(int c = 0; c < 2; ++c)
			{
				const auto k = static_cast<size_t>(c);
				const TWord a = static_cast<TWord>(2 * f + c);
				x[kMain + a] = d56::u24(_in.main[u][k]);
				x[kRev + a] = d56::u24(_in.rev[u][k]);
				x[kDel + a] = d56::u24(_in.del[u][k]);
			}
			// the pass adds the master output to channels 2 and 5 of the frame, where the mixer put tracks routed to outputs A and B
			x[kFrame + 6 * static_cast<TWord>(f) + kChL] = d56::u24(_in.frame[u][kChL]);
			x[kFrame + 6 * static_cast<TWord>(f) + kChR] = d56::u24(_in.frame[u][kChR]);
		}
		x[kFramePtr] = kFrameBase;
		x[kAdcPtr] = kAdcBase;
		if(!runPass())
			return false;
		for(int f = 0; f < kBlockFrames; ++f)
		{
			const auto u = static_cast<size_t>(f);
			_out[u][0] = d56::sx24(x[kFrame + 6 * static_cast<TWord>(f) + kChL]);
			_out[u][1] = d56::sx24(x[kFrame + 6 * static_cast<TWord>(f) + kChR]);
		}
		++m_blocks;
		return true;
	}

	namespace
	{
		// What warmUp() saves and puts back: the DSP's memory in the three views (the shared external range once, in X's
		// view). The master pass writes its rings, filter states and scratch there and nowhere else.
		constexpr TWord kSnapshotX = 0x200000;
	}

	void MasterFx::warmUp(const uint32_t* _wordSets, const int _sets)
	{
		// the memory as it is now, the state of a fresh instance (the program's init has run, no words, no block)
		std::vector<TWord> savedX(kSnapshotX), savedY(kBridge), savedP(kBridge);
		for(TWord a = 0; a < kSnapshotX; ++a)
			savedX[a] = m_mem->get(MemArea_X, a);
		for(TWord a = 0; a < kBridge; ++a)
		{
			savedY[a] = m_mem->get(MemArea_Y, a);
			savedP[a] = m_mem->get(MemArea_P, a);
		}

		uint32_t seed = 0x2545f491;
		auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<int32_t>(seed) >> 8; };	// 24-bit signed noise
		Mixer::Output in;
		Stereo out;
		bool ok = true;
		for(int s = 0; s < _sets && ok; ++s)
		{
			setWords(_wordSets + s * master::kWords);
			// kWarmBlocks per set: silence (everything settles and closes), 80 blocks of signal, silence again (the gate
			// closes, the envelopes release). Each bus draws its own kind of signal per set, so that the dry mix and the two
			// sends (and with them the echo and reverb returns the dynamix sees) meet in many combinations: noise rising from
			// -102 dB to full scale or falling again (every level between: the reverb's gate, the dynamix's detector and its
			// gain computer, whatever their thresholds), steady noise, full-scale and eighth-scale squares, impulses, silence.
			int kind[3];
			for(auto& k : kind)
				k = static_cast<int>((static_cast<uint32_t>(rnd()) >> 3) % 8);
			for(int b = 0; b < kWarmBlocks && ok; ++b)
			{
				const bool active = b >= 20 && b < 100;
				const float t = static_cast<float>(b - 20) / 80.0f;
				for(int f = 0; f < kBlockFrames; ++f)
				{
					const auto u = static_cast<size_t>(f);
					for(size_t c = 0; c < 2; ++c)
					{
						auto gen = [&](const int _kind) -> int32_t
						{
							if(!active)
								return 0;
							const auto noise = [&](const float _amp) { return static_cast<int32_t>(static_cast<float>(rnd()) * _amp); };
							switch(_kind)
							{
								case 0: return noise(std::exp2(-17.0f * (1.0f - t)));		// rising
								case 1: return (f & 8) ? 0x7fffff : -0x800000;
								case 2: return (f & 8) ? 0x100000 : -0x100000;
								case 3: return noise(0.1f);
								case 4: return noise(1.0f);
								case 5: return (b % 5 == 0 && f == 0) ? 0x600000 : 0;
								case 7: return noise(std::exp2(-17.0f * t));				// falling
								default: return 0;
							}
						};
						in.main[u][c] = gen(kind[0]);
						in.rev[u][c] = gen(kind[1]);
						in.del[u][c] = gen(kind[2]);
					}
					for(size_t c = 0; c < 6; ++c)
						in.frame[u][c] = (c == 2 || c == 5) && (s & 2) && active ? rnd() >> 5 : 0;
				}
				ok = process(in, out);
			}
		}

		// put the memory back; a word that differs goes through the DSP's own write, so that a program word (there should
		// be none) would invalidate the JIT's blocks
		for(TWord a = 0; a < kSnapshotX; ++a)
			if(m_mem->get(MemArea_X, a) != savedX[a])
				m_dsp->memWrite(MemArea_X, a, savedX[a]);
		for(TWord a = 0; a < kBridge; ++a)
		{
			if(m_mem->get(MemArea_Y, a) != savedY[a])
				m_dsp->memWrite(MemArea_Y, a, savedY[a]);
			if(m_mem->get(MemArea_P, a) != savedP[a])
				m_dsp->memWriteP(a, savedP[a]);
		}
		m_primed = false;
		m_blocks = 0;
		m_lastInstructions = 0;
	}

	size_t MasterFx::compiledBlocks() const
	{
		if constexpr(g_useJIT)
		{
			if(m_interpreter)
				return 0;
			// the DSP's table of block entry points: a P address nothing was compiled at points at the JIT's "create" stub; the
			// program lives below $c10, nothing runs at $fff
			const auto* entries = m_dsp->getJitEntries();
			size_t n = 0;
			for(TWord pc = 0; pc < 0xfff; ++pc)
				n += entries[pc] != entries[0xfff];
			return n;
		}
		return 0;
	}
}
