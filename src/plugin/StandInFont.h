// The built-in stand-in face of the LCD (MdRomArt.h): the plain glyphs the UI draws with until the user's OS file is
// read, and for an OS image that does not validate. Drawn by hand, not taken from any OS file. From shnolk/monomodule
// src/plugin/one/RomArt.cpp (AGPL-3.0), unchanged.
#pragma once
#include <cstdint>

namespace md::uispec::standin {

// A plain 3x5 caps face, one digit per row of the glyph (bit 2 = left column). The other faces are made from it by
// repeating rows and columns, so layouts keep the row heights they have with the hardware's faces.
struct Glyph35 { char code; const char* rows; };
constexpr Glyph35 kGlyphs[] = {
    {'A', "25755"}, {'B', "65656"}, {'C', "34443"}, {'D', "65556"}, {'E', "74647"}, {'F', "74644"}, {'G', "34553"},
    {'H', "55755"}, {'I', "72227"}, {'J', "11152"}, {'K', "55655"}, {'L', "44447"}, {'M', "57755"}, {'N', "65555"},
    {'O', "25552"}, {'P', "65644"}, {'Q', "25573"}, {'R', "65655"}, {'S', "34216"}, {'T', "72222"}, {'U', "55557"},
    {'V', "55552"}, {'W', "55775"}, {'X', "55255"}, {'Y', "55222"}, {'Z', "71247"},
    {'0', "75557"}, {'1', "26227"}, {'2', "61247"}, {'3', "61216"}, {'4', "55711"}, {'5', "74616"}, {'6', "34757"},
    {'7', "71222"}, {'8', "75757"}, {'9', "75716"},
    {'+', "02720"}, {'-', "00700"}, {'.', "00002"}, {',', "00024"}, {'/', "11244"}, {':', "02020"}, {'(', "12221"},
    {')', "42224"}, {'%', "51245"}, {'!', "22202"}, {'?', "61202"}, {'\'', "22000"}, {'<', "12421"}, {'>', "42124"},
    {'=', "07070"}, {'_', "00007"}, {'#', "57575"}, {'&', "25253"}, {'*', "52725"}, {'[', "32223"}, {']', "62226"},
};

// Three columns cannot hold a diagonal: the 5-column faces take these letters from their own drawings (bit 4 = left).
struct Glyph55 { char code; uint8_t rows[5]; };
constexpr Glyph55 kWideGlyphs[] = {
    {'M', {0b10001, 0b11011, 0b10101, 0b10001, 0b10001}},
    {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001}},
    {'W', {0b10001, 0b10001, 0b10101, 0b11011, 0b10001}},
    {'V', {0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
    {'X', {0b10001, 0b01010, 0b00100, 0b01010, 0b10001}},
    {'Y', {0b10001, 0b01010, 0b00100, 0b00100, 0b00100}},
    {'K', {0b10010, 0b10100, 0b11000, 0b10100, 0b10010}},
};

} // namespace md::uispec::standin
