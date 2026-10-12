#include "MdSysex.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace md::library
{
	namespace
	{
		constexpr uint8_t kHeader[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00};
		constexpr uint8_t kSysexEnd = 0xf7;
		constexpr size_t kT = kTracks, kP = kParams;

		// Kit dump layout, see the header
		constexpr size_t kVersionOff = 0x007;
		constexpr size_t kPositionOff = 0x009;
		constexpr size_t kNameOff = 0x00a, kNameLen = 16;
		constexpr size_t kParamsOff = 0x01a;
		constexpr size_t kLevelsOff = kParamsOff + kT * kP;				// 0x19a
		constexpr size_t kModelsOff = kLevelsOff + kT;							// 0x1aa
		constexpr size_t kModelsData = kT * 4;
		constexpr size_t kLfoOff = kModelsOff + packed7Size(kModelsData);			// 0x1f4
		constexpr size_t kLfoSize = 36;												// 5 settings + 31 bytes of state
		constexpr size_t kLfoData = kT * kLfoSize;
		constexpr size_t kFxOff = kLfoOff + packed7Size(kLfoData);					// 0x487
		constexpr size_t kFxLen = 32;
		constexpr size_t kGroupsOff = kFxOff + kFxLen;								// 0x4a7
		constexpr size_t kGroupsData = 2 * kT;
		constexpr size_t kTrailerOff = kGroupsOff + packed7Size(kGroupsData);		// 0x4cc: checksum, length, F7
		static_assert(kTrailerOff + 5 == kKitMessageSize, "kit dump layout");

		uint16_t checksumOf(const uint8_t* _msg, const size_t _size)
		{
			unsigned sum = 0;
			for(size_t i = kPositionOff; i + 5 < _size; ++i)
				sum += _msg[i];
			return static_cast<uint16_t>(sum & 0x3fff);
		}

		std::string printableName(const std::array<uint8_t, kNameLen>& _raw)
		{
			std::string s;
			for(const uint8_t c : _raw)
			{
				if(c < 0x20 || c >= 0x7f)
					break;
				s.push_back(static_cast<char>(c));
			}
			while(!s.empty() && s.back() == ' ')
				s.pop_back();
			return s;
		}

		bool fail(std::string* _error, const std::string& _what)
		{
			if(_error)
				*_error = _what;
			return false;
		}
	}

	std::vector<Message> splitMessages(const uint8_t* const _data, const size_t _size)
	{
		std::vector<Message> out;
		size_t i = 0;
		while(i < _size)
		{
			if(_data[i] != 0xf0)
			{
				++i;
				continue;
			}
			Message m;
			m.offset = i;
			size_t e = i + 1;
			while(e < _size && _data[e] < 0x80)	// data bytes
				++e;
			if(e < _size && _data[e] == kSysexEnd)
			{
				m.complete = true;
				++e;
			}
			m.size = e - i;
			out.push_back(m);
			i = e;
		}
		return out;
	}

	bool isMachinedrumMessage(const uint8_t* const _msg, const size_t _size)
	{
		return _size > sizeof kHeader && !std::memcmp(_msg, kHeader, sizeof kHeader);
	}

	std::vector<uint8_t> decode7(const uint8_t* const _p, const size_t _n)
	{
		std::vector<uint8_t> out;
		out.reserve(_n - _n / 8);
		for(size_t i = 0; i < _n; i += 8)
			for(size_t j = 1; j < 8 && i + j < _n; ++j)
				out.push_back(static_cast<uint8_t>(_p[i + j] | ((_p[i] << j) & 0x80)));
		return out;
	}

	std::vector<uint8_t> encode7(const uint8_t* const _p, const size_t _n)
	{
		std::vector<uint8_t> out;
		out.reserve(packed7Size(_n));
		for(size_t i = 0; i < _n; i += 7)
		{
			const size_t head = out.size();
			out.push_back(0);
			for(size_t j = 0; j < 7 && i + j < _n; ++j)
			{
				out[head] = static_cast<uint8_t>(out[head] | ((_p[i + j] >> 7) << (6 - j)));
				out.push_back(_p[i + j] & 0x7f);
			}
		}
		return out;
	}

	void Kit::setName(const std::string& _name)
	{
		rawName.fill(0);
		for(size_t i = 0; i < kNameLen && i < _name.size(); ++i)
		{
			const auto c = static_cast<uint8_t>(_name[i]);
			rawName[i] = (c >= 0x20 && c < 0x7f) ? c : ' ';
		}
		name = printableName(rawName);
	}

	bool decodeKit(const uint8_t* const _msg, const size_t _size, Kit& _kit, std::string* const _error)
	{
		if(_size != kKitMessageSize)
			return fail(_error, "kit dump is " + std::to_string(_size) + " bytes, expected " + std::to_string(kKitMessageSize));
		if(!isMachinedrumMessage(_msg, _size) || _msg[6] != static_cast<uint8_t>(MessageType::Kit))
			return fail(_error, "not a Machinedrum kit dump");
		if(_msg[_size - 1] != kSysexEnd)
			return fail(_error, "kit dump does not end with F7");
		for(size_t i = 1; i + 1 < _size; ++i)
			if(_msg[i] >= 0x80)
				return fail(_error, "kit dump has a byte with bit 7 set at offset " + std::to_string(i));

		Kit k;
		k.version = _msg[kVersionOff];
		k.revision = _msg[kVersionOff + 1];
		k.position = _msg[kPositionOff];
		std::memcpy(k.rawName.data(), _msg + kNameOff, kNameLen);
		k.name = printableName(k.rawName);

		for(size_t t = 0; t < kT; ++t)
		{
			for(size_t p = 0; p < kP; ++p)
				k.params[t][p] = _msg[kParamsOff + t * kP + p] & 0x7f;
			k.level[t] = _msg[kLevelsOff + t] & 0x7f;
		}

		const auto models = decode7(_msg + kModelsOff, packed7Size(kModelsData));
		const auto lfos = decode7(_msg + kLfoOff, packed7Size(kLfoData));
		const auto groups = decode7(_msg + kGroupsOff, packed7Size(kGroupsData));
		if(models.size() != kModelsData || lfos.size() != kLfoData || groups.size() != kGroupsData)
			return fail(_error, "kit dump: 7-bit blocks have the wrong size");

		for(size_t t = 0; t < kT; ++t)
		{
			// a 32-bit big-endian word; the machine id is its low byte
			const uint8_t* w = &models[t * 4];
			k.model[t] = uint32_t(w[0]) << 24 | uint32_t(w[1]) << 16 | uint32_t(w[2]) << 8 | w[3];
			k.machine[t] = w[3];

			const uint8_t* l = &lfos[t * kLfoSize];
			Lfo& lfo = k.lfo[t];
			lfo.destTrack = l[0];
			lfo.destParam = l[1];
			lfo.shape1 = l[2];
			lfo.shape2 = l[3];
			lfo.type = l[4];
			std::copy(l + 5, l + kLfoSize, lfo.state.begin());

			k.trigGroup[t] = groups[t];
			k.muteGroup[t] = groups[kT + t];
		}

		const uint8_t* fx = _msg + kFxOff;
		std::copy(fx, fx + 8, k.masterFx.reverb.begin());
		std::copy(fx + 8, fx + 16, k.masterFx.delay.begin());
		std::copy(fx + 16, fx + 24, k.masterFx.eq.begin());
		std::copy(fx + 24, fx + 32, k.masterFx.dynamics.begin());

		const unsigned checksum = unsigned(_msg[kTrailerOff]) << 7 | _msg[kTrailerOff + 1];
		const unsigned length = unsigned(_msg[kTrailerOff + 2]) << 7 | _msg[kTrailerOff + 3];
		k.checksumOk = checksum == checksumOf(_msg, _size) && length == _size - 10;

		_kit = k;
		return true;
	}

	std::vector<uint8_t> encodeKit(const Kit& _kit)
	{
		std::vector<uint8_t> m(kKitMessageSize, 0);
		std::memcpy(m.data(), kHeader, sizeof kHeader);
		m[6] = static_cast<uint8_t>(MessageType::Kit);
		m[kVersionOff] = _kit.version & 0x7f;
		m[kVersionOff + 1] = _kit.revision & 0x7f;
		m[kPositionOff] = _kit.position & 0x7f;
		for(size_t i = 0; i < kNameLen; ++i)
			m[kNameOff + i] = _kit.rawName[i] & 0x7f;

		std::vector<uint8_t> models(kModelsData), lfos(kLfoData), groups(kGroupsData);
		for(size_t t = 0; t < kT; ++t)
		{
			for(size_t p = 0; p < kP; ++p)
				m[kParamsOff + t * kP + p] = _kit.params[t][p] & 0x7f;
			m[kLevelsOff + t] = _kit.level[t] & 0x7f;

			// the machine id replaces the low byte of the word, so editing Kit::machine is enough
			const uint32_t word = (_kit.model[t] & ~0xffu) | _kit.machine[t];
			for(size_t b = 0; b < 4; ++b)
				models[t * 4 + b] = static_cast<uint8_t>(word >> (24 - 8 * b));

			uint8_t* l = &lfos[t * kLfoSize];
			const Lfo& lfo = _kit.lfo[t];
			l[0] = lfo.destTrack;
			l[1] = lfo.destParam;
			l[2] = lfo.shape1;
			l[3] = lfo.shape2;
			l[4] = lfo.type;
			std::copy(lfo.state.begin(), lfo.state.end(), l + 5);

			groups[t] = _kit.trigGroup[t];
			groups[kT + t] = _kit.muteGroup[t];
		}
		const auto put = [&m](const size_t _off, const std::vector<uint8_t>& _packed) { std::copy(_packed.begin(), _packed.end(), m.begin() + static_cast<std::ptrdiff_t>(_off)); };
		put(kModelsOff, encode7(models.data(), models.size()));
		put(kLfoOff, encode7(lfos.data(), lfos.size()));
		put(kGroupsOff, encode7(groups.data(), groups.size()));

		uint8_t* fx = &m[kFxOff];
		const MasterFx& mf = _kit.masterFx;
		for(size_t i = 0; i < 8; ++i)
		{
			fx[i] = mf.reverb[i] & 0x7f;
			fx[8 + i] = mf.delay[i] & 0x7f;
			fx[16 + i] = mf.eq[i] & 0x7f;
			fx[24 + i] = mf.dynamics[i] & 0x7f;
		}

		const uint16_t checksum = checksumOf(m.data(), m.size());
		const size_t length = m.size() - 10;
		m[kTrailerOff] = static_cast<uint8_t>(checksum >> 7);
		m[kTrailerOff + 1] = checksum & 0x7f;
		m[kTrailerOff + 2] = static_cast<uint8_t>(length >> 7);
		m[kTrailerOff + 3] = length & 0x7f;
		m[kTrailerOff + 4] = kSysexEnd;
		return m;
	}

	int Bank::usedKits() const
	{
		return static_cast<int>(std::count_if(kits.begin(), kits.end(), [](const Kit& _k) { return !_k.isFree(); }));
	}

	Bank parseSyx(const uint8_t* const _data, const size_t _size)
	{
		Bank bank;
		for(const Message& m : splitMessages(_data, _size))
		{
			const uint8_t* msg = _data + m.offset;
			if(!m.complete)
			{
				++bank.rejected;
				bank.errors.push_back("cut-short sysex message at offset " + std::to_string(m.offset) + " (" + std::to_string(m.size) + " bytes)");
				continue;
			}
			if(!isMachinedrumMessage(msg, m.size))
			{
				++bank.foreign;
				continue;
			}
			switch(static_cast<MessageType>(msg[6]))
			{
			case MessageType::Kit:
			{
				Kit k;
				std::string error;
				if(!decodeKit(msg, m.size, k, &error))
				{
					++bank.rejected;
					bank.errors.push_back("kit dump at offset " + std::to_string(m.offset) + ": " + error);
					break;
				}
				if(!k.checksumOk)
					++bank.badChecksums;
				bank.kits.push_back(std::move(k));
				break;
			}
			case MessageType::Pattern: ++bank.patterns; break;
			case MessageType::Song: ++bank.songs; break;
			case MessageType::Global: ++bank.globals; break;
			default: ++bank.otherMachinedrum; break;
			}
		}
		return bank;
	}

	Bank parseSyx(const std::vector<uint8_t>& _data)
	{
		return parseSyx(_data.data(), _data.size());
	}

	Bank loadSyxFile(const std::filesystem::path& _path)
	{
		constexpr std::uintmax_t kMaxFile = 32u << 20;	// a Machinedrum OS file is 1.6 MB, a full snapshot under 1 MB
		std::error_code ec;
		const auto size = std::filesystem::file_size(_path, ec);
		if(ec)
			throw std::runtime_error("cannot read " + _path.string() + ": " + ec.message());
		if(size > kMaxFile)
			throw std::runtime_error(_path.string() + " is too large for a sysex file");
		std::ifstream f(_path, std::ios::binary);
		std::vector<uint8_t> data(static_cast<size_t>(size));
		if(!f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
			throw std::runtime_error("cannot read " + _path.string());
		Bank bank = parseSyx(data);
		bank.name = _path.stem().string();
		return bank;
	}
}
