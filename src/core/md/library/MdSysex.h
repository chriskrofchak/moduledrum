// Machinedrum sysex files: splitting a .syx byte stream into messages, recognising the Machinedrum header and decoding
// the kit dumps ($52) in it. Patterns ($67), songs ($69) and global settings ($50) are skipped and counted.
//
// The kit decoder is ported from sd88me/mpc-vst-machinedrum vst/engine.cpp (parseKit, AGPL-3.0, verified there against
// the emulated MD's own dumps) and extended to the whole message; the layout below was cross-checked against jmamma/MCL
// (MDKit::fromSysex), and against the 64 kit dumps of a real MD snapshot: all 232 messages of it carry the checksum
// and length described here, and decode + encode reproduces every kit message byte for byte. The MD manual's sysex
// appendix lists only the message ids, not the bytes of the dumps.
//
// Kit dump, OS 1.63 (version 4, revision 1), 1233 bytes:
//   0x000  F0 00 20 3C 02 00 52      header; the byte after 02 00 is the message type
//   0x007  version, revision         4, 1
//   0x009  kit slot 0-63             the position the dump came from (covered by the checksum)
//   0x00a  name, 16 bytes            ASCII, NUL terminated; whatever follows the NUL is stale buffer content. A free
//                                    slot has $7f as its first byte
//   0x01a  16 x 24 track parameters  7-bit values in HostModel's raw order (see Kit::params)
//   0x19a  16 track levels (LEV)
//   0x1aa  16 machine ids            7-bit packed (below) 32-bit big-endian words, 64 bytes -> 74; the id is the low byte
//   0x1f4  16 LFOs x 36 bytes        7-bit packed, 576 -> 659: destination track, destination param, shape 1, shape 2,
//                                    update type, then 31 bytes of LFO state
//   0x487  master effects, 32 bytes  plain: gate box reverb, rhythm echo delay, master EQ, dynamix, 8 values each
//   0x4a7  trig and mute groups      7-bit packed, 32 -> 37: 16 trig groups, 16 mute groups ($ff = none)
//   0x4cc  checksum (2 bytes, 14 bit), length (2 bytes, 14 bit), F7
//          checksum = sum of the bytes from 0x009 up to here, & $3fff; length = message size - 10
//
// 7-bit packing (Elektron's): every group of up to 7 data bytes is preceded by a byte holding their top bits, the
// first data byte's bit in bit 6. A message of 7-bit data bytes cannot carry bit 7 itself.
//
// Output routing (OUT A-F, main) is not part of a kit: the MD keeps it in the global settings ($50), not read here.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace md::library
{
	constexpr int kTracks = 16;
	constexpr int kParams = 24;			// per track: SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST, VOL PAN DEL REV, LFOS LFOD LFOM
	constexpr int kSynParams = 8;
	constexpr int kKitSlots = 64;		// the MD's kit memory
	constexpr size_t kKitMessageSize = 1233;
	constexpr int kMaxMachineId = 191;	// HostModel: 0-191 (the OS's machine ids, ROM/RAM machines included)

	// Message types: the byte after the header F0 00 20 3C 02 00
	enum class MessageType : uint8_t
	{
		Global = 0x50,
		Kit = 0x52,
		Pattern = 0x67,
		Song = 0x69,
	};

	// A sysex message found in a byte stream: [offset, offset + size) of the stream, F0 to F7 inclusive
	struct Message
	{
		size_t offset = 0;
		size_t size = 0;
		bool complete = false;		// false: cut short by the end of the stream or by another status byte, no F7
	};

	// Every sysex message in the stream, in order. Bytes outside F0..F7 are ignored.
	std::vector<Message> splitMessages(const uint8_t* _data, size_t _size);

	// F0 00 20 3C 02 00 <type> ...: an Elektron Machinedrum message (the type is _msg[6])
	bool isMachinedrumMessage(const uint8_t* _msg, size_t _size);

	// Elektron's 7-bit packing, see above. decode7 ignores a trailing header byte without data.
	std::vector<uint8_t> decode7(const uint8_t* _p, size_t _n);
	std::vector<uint8_t> encode7(const uint8_t* _p, size_t _n);
	constexpr size_t packed7Size(const size_t _n) { return _n + (_n + 6) / 7; }

	// The master effects of a kit: the four blocks as the kit stores them, 8 raw values (0-127) each, in the order of the
	// OS's own kit RAM (reverb, echo, EQ, dynamix: $70042a, $700432, $70043a, $700442). The names below are confirmed by
	// what the OS does with each byte (increment 0009, the research spike): it turns the 32 bytes into the 38 words of the
	// mixer DSP's master section, and each index feeds the role its name says (reverb index 6 is the gate and 127 switches
	// it off, echo 4 and 5 are the loop filter, EQ 4 and 5 are the parametric band's frequency and gain, 6 its Q). The
	// engine runs them (engine::MasterFx, HostModel::setMasterFx). The editor's pages run ECHO, REV, EQ, DYN, as on the
	// hardware's menu; storage and this struct keep the dump's order.
	struct MasterFx
	{
		std::array<uint8_t, 8> reverb{};	// gate box: DVOL PRED DEC DAMP HP LP GATE LEV
		std::array<uint8_t, 8> delay{};		// rhythm echo: TIME MOD MFRQ FB FILTF FILTW MONO LEV
		std::array<uint8_t, 8> eq{};		// master EQ: LF LG HF HG PF PG PQ GAIN
		std::array<uint8_t, 8> dynamics{};	// dynamix: ATCK REL TRHD RTIO KNEE HP OUTG MIX

		// The 32 bytes in the dump's order, the engine's master::kParams (HostModel::setMasterFx(i, bytes[i]))
		std::array<uint8_t, 32> bytes() const
		{
			std::array<uint8_t, 32> b{};
			std::copy(reverb.begin(), reverb.end(), b.begin());
			std::copy(delay.begin(), delay.end(), b.begin() + 8);
			std::copy(eq.begin(), eq.end(), b.begin() + 16);
			std::copy(dynamics.begin(), dynamics.end(), b.begin() + 24);
			return b;
		}
		void setBytes(const uint8_t* _b)
		{
			std::copy(_b, _b + 8, reverb.begin());
			std::copy(_b + 8, _b + 16, delay.begin());
			std::copy(_b + 16, _b + 24, eq.begin());
			std::copy(_b + 24, _b + 32, dynamics.begin());
		}
	};

	// A track's LFO (LFO k belongs to track k; its speed, depth and shape mix are the track's parameters 21-23)
	struct Lfo
	{
		uint8_t destTrack = 0;				// 0-15
		uint8_t destParam = 0;				// 0-23, Kit::params order
		uint8_t shape1 = 0;					// 0-5: triangle, saw, square, linear decay, exponential decay, random
		uint8_t shape2 = 0;					// 0-5
		uint8_t type = 0;					// 0 FREE, 1 TRIG, 2 HOLD
		std::array<uint8_t, 31> state{};	// the LFO's running state, kept raw
	};

	struct Kit
	{
		// ---- as dumped ----
		uint8_t version = 4;
		uint8_t revision = 1;
		uint8_t position = 0;							// the MD's kit slot 0-63 it was dumped from
		std::array<uint8_t, 16> rawName{};				// the 16 name bytes as they are in the message
		std::string name;								// printable ASCII up to the first NUL or other control byte, trimmed
		std::array<uint32_t, kTracks> model{};			// the machine words as dumped; the machine id is the low byte
		std::array<uint8_t, kTracks> machine{};			// machine id per track (0-191, the OS's MachineInfo::id)
		// HostModel raw order: 0-7 SYN1-8, 8 AMD, 9 AMF, 10 EQF, 11 EQG, 12 FLTF, 13 FLTW, 14 FLTQ, 15 SRR, 16 DIST,
		// 17 VOL, 18 PAN, 19 DEL, 20 REV, 21 LFOS, 22 LFOD, 23 LFOM. Values 0-127.
		std::array<std::array<uint8_t, kParams>, kTracks> params{};
		std::array<uint8_t, kTracks> level{};			// track LEV, 0-127
		std::array<Lfo, kTracks> lfo{};
		MasterFx masterFx;								// the four master effect blocks (engine: HostModel::setMasterFx)
		std::array<uint8_t, kTracks> trigGroup{};		// track to trigger along with this one, 0xff = none
		std::array<uint8_t, kTracks> muteGroup{};		// track to mute when this one plays, 0xff = none
		bool checksumOk = true;							// the message's checksum and length matched (decoding goes on if not)

		// A free slot: the MD marks one with $7f (or NUL) as the first name byte, and its machines are all GND-EMPTY
		bool isFree() const { return rawName[0] == 0x7f || rawName[0] == 0; }
		void setName(const std::string& _name);			// rawName and name from _name (16 characters at most)
	};

	// Decode one kit dump message ([F0 .. F7], 1233 bytes). False, with the reason in _error, when it is not one.
	bool decodeKit(const uint8_t* _msg, size_t _size, Kit& _kit, std::string* _error = nullptr);

	// The kit dump message for _kit (what decodeKit reads; the checksum and length are computed)
	std::vector<uint8_t> encodeKit(const Kit& _kit);

	// What a .syx file holds: its kits in file order, and how many other messages were skipped
	struct Bank
	{
		std::string name;				// loadSyxFile: the file name without extension
		std::vector<Kit> kits;			// every kit dump, free slots included (Kit::isFree)
		int patterns = 0;				// skipped
		int songs = 0;					// skipped
		int globals = 0;				// skipped
		int otherMachinedrum = 0;		// other Machinedrum messages (OS data, sample dumps, requests, ...), skipped
		int foreign = 0;				// sysex messages of other devices, skipped
		int rejected = 0;				// kit dumps that did not decode, and cut-short messages
		int badChecksums = 0;			// kits decoded in spite of a wrong checksum or length
		std::vector<std::string> errors;	// one line per rejected message

		int usedKits() const;			// the kits that are not free slots
	};

	Bank parseSyx(const uint8_t* _data, size_t _size);
	Bank parseSyx(const std::vector<uint8_t>& _data);
	Bank loadSyxFile(const std::filesystem::path& _path);	// throws std::runtime_error when the file cannot be read
}
