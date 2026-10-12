// The fixed pattern md-hash renders and its hash: 12 s of all 16 tracks (machines spread over the OS's table, two ROM
// machines, routing, random triggers) through the whole engine, hashed over the main mix, the 6-channel frames and the
// reverb/delay sends. Equal hashes = identical audio. md-hash and the golden test share it, so they render the same thing.
// Ported from sd88me/mpc-vst-machinedrum tools/mdhash/mdhash.cpp (AGPL-3.0).
#pragma once
#include <cstdint>

#include "MdEngine.h"

namespace md::golden
{
	// The default pattern with OS 1.63 and no ROM samples on dsp56300's interpreter (.clew increment 0001); the same
	// for 1, 2 and 4 voice groups. Any engine change must keep it.
	constexpr uint64_t kHashOs163 = 0x04ef1db4fe767721ull;
	constexpr int kBlocks = 44100 * 12 / 32;

	class Pattern
	{
	public:
		// Machines, levels, LFO settings and routing of the 16 tracks
		template<class THost> void setup(THost& _h)
		{
			for(int t = 0; t < 16; ++t)
			{
				_h.setMachine(t, uint8_t(16 + (t * 7) % 60));
				_h.setParam(t, 13, 127);
				_h.setParam(t, 17, 100);
				_h.setParam(t, 18, uint8_t(rnd() % 128));
				_h.setParam(t, 19, 40);
				_h.setParam(t, 20, 40);
				_h.setParam(t, 21, 40);
			}
			for(int t = 0; t < 6; ++t)
				_h.setRouting(t, 1 + t % 5);
			_h.setMachine(5, 130);
			_h.setMachine(9, 140);
		}

		// The triggers due before block _b: four random tracks every 300 blocks
		template<class THost> void beforeBlock(THost& _h, const int _b)
		{
			if(_b % 300 == 0)
				for(int k = 0; k < 4; ++k)
					_h.trigger(static_cast<int>(rnd() % 16), 100);
		}

		void hashBlock(const engine::Mixer::Output& _out)
		{
			for(int f = 0; f < 32; ++f)
				for(int ch = 0; ch < 2; ++ch)
				{
					mix(_out.main[f][ch]);
					for(int q = 0; q < 6; ++q)
						mix(_out.frame[f][q]);
					mix(_out.rev[f][ch]);
					mix(_out.del[f][ch]);
				}
		}

		uint64_t hash() const { return m_hash; }

	private:
		unsigned rnd() { m_rs = m_rs * 1664525u + 1013904223u; return m_rs >> 8; }
		void mix(const int64_t _v) { m_hash ^= uint64_t(_v); m_hash *= 1099511628211ull; }	// FNV-1a over the samples

		unsigned m_rs = 99;
		uint64_t m_hash = 1469598103934665603ull;
	};

	// The whole pattern on a fresh engine. _onBlock(b, output) is called after every block (md-hash's statistics and raw dump).
	template<class E, class F> uint64_t render(E& _eng, F&& _onBlock)
	{
		Pattern p;
		p.setup(_eng.host());
		typename E::Output out;
		for(int b = 0; b < kBlocks; ++b)
		{
			p.beforeBlock(_eng.host(), b);
			_eng.render(out);
			_onBlock(b, out);
			p.hashBlock(out.mix);
		}
		return p.hash();
	}

	template<class E> uint64_t render(E& _eng) { return render(_eng, [](int, const typename E::Output&) {}); }
}
