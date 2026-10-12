// Loading a kit into the engine: machines, the 24 parameters of each track, levels and LFOs of all 16 tracks.
#pragma once
#include <algorithm>

#include "HostModel.h"
#include "MdSysex.h"

namespace md::library
{
	// What a kit sets in the engine (HostModel). Like the MD loading a kit, every track gets its machine, all 24 parameters
	// (SYN1-8 from the kit, not the machine's defaults), its level and its LFO, so nothing of the previous kit stays.
	// Not part of a kit, and so left alone: the output routing (HostModel::setRouting, the MD keeps it in the global
	// settings), mutes and the tempo. The master effects, trig groups and mute groups are not modelled by the engine yet.
	//
	// As on the MD, a track takes a new machine at its next trigger. LFO values are limited to what the OS knows: 6
	// shapes and 3 update types (the MPC port's limits, which HostModel's own clamps are looser than).
	template<class TVoices>
	void applyKit(engine::HostModel<TVoices>& _host, const Kit& _kit)
	{
		for(size_t i = 0; i < kTracks; ++i)
		{
			const int t = static_cast<int>(i);
			_host.setMachine(t, _kit.machine[i]);		// SYN1-8 take the machine's defaults ...
			for(size_t p = 0; p < kParams; ++p)
				_host.setParam(t, static_cast<int>(p), _kit.params[i][p]);	// ... and then the kit's values
			_host.setLevel(t, _kit.level[i]);
			const Lfo& l = _kit.lfo[i];
			_host.setLfo(t, std::min<int>(l.destTrack, kTracks - 1), std::min<int>(l.destParam, kParams - 1),
				std::min<int>(l.shape1, 5), std::min<int>(l.shape2, 5), std::min<int>(l.type, 2));
		}
	}
}
