// LCD-style rendering (see Lcd.h). From shnolk/monomodule src/plugin/one/Lcd.cpp (AGPL-3.0).
#include "Lcd.h"
#include <algorithm>
#include <cmath>

namespace md::plugin::one {

void LcdCanvas::blit(const spec::Bitmap& b, int x, int y, bool on)
{
    for (int r = 0; r < b.h; ++r)
        for (int c = 0; c < b.w; ++c)
            if (b.lit(c, r)) set(x + c, y + r, on);
}

void LcdCanvas::fillRect(int x, int y, int w, int h, bool on)
{
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) set(x + c, y + r, on);
}

void LcdCanvas::invertRect(int x, int y, int w, int h)
{
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) set(x + c, y + r, !get(x + c, y + r));
}

void LcdCanvas::shade(int x, int y, int w, int h)
{
    for (int r = y; r < y + h; ++r)
        for (int c = x; c < x + w; ++c)
            if (((r + c) & 1) != 0) set(c, r, false);
}

void LcdCanvas::line(int x0, int y0, int x1, int y1, bool on)
{
    const int n = std::max({std::abs(x1 - x0), std::abs(y1 - y0), 1});
    for (int i = 0; i <= n; ++i)
        set(x0 + int(std::lround(double((x1 - x0) * i) / n)), y0 + int(std::lround(double((y1 - y0) * i) / n)), on);
}

void LcdCanvas::dotsH(int x0, int x1, int y) { for (int x = x0; x <= x1; x += 2) set(x, y); }
void LcdCanvas::dotsV(int x, int y0, int y1) { for (int y = y0; y <= y1; y += 2) set(x, y); }

int LcdCanvas::textWidth(const spec::Font& f, const char* s)
{
    int w = 0;
    for (const char* c = s; *c; ++c) {
        const auto* g = f.glyph(static_cast<unsigned char>(*c));
        w += (g ? g->w : f.adv) + 1;
    }
    return w > 0 ? w - 1 : 0;
}

void LcdCanvas::text(const spec::Font& f, const char* s, int x, int y, bool on)
{
    for (const char* c = s; *c; ++c) {
        const auto* g = f.glyph(static_cast<unsigned char>(*c));
        if (g) { blit(*g, x, y, on); x += g->w + 1; }
        else x += f.adv + 1;
    }
}

void LcdCanvas::tallDigits(const char* s, int x, int y, bool on)
{
    text(spec::kFontDigitsTop, s, x, y, on);
    text(spec::kFontDigitsBottom, s, x, y + spec::kFontDigitsTop.h, on);
}

void LcdCanvas::draw(juce::Graphics& g, int destX, int destY, int scale) const
{
    juce::Image img(juce::Image::ARGB, m_w, m_h, false);
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < m_h; ++y)
            for (int x = 0; x < m_w; ++x)
                bd.setPixelColour(x, y, m_px[size_t(y * m_w + x)] ? lcd::ink : lcd::paper);
    }
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed(img, juce::AffineTransform::scale(float(scale)).translated(float(destX), float(destY)));
}

// ---------------------------------------------------------------------------

bool loadLcdArt(const juce::String& osPath)
{
    return osPath.isNotEmpty() && spec::ensureRomArt(std::filesystem::path(osPath.toStdString()));
}

juce::String valueText(const spec::Param& p, int raw)
{
    raw = juce::jlimit(0, int(p.maxRaw), raw);
    switch (p.display) {
    case spec::Display::List:
    case spec::Display::Readout:
    case spec::Display::Text:
    case spec::Display::Shape:
        if (const char* name = spec::valueName(p, raw)) return juce::String(name);
        return juce::String(raw);
    case spec::Display::Bipolar: { const int b = raw - 64; return b > 0 ? "+" + juce::String(b) : juce::String(b); }
    case spec::Display::Blank:
    case spec::Display::Numeric: break;
    }
    return juce::String(raw);
}

// Rotary switch for an N-position list without its own glyphs: the plain ring with a pointer line from the centre to
// position idx, swept like the dial (first entry lower-left, last lower-right).
static void drawSwitch(LcdCanvas& cv, int x, int y, int idx, int n)
{
    cv.blit(spec::kRingPlain, x, y);
    const double frac = n > 1 ? double(idx) / double(n - 1) : 0.5;
    const double a = juce::degreesToRadians(-150.0 + 300.0 * frac);   // clockwise from 12 o'clock
    const int cx = x + spec::kRingPlain.w / 2, cy = y + spec::kRingPlain.h / 2;
    for (int t = 1; t <= 4; ++t)
        cv.set(cx + int(std::lround(t * std::sin(a))), cy - int(std::lround(t * std::cos(a))));
}

// The MD's six LFO shapes as its LFO page draws them (sd88me/mpc-vst-machinedrum tools/mdskin/mk_skin.py, draw_shape):
// triangle, saw, square, ramp, exponential decay, random steps.
void drawLfoShape(LcdCanvas& cv, int x, int y, int shape, bool on)
{
    const int b = y + kLfoShapeH - 1;   // bottom row
    switch (juce::jlimit(0, spec::kLfoShapes - 1, shape)) {
    case 0: cv.line(x, b, x + 8, y, on); cv.line(x + 8, y, x + 16, b, on); break;
    case 1:
        cv.line(x, b, x + 7, y, on); cv.line(x + 7, y, x + 7, b, on);
        cv.line(x + 8, b, x + 15, y, on); cv.line(x + 15, y, x + 15, b, on);
        break;
    case 2:
        cv.line(x, b, x + 3, b, on); cv.line(x + 3, b, x + 3, y, on); cv.line(x + 3, y, x + 11, y, on);
        cv.line(x + 11, y, x + 11, b, on); cv.line(x + 11, b, x + 16, b, on);
        break;
    case 3: cv.line(x, y, x + 3, y, on); cv.line(x + 3, y, x + 11, b, on); cv.line(x + 11, b, x + 16, b, on); break;
    case 4:
        cv.line(x, b, x, y, on);
        for (int c = 1; c < kLfoShapeW; ++c) cv.set(x + c, y + int(std::lround(8.0 * (1.0 - std::exp(-c / 2.5)))), on);
        break;
    default: {
        static constexpr int kSteps[6] = {6, 2, 5, 0, 8, 3};
        for (int i = 0; i < 6; ++i) {
            cv.line(x + i * 3, y + kSteps[i], x + i * 3 + 2, y + kSteps[i], on);
            if (i > 0) cv.line(x + i * 3, y + kSteps[i - 1], x + i * 3, y + kSteps[i], on);
        }
        break;
    }
    }
}

void drawKnobCell(LcdCanvas& cv, int x0, int y0, const spec::Param& p, int raw, bool highlightValue, const char* text)
{
    raw = juce::jlimit(0, int(p.maxRaw), raw);
    // dotted top and left edge (the grid's right/bottom edges are the neighbours' left/top)
    cv.dotsH(x0, x0 + kCell, y0);
    cv.dotsV(x0, y0, y0 + kCell - 1);
    if (p.display == spec::Display::Blank) return;   // nothing at all, not even the label

    const auto& font = spec::kFontTiny3x5;
    const int innerX = x0 + 1, innerW = kCell - 1;
    cv.textCentred(font, p.label, innerX, innerW, y0 + kLabelY);

    const auto box = knobValueBox(x0, y0, p.display);
    if (p.display == spec::Display::Text) {   // the value as a word in the dial's place (no value row), as the MD's LFO page
        const juce::String word = text != nullptr ? juce::String(text) : valueText(p, raw);
        cv.textCentred(font, word.toRawUTF8(), box.getX(), box.getWidth(), box.getY() + (box.getHeight() - font.h) / 2);
        if (highlightValue) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
        return;
    }
    if (p.display == spec::Display::Shape) {   // the LFO shape in the dial's place
        drawLfoShape(cv, innerX + (innerW - kLfoShapeW) / 2, box.getY() + (box.getHeight() - kLfoShapeH) / 2, raw);
        if (highlightValue) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
        return;
    }
    if (p.display == spec::Display::Numeric || p.display == spec::Display::Bipolar) {
        const int rx = innerX + (innerW - spec::kDialRing.w) / 2, ry = y0 + kContentY + (kContentH - spec::kDialRing.h) / 2;
        const int frame = p.maxRaw == 127 ? raw : raw * 127 / juce::jmax(1, int(p.maxRaw));   // a shorter range spans the dial
        cv.blit(spec::kDialRing, rx, ry);
        cv.blit(*spec::kDialDot[frame], rx + kDotX, ry + kDotY);
    } else {
        const int idx = p.display == spec::Display::List ? spec::listIndex(raw, p.valueCount) : raw;
        if (const auto* ic = spec::icon(p.icons, idx))
            cv.blit(*ic, innerX + (innerW - ic->w) / 2, y0 + kContentY + (kContentH - ic->h) / 2);
        else if (p.display == spec::Display::List)
            drawSwitch(cv, innerX + (innerW - spec::kRingPlain.w) / 2, y0 + kContentY + (kContentH - spec::kRingPlain.h) / 2, idx, p.valueCount);
    }
    // value row: the hardware prints no number in a dial cell, but the plugin shows every value here
    const juce::String value = text != nullptr ? juce::String(text) : valueText(p, raw);
    cv.textCentred(font, value.toRawUTF8(), box.getX(), box.getWidth(), box.getY() + (box.getHeight() - font.h) / 2);
    if (highlightValue) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
    if (p.tieRight) cv.blit(spec::kGroupTie, x0 + kCell - 3, y0 - 1);   // arch straddling the right divider
}

void drawLcdText(juce::Graphics& g, const spec::Font& f, const char* s, int x, int y, int scale, juce::Colour colour)
{
    const int w = LcdCanvas::textWidth(f, s), h = f.h;
    if (w <= 0) return;
    LcdCanvas cv(w, h);
    cv.text(f, s, 0, 0);
    g.setColour(colour);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            if (cv.get(c, r)) g.fillRect(x + c * scale, y + r * scale, scale, scale);
}

std::vector<juce::String> wrapLcdText(const spec::Font& f, const juce::String& text, int maxWidth)
{
    std::vector<juce::String> lines;
    juce::String line;
    for (const auto& word : juce::StringArray::fromTokens(text, " ", "")) {
        if (word.isEmpty()) continue;
        const juce::String candidate = line.isEmpty() ? word : line + " " + word;
        if (line.isEmpty() || LcdCanvas::textWidth(f, candidate.toRawUTF8()) <= maxWidth) line = candidate;
        else { lines.push_back(line); line = word; }
    }
    if (line.isNotEmpty()) lines.push_back(line);
    return lines;
}

} // namespace md::plugin::one
