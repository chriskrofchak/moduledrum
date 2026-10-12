// MdRomArt: the LCD artwork read from the user's OS file, and the stand-in face without one. The cases run in this
// order in one process: the stand-in is checked before the OS artwork is installed.
#include "mini_test.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "MdRomArt.h"
using namespace md;

namespace {

bool hasGlyphs(const uispec::Font& f, const char* codes)
{
    for (const char* c = codes; *c; ++c) {
        const auto* g = f.glyph(static_cast<unsigned char>(*c));
        if (!g || g->h != f.h || g->w < 1) return false;
    }
    return true;
}

int litCount(const uispec::Bitmap& b)
{
    int n = 0;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) n += b.lit(x, y) ? 1 : 0;
    return n;
}

// What the UI relies on, whichever face is in use.
void checkShapes(const uispec::Art& a)
{
    CHECK_EQ(int(a.bold8.h), 8);
    for (const auto* f : {&a.small4x5, &a.tiny3x5, &a.square5x5, &a.digitsTop, &a.digitsBottom}) CHECK_EQ(int(f->h), 5);
    for (const auto* f : {&a.bold8, &a.small4x5, &a.tiny3x5, &a.square5x5}) CHECK(hasGlyphs(*f, "AZ09-"));
    CHECK(hasGlyphs(a.digitsTop, "0123456789"));
    CHECK(hasGlyphs(a.digitsBottom, "0123456789"));
    CHECK_EQ(int(a.dialRing.w), 11);
    CHECK_EQ(int(a.dialRing.h), 13);
    CHECK(litCount(a.dialRing) > 20);
    CHECK_EQ(int(a.ringPlain.w), 11);
    CHECK_EQ(int(a.ringPlain.h), 11);
    CHECK_EQ(int(a.groupTie.w), 7);
    CHECK_EQ(int(a.groupTie.h), 3);
    for (const auto* d : a.dialDot) CHECK(d && d->w == 7 && d->h == 7 && litCount(*d) > 0);
    CHECK(std::vector<uint64_t>(a.dialDot[0]->rows, a.dialDot[0]->rows + 7) != std::vector<uint64_t>(a.dialDot[127]->rows, a.dialDot[127]->rows + 7));
    for (const auto* t : a.iconToggle) CHECK(t && t->w == 17 && t->h == 13);
}

std::filesystem::path writeTemp(const char* name, const std::string& text)
{
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary) << text;
    return p;
}

} // namespace

TEST_CASE(romart_standin_without_os)
{
    const auto& a = uispec::art();
    CHECK(!a.fromRom);
    checkShapes(a);
    // a file that is not an OS file, a missing file, and an image of the right size but not OS 1.63: stand-in stays
    std::string error;
    const auto junk = writeTemp("md-romart-test-not-an-os.syx", "not a Machinedrum OS file\n");
    CHECK(!uispec::ensureRomArt(junk, &error));
    CHECK(!error.empty());
    std::filesystem::remove(junk);
    error.clear();
    CHECK(!uispec::ensureRomArt("/nonexistent/Elektron_SPS1-1UW_OS1.63.syx", &error));
    CHECK(!error.empty());
    error.clear();
    CHECK(!uispec::installRomArt(std::vector<uint8_t>(404766, 0x4E), &error));
    CHECK_MSG(error.find("1.63") != std::string::npos, error);
    CHECK(!uispec::art().fromRom);
}

TEST_CASE(romart_os163)
{
    const auto path = mt::requireOsFile();
    auto image = uispec::loadMainOs(path);
    // a patched image is refused before any address in it is trusted
    auto patched = image;
    patched[0x59990] ^= 0x01;   // the bold font's descriptor
    std::string error;
    CHECK(!uispec::installRomArt(patched, &error));
    CHECK(!uispec::art().fromRom);

    error.clear();
    CHECK_MSG(uispec::ensureRomArt(path, &error), error);
    const auto& a = uispec::art();
    CHECK(a.fromRom);
    checkShapes(a);
    // the hardware's faces: the bold face is wider than the stand-in's 5 columns somewhere, the tiny face is 3 wide
    CHECK(a.tiny3x5.glyph('A')->w <= 4);
    CHECK(a.bold8.glyph('A')->w >= 5);
    CHECK(uispec::ensureRomArt(path, &error));   // the same file again: nothing to do

    // a wrong file after the OS file keeps the OS artwork in use
    const auto junk = writeTemp("md-romart-test-not-an-os.syx", "not a Machinedrum OS file\n");
    CHECK(!uispec::ensureRomArt(junk, &error));
    std::filesystem::remove(junk);
    CHECK(uispec::art().fromRom);
}
