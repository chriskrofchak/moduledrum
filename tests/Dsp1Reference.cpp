// See Dsp1Reference.h. Ported from the master-effects spike's tools (mfx.h's Dsp1, mfx12).
#include "Dsp1Reference.h"

#include <sstream>
#include <stdexcept>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/audio.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

namespace md::test
{
	using namespace dsp56k;

	namespace
	{
		constexpr TWord kSizeP = 0x200000, kSizeXY = 0x800000, kBridge = 0x020000;
		constexpr TWord kAar[4] = {0x100539, 0x140639, 0x180539, 0x1c0639};
		constexpr TWord kOmr = 0x00490d;
		constexpr TWord kStub = 0xc00;
		constexpr TWord kLoopTop = 0x3c, kFirstPass = 0x294;
		constexpr uint64_t kMaxInstr = 50'000'000;
	}

	Dsp1Reference::Dsp1Reference(const fw::Firmware& _fw, const bool _interpreter) : m_interpreter(_interpreter)
	{
		m_validator = std::make_unique<DefaultMemoryValidator>();
		m_mem = std::make_unique<Memory>(*m_validator, kSizeP, kSizeXY, kBridge);
		m_periphX = std::make_unique<Peripherals56303>();
		m_periphY = std::make_unique<PeripheralsNop>();
		m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
		if(g_useJIT && !_interpreter)
		{
			auto cfg = m_dsp->getJit().getConfig();
			cfg.interruptRegionIsCode = true;
			cfg.dynamicFastInterrupts = true;
			cfg.aguSupportBitreverse = true;
			cfg.linkJitBlocks = false;
			m_dsp->getJit().setConfig(cfg);
		}
		else
			m_dsp->setInterpreterEnabled(true);
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
		m_dsp->resetHW();
		for(const auto& r : _fw.dspB.records)
			for(size_t k = 0; k < r.words.size(); ++k)
			{
				const TWord a = r.addr + static_cast<TWord>(k);
				if(r.space == fw::Space::P || a >= kBridge)
					m_dsp->memWriteP(a, r.words[k]);
				else
					m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
			}
		for(int i = 0; i < 4; ++i)
			m_periphX->write(0xfffff9 - static_cast<TWord>(i), kAar[i]);
		m_dsp->regs().omr.var = kOmr;

		// the init (jsr $100000 from the vector area) comes back to $2d: park it there
		m_dsp->memWriteP(0x2d, 0x0c0000 | 0x2d);
		m_dsp->setPC(0x24);
		const auto start = m_dsp->getInstructionCounter();
		while(m_dsp->getPC().toWord() != 0x2d)
		{
			if(!step() || m_dsp->getInstructionCounter() - start > 100'000'000)
				throw std::runtime_error("DSP1 reference: init did not complete " + m_fault);
		}
		m_dsp->memWrite(MemArea_X, 0xffffec, 0);	// DMA0 off: its block-complete interrupt feeds a deadline watchdog

		// the loop without the hardware waits (see the header)
		m_dsp->memWriteP(0x3c, 0x0c0044);			// jmp $44: always the X:$100/X:$400 half
		m_dsp->memWriteP(0x79, 0x000000);			// nop
		m_dsp->memWriteP(0x7d, 0x000000);			// nop
		m_dsp->memWriteP(0x25e, 0x0c006f);			// jmp $6f: the track function's timing pad does nothing
		m_dsp->memWriteP(0x25f, 0x000000);

		// the block end: $9a8 'bra $3c' -> a stub that sends one host word and waits for another
		Assembler as;
		TWord pc = kStub;
		auto emit = [&](const std::string& _text)
		{
			const auto r = as.assemble(_text.c_str());
			if(!r.success())
				throw std::runtime_error("DSP1 reference: cannot assemble '" + _text + "'");
			m_dsp->memWriteP(pc++, r.word[0]);
			if(r.wordCount > 1)
				m_dsp->memWriteP(pc++, r.word[1]);
		};
		std::ostringstream wait;
		wait << "jclr #0,x:<<$ffffc3,$" << std::hex << kStub + 2;
		emit("movep #>1,x:<<$ffffc7");
		emit(wait.str());
		emit("movep x:<<$ffffc6,a");
		emit("jmp $3c");
		m_dsp->memWriteP(0x9a8, 0x0af080);
		m_dsp->memWriteP(0x9a9, kStub);
		auto& hi = m_periphX->getHI08();
		hi.setRXRateLimit(0);
		hi.setTransmitDataAlwaysEmpty(true);
		hi.clearRX();
		while(hi.hasTX())
			hi.readTX();
	}

	Dsp1Reference::~Dsp1Reference() = default;

	bool Dsp1Reference::step()
	{
		if(m_interpreter)
			m_dsp->execInterpreter();
		else
			m_dsp->exec();
		if constexpr(g_useJIT)
		{
			if(!m_interpreter && m_dsp->getJit().hasFailed())
			{
				m_fault = "DSP JIT failed: " + m_dsp->getJit().failReason();
				return false;
			}
		}
		return true;
	}

	bool Dsp1Reference::pass(const uint32_t _pc)
	{
		for(int i = 0; i < 8; ++i)
			m_dsp->writeReg(static_cast<EReg>(Reg_M0 + i), TReg24(0xffffff));
		m_dsp->writeReg(Reg_SR, TReg24(m_dsp->getSR().toWord() | 0x300));
		auto& hi = m_periphX->getHI08();
		while(hi.hasTX())
			hi.readTX();
		m_dsp->setPC(_pc);
		const auto start = m_dsp->getInstructionCounter();
		while(!hi.hasTX())
		{
			if(!step())
				return false;
			if(m_dsp->getInstructionCounter() - start > kMaxInstr)
			{
				m_fault = "DSP1 reference: block budget exceeded";
				return false;
			}
		}
		while(hi.hasTX())
			hi.readTX();
		return true;
	}

	bool Dsp1Reference::startUp()
	{
		m_dsp->memWrite(MemArea_X, 0x640, 0x400);
		m_dsp->memWrite(MemArea_X, 0x641, 0x100);
		return pass(kFirstPass);
	}

	bool Dsp1Reference::runBlock(const Voices& _voices, const FxWords& _fx, const MixWords& _mix)
	{
		for(int t = 0; t < kTracks; ++t)
		{
			const auto u = static_cast<size_t>(t);
			const TWord tt = static_cast<TWord>(t);
			for(size_t k = 0; k < 9; ++k)
				m_dsp->memWrite(MemArea_Y, 0x200 + 0x40 * tt + static_cast<TWord>(k), _fx[u][k] & 0xffffff);
			for(size_t k = 0; k < 5; ++k)
				m_dsp->memWrite(MemArea_Y, 0x100 + 5 * tt + static_cast<TWord>(k), _mix[u][k] & 0xffffff);
			for(size_t i = 0; i < kBlock; ++i)
				m_dsp->memWrite(MemArea_Y, 0x600 + 0x20 * tt + static_cast<TWord>(i), static_cast<uint32_t>(_voices[u][i]) & 0xffffff);
		}
		m_dsp->memWrite(MemArea_X, 0x640, 0x400);
		m_dsp->memWrite(MemArea_X, 0x641, 0x100);
		return pass(kLoopTop);
	}

	uint32_t Dsp1Reference::x(const uint32_t _addr) const { return m_mem->get(MemArea_X, _addr) & 0xffffff; }
	uint32_t Dsp1Reference::y(const uint32_t _addr) const { return m_mem->get(MemArea_Y, _addr) & 0xffffff; }
	void Dsp1Reference::setY(const uint32_t _addr, const uint32_t _value) { m_dsp->memWrite(MemArea_Y, _addr, _value & 0xffffff); }
}
