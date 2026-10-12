// The LCD artwork of the UI: six bitmap fonts and the dial (ring, pointer frames, plain ring, group tie). None of it is
// part of this source tree or of the binaries. It is read out of the user's own Machinedrum OS file when that file is
// selected (installRomArt / ensureRomArt); only the places where OS 1.63 keeps it are listed in MdRomArt.cpp. Until an
// OS file is present, and for an image that does not validate (another OS version), the UI draws with a plain built-in
// stand-in face (StandInFont.h) and generated dials.
// Ported from shnolk/monomodule src/plugin/one/RomArt.{h,cpp} (AGPL-3.0), with the addresses and layouts of
// sd88me/mpc-vst-machinedrum tools/mdskin/mdartdump.cpp (AGPL-3.0) for Machinedrum UW OS 1.63. The Monomachine-only
// parts (its list-icon families and machine-group logos) are left out; the OFF/ON toggle icon, which the MD OS lacks,
// is drawn in code (as mdartdump does).
//
// No JUCE: the unit tests link this file on its own.
//
// Threading: the artwork is read while painting and written by an install, both without locks. Install only from the
// thread that paints (the message thread; the main thread of the console tools). Storage of an earlier install is
// never freed, so a Bitmap pointer taken before an install stays valid.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace md::uispec {

// 1-bit image; row r, pixel x is lit when bit (63 - x) of rows[r] is set (row 0 = top).
struct Bitmap {
    uint8_t w = 0, h = 0;
    const uint64_t* rows = nullptr;
    bool lit(int x, int y) const { return (rows[y] >> (63 - x)) & 1u; }
};

// Proportional bitmap font: glyph(c) is null for a code without a glyph (advance by adv then).
struct Font {
    uint8_t h = 0, adv = 0;
    const Bitmap* bitmaps = nullptr;
    const int16_t* index = nullptr;   // 128 entries, -1 = no glyph
    const Bitmap* glyph(int c) const { return c >= 0 && c < 128 && index[c] >= 0 ? &bitmaps[index[c]] : nullptr; }
};

struct Art {
    Font bold8, small4x5, tiny3x5, square5x5, digitsTop, digitsBottom;
    Bitmap dialRing;                  // 11x13
    Bitmap groupTie;                  // 7x3
    Bitmap ringPlain;                 // 11x11 plain ring (base of the drawn rotary switch)
    const Bitmap* dialDot[128] = {};  // 7x7 pointer frame per raw value, composite at (2, 4); never null
    // OFF/ON, 17x13: a ring with its pointer up-left (off) or up-right (on). Not in the MD OS: always drawn in code.
    const Bitmap* iconToggle[2] = {};
    bool fromRom = false;
};

// The artwork in use: the stand-in until an install succeeds. The object itself never moves.
Art& art();

// Reads the artwork out of a decompressed main OS image (section 0 of the OS file, load base 0x200000) and makes it
// the artwork in use. The image must be OS 1.63's (size and checksum), and every descriptor is bounds- and
// shape-checked; on failure nothing changes and `error` says what did not fit.
bool installRomArt(const std::vector<uint8_t>& mainOs, std::string* error = nullptr);

// installRomArt for an OS file on disk. Does nothing (and returns true) when the artwork in use already came from this
// file (same path, size and modification time), so it is cheap to call on every editor open.
bool ensureRomArt(const std::filesystem::path& osFile, std::string* error = nullptr);

// Section 0 of an OS file (the ColdFire OS), decompressed. Throws on anything but a Machinedrum OS file.
std::vector<uint8_t> loadMainOs(const std::filesystem::path& osFile);

// The rectangle of a bitmap that holds its lit pixels.
struct Bounds { int x = 0, y = 0, w = 0, h = 0; };
Bounds litBounds(const Bitmap& b);

inline const Font& kFontBold8 = art().bold8;
inline const Font& kFontSmall4x5 = art().small4x5;
inline const Font& kFontTiny3x5 = art().tiny3x5;
inline const Font& kFontSquare5x5 = art().square5x5;
inline const Font& kFontDigitsTop = art().digitsTop;
inline const Font& kFontDigitsBottom = art().digitsBottom;
inline const Bitmap& kDialRing = art().dialRing;
inline const Bitmap& kGroupTie = art().groupTie;
inline const Bitmap& kRingPlain = art().ringPlain;
inline const auto& kDialDot = art().dialDot;
inline const auto& kIconToggle = art().iconToggle;

} // namespace md::uispec
