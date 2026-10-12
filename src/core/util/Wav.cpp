// Ported from shnolk/monomodule src/core/util/Wav.cpp (AGPL-3.0).
#include "Wav.h"

#include <fstream>

namespace md::util
{
	namespace
	{
		void put32(std::ostream& _o, const uint32_t _v)
		{
			const char b[4] = {char(_v), char(_v >> 8), char(_v >> 16), char(_v >> 24)};
			_o.write(b, 4);
		}

		void put16(std::ostream& _o, const uint16_t _v)
		{
			const char b[2] = {char(_v), char(_v >> 8)};
			_o.write(b, 2);
		}
	}

	bool writeWav24(const std::string& _path, const std::vector<int32_t>& _s, const int _ch, const int _sr)
	{
		std::ofstream o(_path, std::ios::binary);
		if(!o)
			return false;
		const uint32_t dataBytes = uint32_t(_s.size()) * 3;
		o.write("RIFF", 4); put32(o, 36 + dataBytes); o.write("WAVE", 4);
		o.write("fmt ", 4); put32(o, 16); put16(o, 1); put16(o, uint16_t(_ch)); put32(o, uint32_t(_sr));
		put32(o, uint32_t(_sr * _ch * 3)); put16(o, uint16_t(_ch * 3)); put16(o, 24);
		o.write("data", 4); put32(o, dataBytes);
		for(const int32_t v : _s)
		{
			const char b[3] = {char(v), char(v >> 8), char(v >> 16)};
			o.write(b, 3);
		}
		return bool(o);
	}
}
