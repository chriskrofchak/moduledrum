// LCD-style rendering for Moduledrum: every element is drawn at hardware pixel resolution from the LCD glyphs and fonts
// (MdRomArt.h: read from the user's OS file), then scaled up with nearest-neighbour sampling so each LCD pixel stays a
// crisp square. From shnolk/monomodule src/plugin/one/Lcd.{h,cpp} (AGPL-3.0); the Monomachine-only parts (machine-group
// logos, the library's loop glyph, list icon families) are left out.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "SpecData.h"
#include "Skin.h"

namespace md::plugin::one {

namespace spec = md::uispec;

constexpr int kScale = 3;   // screen pixels per LCD pixel
// Knob cell: 32x32 pitch. Label (tiny-3x5) on row 3 (two rows of padding under the border, matching the two rows under
// the value), dial/icon centred in rows 9..22, the value row (tiny-3x5, always shown) in a 9-row box at the bottom;
// pointer frame at (2, 4) in the ring.
constexpr int kCell = 32, kLabelY = 3, kContentY = 9, kContentH = 14, kValueBoxY = 23, kValueBoxH = 9, kDotX = 2, kDotY = 4;

// The two colours everything is drawn in: the skin's (Skin.h). Read at paint time, never cached across paints.
namespace lcd {
inline juce::Colour& paper = md::plugin::skin::paperColour();
inline juce::Colour& ink = md::plugin::skin::inkColour();
}

class LcdCanvas {
public:
    LcdCanvas(int w, int h) : m_w(w), m_h(h), m_px(size_t(w * h), 0) {}
    int width() const { return m_w; }
    int height() const { return m_h; }
    void clear(bool on = false) { std::fill(m_px.begin(), m_px.end(), uint8_t(on ? 1 : 0)); }
    void set(int x, int y, bool on = true) { if (x >= 0 && y >= 0 && x < m_w && y < m_h) m_px[size_t(y * m_w + x)] = on ? 1 : 0; }
    bool get(int x, int y) const { return x >= 0 && y >= 0 && x < m_w && y < m_h && m_px[size_t(y * m_w + x)] != 0; }
    // Composites the bitmap's lit pixels (only), like the hardware's masked draw; on=false draws them as paper.
    void blit(const spec::Bitmap& b, int x, int y, bool on = true);
    void fillRect(int x, int y, int w, int h, bool on = true);
    void invertRect(int x, int y, int w, int h);
    // Greys what is drawn in the rectangle, as a 1-bit display can: every other lit pixel (a checkerboard) goes dark.
    void shade(int x, int y, int w, int h);
    void line(int x0, int y0, int x1, int y1, bool on = true);   // straight line, both ends included
    void dotsH(int x0, int x1, int y);   // dotted rule, every other pixel from x0 to x1 inclusive
    void dotsV(int x, int y0, int y1);
    void dottedFrame(int x, int y, int w, int h) { dotsH(x, x + w - 1, y); dotsH(x, x + w - 1, y + h - 1); dotsV(x, y, y + h - 1); dotsV(x + w - 1, y, y + h - 1); }
    static int textWidth(const spec::Font& f, const char* s);
    void text(const spec::Font& f, const char* s, int x, int y, bool on = true);   // 1-px gap between glyphs
    void textCentred(const spec::Font& f, const char* s, int x0, int w, int y, bool on = true)
    {
        text(f, s, x0 + (w - textWidth(f, s)) / 2, y, on);
    }
    void tallDigits(const char* s, int x, int y, bool on = true);   // digits-top over digits-bottom (10 rows)
    static int tallDigitsWidth(const char* s) { return textWidth(spec::kFontDigitsTop, s); }
    static constexpr int kTallDigitsH = 10;
    void draw(juce::Graphics& g, int destX, int destY, int scale = kScale) const;

private:
    int m_w, m_h;
    std::vector<uint8_t> m_px;
};

// Draws one knob cell at cell origin (x0, y0): dotted top/left border, then label, dial, icon, word or shape, and the
// value row; nothing but the border when blank. `raw` is the parameter's raw value (a dial shows it as one of 128
// pointer frames, so a parameter with another range passes its position as 0-127 with maxRaw 127). `text`, when
// given, is printed instead of the value's own text (a tempo, a name the page looks up). `highlightValue` inverts the
// value box (hover/drag/edit).
void drawKnobCell(LcdCanvas& cv, int x0, int y0, const spec::Param& p, int raw, bool highlightValue = false, const char* text = nullptr);
// The value box of a cell at (x0, y0), in LCD pixels (the click target for editing): the row under a dial, or the word
// or shape a Text / Shape cell shows in the dial's place.
inline juce::Rectangle<int> knobValueBox(int x0, int y0, spec::Display d = spec::Display::Numeric)
{
    if (d == spec::Display::Text || d == spec::Display::Shape) return {x0 + 1, y0 + kContentY + 6, kCell - 1, 11};
    return {x0 + 1, y0 + kValueBoxY, kCell - 1, kValueBoxH};
}
// An LFO shape (spec::kLfoShapeNames order), 17x9 LCD px with its top-left at (x, y)
constexpr int kLfoShapeW = 17, kLfoShapeH = 9;
void drawLfoShape(LcdCanvas& cv, int x, int y, int shape, bool on = true);

// Hardware-style value string for a raw 0..127 value: raw, raw-64, or the list/readout name.
juce::String valueText(const spec::Param& p, int raw);

// Reads the LCD artwork (fonts, dial) from the user's OS file if it is not in use already; see MdRomArt.h. Message
// thread only. Returns true when the artwork of that file is in use (the caller then re-lays out and repaints); with
// an empty or unusable path the face in use stays.
bool loadLcdArt(const juce::String& osPath);

// LCD-font text composited over whatever is underneath (only the lit pixels are painted), `scale` screen px per LCD px.
void drawLcdText(juce::Graphics& g, const spec::Font& f, const char* s, int x, int y, int scale, juce::Colour colour);
// Greedy word wrap of (caps) text to maxWidth LCD pixels; a word wider than the line stands alone.
std::vector<juce::String> wrapLcdText(const spec::Font& f, const juce::String& text, int maxWidth);

} // namespace md::plugin::one
