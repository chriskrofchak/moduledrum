// Harness structure adapted from shnolk/monomodule src/core/dsp/DspEngine.cpp (AGPL-3.0).
#include "VoiceEngine.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

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
		// Memory as gearmulator-md-mm models the Machinedrum: external SRAM from $020000 is shared by P, X and Y.
		// P is 2M words, not the DSP's 8M: the MD's map stays below $200000 (samples end at $18fc12), and the interpreter's
		// per-word opcode cache made the full size cost ~300 MB of RAM in MPC. Output is identical.
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x800000, kBridge = 0x020000;

		// DSP2 state after the ColdFire's first-stage loader, read from a running gearmulator-md-mm (OS 1.63):
		// the full SPS-1 UW memory map (AAR0-3) and OMR.
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00498d;

		// Program addresses (OS 1.63 voice program, see docs/PROTOCOL.md)
		constexpr TWord kEntry = 0x24;			// after the vector table: init, then bra $64
		constexpr TWord kHostGroupTx = 0x73;	// movep a1,x:HTX every 4 voices (ColdFire pacing): removed
		constexpr TWord kRenderFn = 0xa8;		// r1 = y:(r0+$145c77): render fn address for the voice's machine
		constexpr TWord kAfterRenderFn = 0xaa;	// resumes normal flow after the render-fn lookup
		constexpr TWord kAfterRender = 0xb5;	// voice rendered into Y:(y:$140); original: frame sync + link DMA
		constexpr TWord kNextVoice = 0xd5;		// advance to the next voice
		constexpr TWord kEndOfBlock = 0xe2;		// all 16 voices done; original: RAM-R input position, bra $64
		constexpr TWord kBlockStart = 0x64;
		constexpr TWord kStub = 0x0c00;			// free internal P RAM (the program's internal P ends at $3e2)
		constexpr TWord kSkipStub = 0x0c20;	// silent-voice check, replaces kRenderFn's lookup
		constexpr TWord kSkipNormal = 0x0c30;	// real render fn lookup, resumed
		constexpr TWord kSkipSilent = 0x0c38;	// idle voice: flag 0, straight to the next voice

		constexpr uint64_t kMaxInstrInit = 200'000'000;
		constexpr uint64_t kMaxInstrBlock = 20'000'000;
	}

	VoiceEngine::VoiceEngine(const fw::Firmware& _fw, const bool _boot) : VoiceEngine(_fw, _boot, false)
	{
	}

	// _shadow: the lockstep shadow (MD_JIT_LOCKSTEP), the same DSP run by the interpreter; its owner steps and boots it.
	VoiceEngine::VoiceEngine(const fw::Firmware& _fw, const bool _boot, const bool _shadow) : m_fw(_fw), m_isShadow(_shadow)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		if(g_useJIT && !m_isShadow)
		{
			configureJit();
			if(std::getenv("MD_JIT_LOCKSTEP"))
				m_shadow.reset(new VoiceEngine(_fw, false, true));
		}
		else
		{
			// The interpreter (no JIT on this target or MD_USE_JIT=OFF, or the lockstep shadow): exec() falls back to
			// it, and its per-address opcode cache this fork only builds on request (it costs ~48 bytes x P size, moot
			// when the JIT is the normal path). Without this, the cache is empty and exec() dereferences a
			// null instruction-handler pointer on the very first instruction.
			m_dsp->setInterpreterEnabled(true);
		}
		m_periphX->getHI08().setRXRateLimit(0);
		m_periphX->getHI08().setTransmitDataAlwaysEmpty(true);

		// The program's init starts both ESSI ports (ESSI0 = link to the mixer DSP, ESSI1 = codec ADC). The
		// harness replaces the link and does not use the ADC: feed silence and discard output, never block.
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
		reset(_boot);
	}

	VoiceEngine::~VoiceEngine() = default;

	void VoiceEngine::configureJit()
	{
		auto cfg = m_dsp->getJit().getConfig();

		// The voice program keeps ordinary code in the interrupt vector area (init at P:$24, the whole block loop at
		// $64-$e5). Without interruptRegionIsCode the JIT compiles everything below $100 as 2-word fast-interrupt
		// blocks; there, JitOps::pushPCSR pushes the PC register instead of the return address, and an unconditional
		// branch's block never sets PC before its op runs: init's 'jsr >$100024' at $3b pushed $3b, its rts came back
		// to the jsr, and the init ran forever ("instruction budget exceeded at PC=$10008b"). With it, a block there is
		// a fast-interrupt block only while the DSP is in a fast interrupt (Monomodule's setting, for the Monomachine
		// kernel's code below $100).
		cfg.interruptRegionIsCode = true;
		cfg.dynamicFastInterrupts = true;	// as gearmulator-md-mm configures its DSPs
		cfg.aguSupportBitreverse = true;	// as gearmulator-md-mm configures its DSPs

		// Blocks return to exec() rather than jumping into each other: runUntilTx() sees each host word right after the
		// block that sent it. Linking did not measure faster here (md-hash 0.54 s either way).
		cfg.linkJitBlocks = false;

		// The two flag stubs are one-instruction blocks, so that exec() returns right after the flag's movep, as the
		// interpreter does: readBlock() then reads the instruction counter at the same point and the per-voice costs
		// (HostModel's voice-group balancing) are the interpreter's to the instruction. One more exec() call per voice.
		auto single = cfg;
		single.maxInstructionsPerBlock = 1;
		cfg.getBlockConfig = [single](const TWord _pc) -> std::optional<JitConfig>
		{
			if(_pc == kSkipNormal || _pc == kSkipSilent)
				return single;
			return std::nullopt;
		};
		m_dsp->getJit().setConfig(cfg);
	}

	void VoiceEngine::loadImage(const fw::DspImage& _img)
	{
		for(const auto& r : _img.records)
		{
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				// P space, and anything in the shared external range, must go through memWriteP so the JIT sees it.
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		}
	}

	void VoiceEngine::installHarness()
	{
		Assembler as;
		auto emit = [&](TWord& _pc, const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("voice harness: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(_pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(_pc++, r.word[1]);
		};
		auto hex = [](TWord _v) { std::ostringstream o; o << "$" << std::hex << _v; return o.str(); };

		// 0) the silent-voice render ($10008f: 32 zeros into Y:(r7)) pads its time with a 32 x 50 nop loop so
		//    the hardware's block timing is even; the output is the same without it
		m_dsp->memWriteP(0x100093, 0x000000);
		m_dsp->memWriteP(0x100094, 0x000000);

		// 1) no per-group handshake word to the host
		m_dsp->memWriteP(kHostGroupTx, 0x000000);

		// 2) after each voice's render: send its 32 samples (Y:(y:$140)) to the host, then continue
		{
			TWord pc = kAfterRender;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >stub
			m_dsp->memWriteP(pc++, kStub);
		}
		{
			TWord pc = kStub;
			emit(pc, "move y:>$140,r0");
			emit(pc, "move #>$ffffff,m0");
			emit(pc, "do #32," + hex(pc + 3));	// DO, not REP: REP over a peripheral access is unsafe in the JIT
			emit(pc, "movep y:(r0)+,x:<<$ffffc7");
			emit(pc, "nop");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >next voice
			m_dsp->memWriteP(pc++, kNextVoice);
		}

		// 2b) skip the machine call for idle voices (current machine code 0 = never assigned, or 1 = the
		//     empty machine GND--, both of which render 32 zeros, docs/PROTOCOL.md "per voice, every
		//     tick"): redirect r1 (the render fn about to be jsr'd at $b4) to a fast clear instead of the
		//     real GND-- function. r0 (current machine code) and r7 (buffer pointer, mod 32) are live here.
		{
			TWord pc = kRenderFn;
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kSkipStub
			m_dsp->memWriteP(pc++, kSkipStub);
		}
		{
			// jeq/jmp, not beq/bra: Bcc_xxxx's operand is a raw PC-relative displacement, not an address
			// (unlike jmp/jclr/jset elsewhere in this file); jeq is the short-absolute conditional form.
			TWord pc = kSkipStub;
			emit(pc, "move r0,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "move #>1,b");	// (cmp a,b here never matched code 1: an empty-machine voice kept rendering)
			emit(pc, "sub b,a");
			emit(pc, "tst a");
			emit(pc, "jeq " + hex(kSkipSilent));
			emit(pc, "add b,a");	// a back to the machine code, as the compare left it
			emit(pc, "jmp " + hex(kSkipNormal));
		}
		{
			TWord pc = kSkipNormal;
			emit(pc, "movep #>1,x:<<$ffffc7");	// this voice will render: flag 1, its 32 samples follow
			emit(pc, "move y:(r0+$145c77),r1");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kAfterRenderFn
			m_dsp->memWriteP(pc++, kAfterRenderFn);
		}
		{
			// An idle voice sends flag 0 and nothing else - no 32-word clear, no 32-word transmit; the host fills its
			// zeros. Its buffer pointer (mod 32) ends where it started, so going straight to the next voice is the same
			// state the clear-and-send path left. A voice's 32 samples are only sent when its flag is 1.
			TWord pc = kSkipSilent;
			emit(pc, "movep #>0,x:<<$ffffc7");
			m_dsp->memWriteP(pc++, 0x0af080);	// jmp >kNextVoice
			m_dsp->memWriteP(pc++, kNextVoice);
		}

		// 3) after all 16 voices: wait for the host's "go" word (the host updates the voice slots meanwhile)
		{
			TWord pc = kEndOfBlock;
			emit(pc, "jclr #0,x:<<$ffffc3," + hex(kEndOfBlock));
			emit(pc, "movep x:<<$ffffc6,a");
			emit(pc, "jmp " + hex(kBlockStart));
		}
	}

	void VoiceEngine::writeP(const uint32_t _addr, const uint32_t* _words, const size_t _count)
	{
		// memWriteP, not a plain memory write: it is what tells the JIT (and the interpreter's opcode cache) about P changes
		for(size_t i = 0; i < _count && _addr + i < m_mem->sizeP(); ++i)
			m_dsp->memWriteP(static_cast<TWord>(_addr + i), _words[i] & 0xffffff);
		if(m_shadow)
			m_shadow->writeP(_addr, _words, _count);
	}

	uint32_t VoiceEngine::readP(const uint32_t _addr) const
	{
		return _addr < m_mem->sizeP() ? m_mem->get(MemArea_P, _addr) & 0xffffff : 0;
	}

	void VoiceEngine::reset(const bool _boot)
	{
		m_fault.clear();
		m_dsp->resetHW();
		loadImage(m_fw.dspA);	// section 1 = voice program (docs/PROTOCOL.md)
		installHarness();
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;
		auto& hi = m_periphX->getHI08();
		hi.clearRX();
		while(hi.hasTX())
			hi.readTX();
		m_dsp->setPC(kEntry);
		if(m_shadow)
			m_shadow->reset(false);
		if(!_boot)
			return;

		// Init runs into the block loop; the first (silent) block comes out and the DSP then waits for "go".
		Block first;
		if(!readBlock(first, kMaxInstrInit))
			throw std::runtime_error("voice DSP init did not complete: " + m_fault);
		while(hi.hasTX())
			readTx();
	}

	void VoiceEngine::setSlot(const int _voice, const uint32_t* _words, const int _count)
	{
		const TWord base = kSlotBase + kSlotStride * static_cast<TWord>(_voice);
		for(int k = 0; k < _count; ++k)
			m_dsp->memWrite(MemArea_Y, base + static_cast<TWord>(k), _words[k] & 0xffffff);
		if(m_shadow)
			m_shadow->setSlot(_voice, _words, _count);
	}

	bool VoiceEngine::runUntilTx(const size_t _words, const uint64_t _maxInstructions)
	{
		auto& hi = m_periphX->getHI08();
		const auto start = m_dsp->getInstructionCounter();
		static const bool debug = std::getenv("MDV_DEBUG") != nullptr;
		uint64_t nextReport = start + 5'000'000;
		while(hi.txData().size() < _words)
		{
			const TWord pc = m_dsp->getPC().toWord();
			m_dsp->exec();
			if constexpr(g_useJIT)
			{
				// A block the JIT could not generate (no JIT memory) is a sticky failure: the DSP makes no progress.
				if(m_dsp->getJit().hasFailed())
				{
					m_fault = "DSP JIT failed: " + m_dsp->getJit().failReason();
					return false;
				}
				if(m_shadow)
					lockstep(pc);
			}
			if(debug && m_dsp->getInstructionCounter() > nextReport)
			{
				std::fprintf(stderr, "  PC=$%06x instr=%llu tx=%zu\n", m_dsp->getPC().toWord(),
					static_cast<unsigned long long>(m_dsp->getInstructionCounter() - start), hi.txData().size());
				nextReport += 5'000'000;
			}
			if(m_dsp->getInstructionCounter() - start > _maxInstructions)
			{
				std::ostringstream o;
				o << "instruction budget exceeded at PC=$" << std::hex << m_dsp->getPC().toWord() << std::dec
				  << " with " << hi.txData().size() << "/" << _words << " words";
				m_fault = o.str();
				return false;
			}
		}
		return true;
	}

	// One block from the DSP, voice by voice: a flag word (1 = it rendered, its 32 samples follow; 0 = idle, all zeros).
	// Every voice's slot is read before its flag goes out, so once the 16th flag is in the DSP has consumed every slot.
	bool VoiceEngine::readBlock(Block& _out, const uint64_t _maxInstructions)
	{
		m_lastActive = 0;
		uint64_t prev = m_dsp->getInstructionCounter();
		int idx = 0;
		for(auto& voice : _out)
		{
			if(!runUntilTx(1, _maxInstructions))
				return false;
			// The DSP reaches voice k's flag after finishing voice k-1's render: the instructions between two flags are
			// the previous voice's cost (an idle voice costs almost nothing).
			{
				const uint64_t now = m_dsp->getInstructionCounter();
				if(idx > 0) m_voiceInstr[static_cast<size_t>(idx - 1)] = static_cast<uint32_t>(now - prev);
				prev = now;
			}
			const TWord flag = readTx() & 0xffffff;
			if(flag == 1) ++m_lastActive;
			if(flag != 1)
			{
				voice.fill(0);
				++idx;
				continue;
			}
			if(!runUntilTx(voice.size(), _maxInstructions))
				return false;
			for(auto& s : voice)
			{
				const TWord t = readTx() & 0xffffff;
				s = static_cast<int32_t>(t << 8) >> 8;
			}
			++idx;
		}
		m_voiceInstr[kVoices - 1] = static_cast<uint32_t>(m_dsp->getInstructionCounter() - prev);
		return true;
	}

	bool VoiceEngine::renderBlock(Block& _out)
	{
		auto& hi = m_periphX->getHI08();
		const TWord go = 1;
		hi.writeRX(&go, 1);
		if(m_shadow)
			m_shadow->m_periphX->getHI08().writeRX(&go, 1);
		const auto start = m_dsp->getInstructionCounter();
		if(!readBlock(_out, kMaxInstrBlock))
			return false;
		m_lastInstructions = m_dsp->getInstructionCounter() - start;
		return true;
	}

	uint32_t VoiceEngine::readTx()
	{
		const TWord w = m_periphX->getHI08().readTX();
		if(m_shadow)	// the shadow has sent the same words by now (lockstep() keeps it at the same instruction count)
		{
			auto& shadowHi = m_shadow->m_periphX->getHI08();
			const TWord s = shadowHi.hasTX() ? shadowHi.readTX() : ~TWord(0);
			if(s != w)
			{
				std::ostringstream o;
				o << "JIT lockstep: host word " << std::hex << w << " from the JIT, " << s << " from the interpreter";
				m_fault = o.str();
				std::fprintf(stderr, "%s\n", m_fault.c_str());
				m_shadow.reset();	// reported once; the JIT engine carries on alone
			}
		}
		return w;
	}

	// MD_JIT_LOCKSTEP: after each JIT block, run the interpreter-driven shadow to the same instruction count (the
	// interpreter runs a whole DO loop in one exec(), so whichever is behind steps) and compare the registers. Memory is
	// not compared: a wrong store shows up in the registers soon after. SR's E, U and N bits are left out (the
	// interpreter's CLR sets them but keeps a pending lazy update from the previous ALU op, which a later read of SR
	// applies; nothing in the voice program reads them in between, the JIT is right). The first difference becomes the
	// engine's fault (faultReason(), and on stderr) and ends the lockstep; rendering carries on with the JIT alone, so a
	// run still ends with its hash. Slow (about 2x the interpreter): for checking a JIT change, not for playing.
	void VoiceEngine::lockstep(const uint32_t _blockPc)
	{
		auto& jit = *m_dsp;
		auto& interp = *m_shadow->m_dsp;
		++m_lockstepBlocks;
		for(;;)
		{
			if(interp.getInstructionCounter() < jit.getInstructionCounter())
				interp.execInterpreter();
			else if(jit.getInstructionCounter() < interp.getInstructionCounter())
				jit.exec();
			else
				break;
		}
		const auto& a = jit.regs();
		const auto& b = interp.regs();
		std::string d;
		auto cmp = [&](const char* _name, const int _index, const uint64_t _jit, const uint64_t _interp)
		{
			if(_jit == _interp)
				return;
			char s[96];
			std::snprintf(s, sizeof(s), " %s%s jit=%llx interp=%llx", _name, _index >= 0 ? std::to_string(_index).c_str() : "",
				static_cast<unsigned long long>(_jit), static_cast<unsigned long long>(_interp));
			d += s;
		};
		constexpr uint64_t kNoEUN = ~uint64_t(CCR_E | CCR_U | CCR_N);
		cmp("pc", -1, a.pc.var, b.pc.var);
		cmp("sr", -1, jit.getSR().var & kNoEUN, interp.getSR().var & kNoEUN);
		cmp("omr", -1, a.omr.var, b.omr.var);
		cmp("x", -1, a.x.var, b.x.var);
		cmp("y", -1, a.y.var, b.y.var);
		cmp("a", -1, a.a.var, b.a.var);
		cmp("b", -1, a.b.var, b.b.var);
		for(int i = 0; i < 8; ++i)
		{
			cmp("r", i, a.r[i].var, b.r[i].var);
			cmp("n", i, a.n[i].var, b.n[i].var);
			cmp("m", i, a.m[i].var, b.m[i].var);
		}
		cmp("la", -1, a.la.var, b.la.var);
		cmp("lc", -1, a.lc.var, b.lc.var);
		cmp("sp", -1, a.sp.var, b.sp.var);
		cmp("sc", -1, a.sc.var, b.sc.var);
		for(int i = 0; i < static_cast<int>(a.ss.size()); ++i)	// SSL holds stacked SRs: E/U/N left out as above
			cmp("ss", i, a.ss[i].var & kNoEUN, b.ss[i].var & kNoEUN);
		if(d.empty())
			return;
		std::ostringstream o;
		o << "JIT lockstep: registers differ after JIT block " << m_lockstepBlocks << " (from P:$" << std::hex << _blockPc
		  << " to $" << a.pc.var << std::dec << ", instruction " << jit.getInstructionCounter() << "):" << d;
		m_fault = o.str();
		std::fprintf(stderr, "%s\n", m_fault.c_str());
		m_shadow.reset();	// reported once; the JIT engine carries on alone
	}
}
