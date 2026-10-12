// The LCD artwork, read from the user's Machinedrum OS file (see MdRomArt.h). From shnolk/monomodule
// src/plugin/one/RomArt.cpp (AGPL-3.0); addresses and layouts from sd88me/mpc-vst-machinedrum
// tools/mdskin/mdartdump.cpp (AGPL-3.0).
#include "MdRomArt.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <system_error>
#include "Firmware.h"
#include "StandInFont.h"

namespace md::uispec {

namespace {

// Backing storage of one artwork set. Deques and per-font vectors sized once: addresses stay put.
struct Store {
    std::deque<std::vector<uint64_t>> rows;
    std::deque<Bitmap> bitmaps;
    struct FontData { std::vector<Bitmap> glyphs; std::array<int16_t, 128> index; };
    std::deque<FontData> fonts;

    const uint64_t* addRows(std::vector<uint64_t> r) { rows.push_back(std::move(r)); return rows.back().data(); }
    const Bitmap* addBitmap(int w, int h, std::vector<uint64_t> r)
    {
        bitmaps.push_back(Bitmap{uint8_t(w), uint8_t(h), addRows(std::move(r))});
        return &bitmaps.back();
    }
};

// Every set ever made, kept for the life of the process (see the threading note in MdRomArt.h).
std::vector<std::unique_ptr<Store>>& stores() { static std::vector<std::unique_ptr<Store>> s; return s; }

// ---------------------------------------------------------------------------------------------------
// Drawn artwork: the stand-in face (StandInFont.h) and dials, used while no OS file is present, and the OFF/ON toggle,
// which the MD OS does not have.

// rowMap/colMap: source row/column of every output row/column.
Font standInFont(Store& st, std::vector<int> rowMap, std::vector<int> colMap, const char* only = nullptr)
{
    st.fonts.emplace_back();
    auto& fd = st.fonts.back();
    fd.index.fill(-1);
    const int w = int(colMap.size()), h = int(rowMap.size());
    for (const auto& g : standin::kGlyphs) {
        if (only && !std::char_traits<char>::find(only, std::char_traits<char>::length(only), g.code)) continue;
        std::vector<uint64_t> rows(size_t(h), 0);
        const standin::Glyph55* wide = nullptr;
        if (w == 5) for (const auto& k : standin::kWideGlyphs) if (k.code == g.code) wide = &k;
        for (int r = 0; r < h; ++r) {
            const int bits = wide ? wide->rows[rowMap[size_t(r)]] : g.rows[rowMap[size_t(r)]] - '0';
            for (int c = 0; c < w; ++c)
                if ((bits >> (wide ? 4 - c : 2 - colMap[size_t(c)])) & 1) rows[size_t(r)] |= uint64_t(1) << (63 - c);
        }
        fd.index[size_t(g.code)] = int16_t(fd.glyphs.size());
        fd.glyphs.push_back(Bitmap{uint8_t(w), uint8_t(h), st.addRows(std::move(rows))});
    }
    return Font{uint8_t(h), uint8_t(w), fd.glyphs.data(), fd.index.data()};
}

const Bitmap* fromText(Store& st, std::initializer_list<const char*> lines)
{
    std::vector<uint64_t> rows;
    int w = 0;
    for (const char* l : lines) {
        uint64_t v = 0; int x = 0;
        for (; l[x]; ++x) if (l[x] == '#') v |= uint64_t(1) << (63 - x);
        rows.push_back(v); w = x;
    }
    const int h = int(rows.size());
    return st.addBitmap(w, h, std::move(rows));
}

const Bitmap* ring(Store& st, int size, int topPad)   // a circle of `size` pixels across under topPad blank rows
{
    std::vector<uint64_t> rows(size_t(size + topPad), 0);
    const double c = (size - 1) / 2.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (std::abs(std::hypot(x - c, y - c) - c) < 0.5) rows[size_t(y + topPad)] |= uint64_t(1) << (63 - x);
    return st.addBitmap(size, size + topPad, std::move(rows));
}

// The OFF/ON icon, 17x13: an 11x11 ring with a pointer from its centre, up-left = off, up-right = on (mdartdump's).
const Bitmap* toggleIcon(Store& st, bool on)
{
    static const char* kRing[11] = {"...#####...", "..#.....#..", ".#.......#.", "#.........#", "#.........#", "#.........#",
                                    "#.........#", "#.........#", ".#.......#.", "..#.....#..", "...#####..."};
    std::vector<uint64_t> rows(13, 0);
    auto set = [&](int x, int y) { rows[size_t(y)] |= uint64_t(1) << (63 - x); };
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 11; ++x)
            if (kRing[y][x] == '#') set(3 + x, 1 + y);
    for (int i = 0; i < 4; ++i) set(on ? 8 + 1 + i : 8 - 1 - i, 6 - i);
    return st.addBitmap(17, 13, std::move(rows));
}

void drawToggles(Store& st, Art& a)
{
    a.iconToggle[0] = toggleIcon(st, false);
    a.iconToggle[1] = toggleIcon(st, true);
}

void buildStandIn(Art& a)
{
    stores().push_back(std::make_unique<Store>());
    auto& st = *stores().back();
    a = Art{};
    a.tiny3x5 = standInFont(st, {0, 1, 2, 3, 4}, {0, 1, 2});
    a.small4x5 = a.tiny3x5;
    a.square5x5 = standInFont(st, {0, 1, 2, 3, 4}, {0, 0, 1, 2, 2});
    // the 8-row face: outer columns doubled for a heavy stem (smearing a 3-column glyph would close its counters)
    a.bold8 = standInFont(st, {0, 0, 1, 1, 2, 3, 3, 4}, {0, 0, 1, 2, 2});
    a.digitsTop = standInFont(st, {0, 0, 1, 1, 2}, {0, 0, 1, 2, 2}, "%./0123456789");
    a.digitsBottom = standInFont(st, {2, 3, 3, 4, 4}, {0, 0, 1, 2, 2}, "%./0123456789");

    const Bitmap* dial = ring(st, 11, 2);   // 11x13 like the hardware's, with a tick at 12 o'clock
    const_cast<uint64_t*>(dial->rows)[0] |= uint64_t(1) << (63 - 5);
    a.dialRing = *dial;
    a.ringPlain = *ring(st, 11, 0);
    a.groupTie = *fromText(st, {".#####.", "#.....#", "#.....#"});
    for (int v = 0; v < 128; ++v) {   // pointer: two pixels on the spoke, swept -150..+150 degrees
        std::vector<uint64_t> rows(7, 0);
        const double ang = (-150.0 + 300.0 * v / 127.0) * 3.14159265358979323846 / 180.0;
        for (double rad : {3.0, 2.0}) {
            const int x = int(std::lround(3 + rad * std::sin(ang))), y = int(std::lround(3 - rad * std::cos(ang)));
            if (x >= 0 && x < 7 && y >= 0 && y < 7) rows[size_t(y)] |= uint64_t(1) << (63 - x);
        }
        a.dialDot[v] = st.addBitmap(7, 7, std::move(rows));
    }
    drawToggles(st, a);
}

// ---------------------------------------------------------------------------------------------------
// Where Machinedrum UW OS 1.63 keeps the artwork (addresses in the main OS image, load base 0x200000; from
// mdartdump.cpp). Facts about the file's layout; the pixels themselves are only ever read from the user's copy.
//
// Icon descriptor, 20 bytes of big-endian u32: w, h, n, ->pixels, ->mask. pixels = w columns of u32, the glyph in
// the high h bits, row r (0 = top) = bit 32-h+r; mask = pixels + 4w.
// Font descriptor, 5 x u32: default advance, rows (<= 8), ->widths[256] u8, ->offsets[256] i16 (negative = no
// glyph), ->columns, one byte per column, row r = bit 8-h+r. Codes >= 128 are symbol slots, not used.

constexpr uint32_t kBase = 0x200000;
// The image these addresses belong to: OS 1.63's section 0 (its size, and its FNV-1a 64-bit hash). Any other image
// (another OS version, a patched OS) gets the stand-in face rather than whatever lies at these addresses.
constexpr size_t kOs163Size = 404766;
constexpr uint64_t kOs163Fnv = 0x63e6be2538e4eb54ull;
constexpr uint32_t kFontTiny3x5At = 0x259954, kFontSmall4x5At = 0x259968, kFontSquare5x5At = 0x25997C,
                   kFontBold8At = 0x259990, kFontDigitsTopAt = 0x2599A4, kFontDigitsBottomAt = 0x2599B8;
constexpr uint32_t kRingPlainAt = 0x2599CC, kDialRingAt = 0x2599E0, kGroupTieAt = 0x259AF8;
constexpr uint32_t kDialDotAt = 0x259B0C;   // 128 descriptors back to back
constexpr uint32_t kDescSize = 20;

uint64_t fnv1a64(const std::vector<uint8_t>& d)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (const uint8_t b : d) { h ^= b; h *= 0x100000001b3ull; }
    return h;
}

struct Reader {
    const std::vector<uint8_t>& d;
    Store& st;

    [[noreturn]] static void bad(const char* what, uint32_t at)
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s at 0x%06X", what, unsigned(at));
        throw std::runtime_error(buf);
    }
    const uint8_t* at(uint32_t addr, size_t n) const
    {
        if (addr < kBase || size_t(addr - kBase) + n > d.size()) bad("address outside the image", addr);
        return &d[addr - kBase];
    }
    uint32_t u32(uint32_t addr) const { const auto* p = at(addr, 4); return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

    const Bitmap* icon(uint32_t addr, int wantW, int wantH)
    {
        const uint32_t w = u32(addr), h = u32(addr + 4), n = u32(addr + 8), px = u32(addr + 12), mask = u32(addr + 16);
        if (w < 1 || w > 64 || h < 1 || h > 32 || n < 1 || n > 4 || mask != px + 4 * w) bad("not an icon descriptor", addr);
        if (int(w) != wantW || int(h) != wantH) bad("icon of unexpected size", addr);
        std::vector<uint64_t> rows(h, 0);
        for (uint32_t c = 0; c < w; ++c) {
            const uint32_t col = u32(px + 4 * c);
            for (uint32_t r = 0; r < h; ++r)
                if ((col >> (32 - h + r)) & 1) rows[r] |= uint64_t(1) << (63 - c);
        }
        return st.addBitmap(int(w), int(h), std::move(rows));
    }

    Font font(uint32_t addr, int wantH)
    {
        const uint32_t adv = u32(addr), h = u32(addr + 4), widthsAt = u32(addr + 8), offsetsAt = u32(addr + 12), colsAt = u32(addr + 16);
        if (int(h) != wantH || adv < 1 || adv > 16) bad("not the expected font", addr);
        const uint8_t* widths = at(widthsAt, 256);
        const uint8_t* offsets = at(offsetsAt, 512);
        st.fonts.emplace_back();
        auto& fd = st.fonts.back();
        fd.index.fill(-1);
        int have = 0;
        for (int c = 0; c < 128; ++c) {
            const int off = int16_t((offsets[2 * c] << 8) | offsets[2 * c + 1]);
            if (off < 0) continue;
            const uint32_t w = widths[c] ? widths[c] : adv;
            if (w > 16) bad("font glyph too wide", addr);
            const uint8_t* cols = at(colsAt + uint32_t(off), w);
            std::vector<uint64_t> rows(h, 0);
            for (uint32_t x = 0; x < w; ++x)
                for (uint32_t r = 0; r < h; ++r)
                    if ((cols[x] >> (8 - h + r)) & 1) rows[r] |= uint64_t(1) << (63 - x);
            fd.index[size_t(c)] = int16_t(fd.glyphs.size());
            fd.glyphs.push_back(Bitmap{uint8_t(w), uint8_t(h), st.addRows(std::move(rows))});
            ++have;
        }
        if (have < 10) bad("font without glyphs", addr);
        return Font{uint8_t(h), uint8_t(adv), fd.glyphs.data(), fd.index.data()};
    }
};

struct Installed { std::string path; std::uintmax_t size = 0; std::filesystem::file_time_type mtime{}; bool valid = false; };
Installed& installed() { static Installed i; return i; }

} // namespace

Art& art()
{
    static Art a = [] { Art x; buildStandIn(x); return x; }();
    return a;
}

bool installRomArt(const std::vector<uint8_t>& mainOs, std::string* error)
{
    // the image first: the addresses below are only meaningful in OS 1.63
    if (mainOs.size() != kOs163Size || fnv1a64(mainOs) != kOs163Fnv) {
        if (error) *error = "LCD artwork: this is not the main image of Machinedrum UW OS 1.63 (the supported version)";
        return false;
    }
    auto store = std::make_unique<Store>();
    Art a;
    try {
        Reader rd{mainOs, *store};
        a.tiny3x5 = rd.font(kFontTiny3x5At, 5);
        a.small4x5 = rd.font(kFontSmall4x5At, 5);
        a.square5x5 = rd.font(kFontSquare5x5At, 5);
        a.bold8 = rd.font(kFontBold8At, 8);
        a.digitsTop = rd.font(kFontDigitsTopAt, 5);
        a.digitsBottom = rd.font(kFontDigitsBottomAt, 5);
        a.ringPlain = *rd.icon(kRingPlainAt, 11, 11);
        a.dialRing = *rd.icon(kDialRingAt, 11, 13);
        a.groupTie = *rd.icon(kGroupTieAt, 7, 3);
        for (uint32_t v = 0; v < 128; ++v) a.dialDot[v] = rd.icon(kDialDotAt + v * kDescSize, 7, 7);
    } catch (const std::exception& e) {
        if (error) *error = std::string("LCD artwork not found in this OS image (") + e.what() + ")";
        return false;
    }
    drawToggles(*store, a);
    a.fromRom = true;
    stores().push_back(std::move(store));
    art() = a;
    return true;
}

std::vector<uint8_t> loadMainOs(const std::filesystem::path& osFile)
{
    auto c = fw::parseContainer(fw::parseSysex(fw::readFile(osFile)));
    if (c.sections.empty() || c.sections.front().index != 0) throw std::runtime_error("no main OS section in " + osFile.string());
    return std::move(c.sections.front().data);
}

Bounds litBounds(const Bitmap& b)
{
    int x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (b.lit(x, y)) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    return x1 < 0 ? Bounds{} : Bounds{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

bool ensureRomArt(const std::filesystem::path& osFile, std::string* error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(osFile, ec);
    if (ec) { if (error) *error = "cannot read " + osFile.string(); return false; }
    const auto mtime = std::filesystem::last_write_time(osFile, ec);
    auto& in = installed();
    if (in.valid && in.path == osFile.string() && in.size == size && in.mtime == mtime) return true;
    try {
        if (!installRomArt(loadMainOs(osFile), error)) return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
    in = Installed{osFile.string(), size, mtime, true};
    return true;
}

} // namespace md::uispec
