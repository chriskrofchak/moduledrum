#include "OneEditor.h"
#include <cmath>
#include <cstring>

namespace md::plugin::one {

namespace {
// The chassis around the LCD (after mk_skin.py's, from a photo of the MD): brushed aluminium, a glossy black bezel, the
// LCD's backlight darkening towards its edge. Its print and labels are dark grey on the aluminium.
const juce::Colour kAluTop(0xffbdbdbb), kAluBottom(0xffa2a2a0), kBezelFill(0xff070607), kBezelOuter(0xff505052),
    kBezelInner(0xff18181a), kText(0xff1c1c1c), kDim(0xff4c4c4c), kError(0xff9c1a10), kPrint(0xff2c2c2c);
constexpr const char* kProductName = "MODULEDRUM";   // on the LCD, top right, as the MPC skin has its name

// The GLOBAL page's cells that are not raw 0-127 knobs
constexpr const char* const kNoteMapShort[2] = {"WHITE", "CHROM"};
constexpr double kTempoMin = 30.0, kTempoMax = 300.0;

bool typedDisplay(spec::Display d) { return d == spec::Display::Numeric || d == spec::Display::Bipolar; }

spec::Param cellParam(const char* label, spec::Display display, int defaultRaw, int maxRaw, const char* const* values = nullptr)
{
    spec::Param p = spec::kBlankParam;
    std::strncpy(p.label, label, sizeof p.label - 1);
    p.display = display;
    p.defaultRaw = uint8_t(defaultRaw);
    p.maxRaw = uint8_t(maxRaw);
    p.valueCount = uint8_t(display == spec::Display::Numeric || display == spec::Display::Bipolar ? 128 : maxRaw + 1);
    p.values = values;
    return p;
}
} // namespace

// ---------------------------------------------------------------------------
// KnobCell

KnobCell::KnobCell()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(int(kDragPixels));
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);   // drag in any direction
    setOpaque(false);
}

void KnobCell::mouseMove(const juce::MouseEvent& e)
{
    Slider::mouseMove(e);
    setMouseCursor(m_valueArea.contains(e.getPosition()) ? m_valueCursor : juce::MouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor));
}

double KnobCell::snapped(double v, bool fine) const
{
    const double step = fine && m_fineStep > 0.0 ? m_fineStep : m_step;
    return step > 0.0 ? std::round(v / step) * step : v;   // else the slider snaps to its own interval
}

void KnobCell::mouseDown(const juce::MouseEvent& e)
{
    Slider::mouseDown(e);   // the gesture begins (the attachment's beginChangeGesture); the press itself turns nothing
    m_dragValue = getValue();
    m_lastDrag = e.position;
}

// The drag since the last event, not since the press: shift can come and go mid-drag without the value jumping.
void KnobCell::mouseDrag(const juce::MouseEvent& e)
{
    if (!isEnabled() || getMaximum() <= getMinimum()) return;
    const double d = double(e.position.x - m_lastDrag.x) + double(m_lastDrag.y - e.position.y);   // up or right: up
    m_lastDrag = e.position;
    const bool fine = e.mods.isShiftDown();
    const double span = m_span > 0.0 ? m_span : getMaximum() - getMinimum();
    m_dragValue = juce::jlimit(getMinimum(), getMaximum(), m_dragValue + d * span / (kDragPixels * (fine ? kFine : 1.0)));
    setValue(snapped(m_dragValue, fine), juce::sendNotificationSync);
}

void KnobCell::mouseUp(const juce::MouseEvent& e)
{
    Slider::mouseUp(e);   // the gesture ends
    // a click that did not turn the knob, released on the value, edits the value
    if (onValueClick && !e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1
        && m_valueArea.contains(e.getMouseDownPosition()) && m_valueArea.contains(e.getPosition()))
        onValueClick();
}

void KnobCell::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (m_valueArea.contains(e.getPosition())) return;   // double-click on the value is editing, not reset
    Slider::mouseDoubleClick(e);                          // the parameter's default
}

// Monomodule's wheel (the slider's own) for the knobs; with shift, and for the tempo, one step per notch
void KnobCell::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const bool fine = e.mods.isShiftDown();
    if (!fine && m_step <= 0.0) { Slider::mouseWheelMove(e, wheel); return; }
    const float amount = (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY) * (wheel.isReversed ? -1.0f : 1.0f);
    if (!isEnabled() || amount == 0.0f) return;
    const double step = fine ? (m_fineStep > 0.0 ? m_fineStep : getInterval()) : m_step;
    const ScopedDragNotification gesture(*this);
    setValue(juce::jlimit(getMinimum(), getMaximum(), getValue() + (amount > 0.0f ? step : -step)), juce::sendNotificationSync);
}

// ---------------------------------------------------------------------------
// KnobPage

KnobPage::KnobPage(const char* title, int numCells) : m_title(title)
{
    for (int k = 0; k < numCells; ++k) {
        auto c = std::make_unique<Cell>();
        c->slider.onValueChange = [this] { repaint(); };
        c->slider.onValueClick = [this, k] {
            if (typedDisplay(m_cells[size_t(k)]->d.display)) beginEdit(k);
            else showValueList(k);
        };
        addChildComponent(c->slider);   // shown once bound
        m_cells.push_back(std::move(c));
    }
    m_editor.setJustification(juce::Justification::centred);
    m_editor.setInputRestrictions(6, "0123456789+-.");
    m_editor.setSelectAllWhenFocused(true);
    m_editor.setFont(juce::Font(juce::FontOptions(float(6 * kScale), juce::Font::bold)));   // two thirds of the value box
    m_editor.setIndents(2, 1);
    m_editor.onReturnKey = [this] { endEdit(true); };
    m_editor.onEscapeKey = [this] { endEdit(false); };
    m_editor.onFocusLost = [this] { endEdit(true); };
    addChildComponent(m_editor);
    setOpaque(true);
}

KnobPage::~KnobPage()
{
    if (m_listenedTop != nullptr) m_listenedTop->removeMouseListener(this);
}

void KnobPage::bind(int k, juce::RangedAudioParameter* param, const spec::Param& d, TextFn text)
{
    auto& c = *m_cells[size_t(k)];
    if (m_editing == k) endEdit(false);
    c.attach.reset();
    c.param = param;
    c.text = std::move(text);
    // the slider takes the parameter's range and value; turning it is a gesture on the parameter, a change from the
    // host or automation moves it (on the message thread)
    if (param != nullptr) c.attach = std::make_unique<juce::SliderParameterAttachment>(*param, c.slider);
    describe(k, d);
}

void KnobPage::describe(int k, const spec::Param& d)
{
    auto& c = *m_cells[size_t(k)];
    c.d = d;
    c.slider.setVisible(c.param != nullptr && d.display != spec::Display::Blank);   // a blank knob is dead, as on the MD
    const auto area = cellArea(k), box = valueBox(k);
    c.slider.setValueArea((box - area.getPosition() - juce::Point<int>(1, 1)) * kScale,
                          typedDisplay(d.display) ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
    if (c.param != nullptr) c.slider.setDoubleClickReturnValue(true, c.param->convertFrom0to1(c.param->getDefaultValue()));
    c.shown = cellText(k);
    repaint();
}

void KnobPage::setMessage(const juce::StringArray& lines)
{
    if (lines != m_message) { m_message = lines; repaint(); }
}

void KnobPage::setBadge(const juce::String& badge)
{
    if (badge != m_badge) { m_badge = badge; repaint(); }
}

void KnobPage::refreshText()
{
    bool changed = false;
    for (int k = 0; k < numCells(); ++k) {
        auto& c = *m_cells[size_t(k)];
        if (!c.text) continue;
        const auto s = cellText(k);
        if (s != c.shown) { c.shown = s; changed = true; }
    }
    if (changed) repaint();
}

int KnobPage::rawOf(int k) const
{
    const auto& c = *m_cells[size_t(k)];
    if (c.param == nullptr) return c.d.defaultRaw;
    const double lo = c.slider.getMinimum(), hi = c.slider.getMaximum(), v = c.slider.getValue();
    if (juce::roundToInt(hi - lo) == c.d.maxRaw && c.slider.getInterval() >= 1.0) return juce::roundToInt(v - lo);   // raw steps
    return hi > lo ? juce::roundToInt((v - lo) / (hi - lo) * c.d.maxRaw) : 0;   // a position on the dial (the tempo)
}

juce::String KnobPage::cellText(int k) const
{
    const auto& c = *m_cells[size_t(k)];
    return c.text ? c.text(c.slider.getValue()) : valueText(c.d, rawOf(k));
}

bool KnobPage::isHot(int k) const
{
    const auto& s = m_cells[size_t(k)]->slider;
    return s.isVisible() && (s.isMouseOverOrDragging() || m_editing == k);
}

juce::Rectangle<int> KnobPage::cellArea(int k) const { return {(k % 4) * kCell, kGridY + (k / 4) * kCell, kCell, kCell}; }

juce::Rectangle<int> KnobPage::valueBox(int k) const
{
    const auto a = cellArea(k);
    return knobValueBox(a.getX(), a.getY(), m_cells[size_t(k)]->d.display);
}

void KnobPage::beginEdit(int k)
{
    if (m_editing >= 0) endEdit(true);
    m_editing = k;
    m_editor.setColour(juce::TextEditor::backgroundColourId, lcd::ink);   // the value box, inverted while typing
    m_editor.setColour(juce::TextEditor::textColourId, lcd::paper);
    m_editor.setColour(juce::TextEditor::highlightColourId, lcd::paper.withAlpha(0.35f));
    m_editor.setColour(juce::TextEditor::highlightedTextColourId, lcd::paper);
    m_editor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    m_editor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    m_editor.setColour(juce::CaretComponent::caretColourId, lcd::paper);
    m_editor.setText(cellText(k), false);
    m_editor.applyColourToAllText(lcd::paper);
    m_editor.setBounds(valueBox(k) * kScale);
    m_editor.setVisible(true);
    if (isShowing()) m_editor.grabKeyboardFocus();
    m_editor.selectAll();
    // clicks that land on components which do not take keyboard focus (the knobs, the page itself) never make the
    // editor lose focus, so watch the whole window for the next press
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this) { m_listenedTop = top; top->addMouseListener(this, true); }
    repaint();
}

void KnobPage::endEdit(bool commit)
{
    if (m_editing < 0) return;
    const int k = m_editing;
    m_editing = -1;
    if (m_listenedTop != nullptr) m_listenedTop->removeMouseListener(this);
    m_listenedTop = nullptr;
    m_editor.setVisible(false);
    if (commit) setTyped(k, m_editor.getText().trim());
    repaint();
}

// "+3" -> 3, "-19" -> -19 (a bipolar knob's value: raw 45), "133.5" (the tempo); out-of-range input snaps to the
// displayed range's limits. One gesture on the parameter.
void KnobPage::setTyped(int k, const juce::String& text)
{
    auto& c = *m_cells[size_t(k)];
    if (c.param == nullptr || !text.containsAnyOf("0123456789")) return;
    double v = c.d.display == spec::Display::Bipolar ? double(juce::jlimit(-64, 63, text.getIntValue()) + 64)
             : c.slider.getInterval() < 1.0     ? text.getDoubleValue()
                                                : double(text.getIntValue());
    v = juce::jlimit(c.slider.getMinimum(), c.slider.getMaximum(), v);
    const juce::Slider::ScopedDragNotification gesture(c.slider);
    c.slider.setValue(v, juce::sendNotificationSync);
}

// A cell that names its values: pick one by name from a menu under it (one gesture on the parameter)
void KnobPage::showValueList(int k)
{
    auto& c = *m_cells[size_t(k)];
    if (c.param == nullptr) return;
    std::vector<int> raws;
    if (c.d.display == spec::Display::List)
        for (int i = 0; i < c.d.valueCount; ++i) raws.push_back(spec::listRawMid(i, c.d.valueCount));
    else
        for (int r = juce::roundToInt(c.slider.getMinimum()); r <= juce::roundToInt(c.slider.getMaximum()); ++r) raws.push_back(r);
    const int current = juce::roundToInt(c.slider.getValue());
    juce::PopupMenu m;
    for (size_t i = 0; i < raws.size(); ++i) {
        const int r = raws[i];
        const bool ticked = c.d.display == spec::Display::List ? spec::listIndex(current, c.d.valueCount) == int(i) : r == current;
        m.addItem(int(i) + 1, c.text ? c.text(r) : valueText(c.d, r), true, ticked);
    }
    // the target component scales the menu with the window (ScaledEditor); the area places it under the value
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(valueBox(k) * kScale))
                        .withMinimumWidth(kCell * kScale),
        [safe = juce::Component::SafePointer<KnobPage>(this), k, raws](int result) {
            if (safe == nullptr || result <= 0) return;
            auto& s = safe->cell(k);
            const juce::Slider::ScopedDragNotification gesture(s);
            s.setValue(double(raws[size_t(result - 1)]), juce::sendNotificationSync);
        });
}

void KnobPage::mouseDown(const juce::MouseEvent& e)
{
    if (m_editing < 0) return;
    auto* c = e.eventComponent;
    if (c == &m_editor || m_editor.isParentOf(c)) return;
    endEdit(true);
}

void KnobPage::paintTitle(LcdCanvas& cv) const
{
    cv.fillRect(0, 0, cv.width(), kTitleH, true);   // the full-width title bar
    cv.text(spec::kFontBold8, m_title.toRawUTF8(), 2, 1, false);
    if (m_badge.isNotEmpty()) {   // paper box with ink text inside the inverted bar
        const auto& f = spec::kFontTiny3x5;
        const int bw = LcdCanvas::textWidth(f, m_badge.toRawUTF8()) + 4, bh = f.h + 2;
        cv.fillRect(cv.width() - bw - 2, (kTitleH - bh) / 2, bw, bh, false);
        cv.text(f, m_badge.toRawUTF8(), cv.width() - bw, (kTitleH - bh) / 2 + 1, true);
    }
}

void KnobPage::paintCells(LcdCanvas& cv) const
{
    for (int k = 0; k < numCells(); ++k) {
        const auto a = cellArea(k);
        const auto& c = *m_cells[size_t(k)];
        const auto text = c.text ? cellText(k) : juce::String();
        drawKnobCell(cv, a.getX(), a.getY(), c.d, rawOf(k), isHot(k), c.text ? text.toRawUTF8() : nullptr);
    }
    cv.dotsV(kLcdW - 1, kGridY, kLcdH - 1);   // grid right edge
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);        // grid bottom edge
}

void KnobPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(getWidth() / kScale, getHeight() / kScale);
    paintTitle(cv);
    if (m_message.isEmpty()) {
        paintCells(cv);
    } else {   // the page's frame with the text centred in it, a blank line between paragraphs
        const int w = cv.width(), h = cv.height();
        cv.dottedFrame(0, kGridY, w, h - kGridY);
        const auto& f = spec::kFontSmall4x5;
        constexpr int kLineH = 8;
        std::vector<juce::String> lines;
        for (const auto& l : m_message) {
            if (l.isEmpty()) { lines.push_back({}); continue; }
            for (const auto& wl : wrapLcdText(f, l.toUpperCase(), w - 8)) lines.push_back(wl);
        }
        int y = kGridY + (h - kGridY - (int(lines.size()) * kLineH - (kLineH - f.h))) / 2;
        for (const auto& l : lines) { cv.textCentred(f, l.toRawUTF8(), 0, w, y); y += kLineH; }
    }
    cv.draw(g, 0, 0);
}

void KnobPage::resized()
{
    for (int k = 0; k < numCells(); ++k) {
        const auto a = cellArea(k);
        m_cells[size_t(k)]->slider.setBounds((a.getX() + 1) * kScale, (a.getY() + 1) * kScale, (a.getWidth() - 1) * kScale, (a.getHeight() - 1) * kScale);
    }
    if (m_editing >= 0) m_editor.setBounds(valueBox(m_editing) * kScale);
}

// ---------------------------------------------------------------------------
// MixerPage

namespace {
// A channel's rows, LCD px from the page's top: the track's number, its machine, the fader's dotted frame, the value
constexpr int kMixNumberY = 14, kMixNameY = 21, kMixFrameY = 28, kMixFrameH = 36, kMixFrameW = 9;
}

MixerPage::MixerPage() : KnobPage("MIXER", 16)
{
    static_assert(MixerPage::kLcdW == OneView::kLcdContentW, "the mixer spans the two pages and their gap");
    for (int k = 0; k < 16; ++k) cell(k).setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void MixerPage::setMachineName(int track, const juce::String& shortName)
{
    if (m_names[size_t(track)] != shortName) { m_names[size_t(track)] = shortName; repaint(); }
}

juce::Rectangle<int> MixerPage::cellArea(int k) const
{
    // 16 channels across the page, 16 or 17 px each (the page is two pages and their gap wide)
    const int x0 = k * (kLcdW - 1) / 16, x1 = (k + 1) * (kLcdW - 1) / 16;
    return {x0, kGridY, x1 - x0, kLcdH - 1 - kGridY};
}

juce::Rectangle<int> MixerPage::valueBox(int k) const
{
    const auto a = cellArea(k);
    return {a.getX() + 1, a.getBottom() - kValueBoxH, a.getWidth() - 1, kValueBoxH};
}

void MixerPage::paintCells(LcdCanvas& cv) const
{
    const auto& tiny = spec::kFontTiny3x5;
    for (int k = 0; k < numCells(); ++k) {
        const auto a = cellArea(k);
        const int x0 = a.getX(), w = a.getWidth();
        cv.dotsH(x0, a.getRight(), a.getY());
        cv.dotsV(x0, a.getY(), kLcdH - 1);
        cv.textCentred(tiny, juce::String(k + 1).toRawUTF8(), x0 + 1, w - 1, kMixNumberY);
        cv.textCentred(tiny, m_names[size_t(k)].toRawUTF8(), x0 + 1, w - 1, kMixNameY);
        // the fader: Monomodule's LEV bar, a dotted frame filled from the bottom to the level
        const int fx = x0 + 1 + (w - 1 - kMixFrameW) / 2;
        cv.dottedFrame(fx, kMixFrameY, kMixFrameW, kMixFrameH);
        const int innerH = kMixFrameH - 4, level = juce::roundToInt(rawOf(k) * innerH / 127.0);
        cv.fillRect(fx + 2, kMixFrameY + 2 + innerH - level, kMixFrameW - 4, level, true);
        const auto box = valueBox(k);
        cv.textCentred(tiny, cellText(k).toRawUTF8(), box.getX(), box.getWidth(), box.getY() + (box.getHeight() - tiny.h) / 2);
        if (isHot(k)) cv.invertRect(box.getX(), box.getY(), box.getWidth(), box.getHeight());
    }
    cv.dotsV(kLcdW - 1, kGridY, kLcdH - 1);
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);
}

// ---------------------------------------------------------------------------
// MachineBar

void MachineBar::setMachine(const juce::String& group, const juce::String& name)
{
    if (group == m_group && name == m_name) return;
    m_group = group;
    m_name = name;
    setMouseCursor(m_group.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (onContentChanged) onContentChanged();   // re-layout: the block's width follows the name
    repaint();
}

int MachineBar::widthLcd() const
{
    const auto& f = spec::kFontBold8;
    int w = kTextX + LcdCanvas::textWidth(f, m_name.toRawUTF8()) + kPadR;
    if (m_group.isNotEmpty()) w += LcdCanvas::textWidth(f, m_group.toRawUTF8()) + kNameGap + kArrowGap + kArrowW;
    return w;
}

void MachineBar::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, true);
    const auto& f = spec::kFontBold8;
    const int ty = (h - f.h) / 2;
    int x = kTextX;
    if (m_group.isNotEmpty()) {
        cv.text(f, m_group.toRawUTF8(), x, ty, false);
        x += LcdCanvas::textWidth(f, m_group.toRawUTF8()) + kNameGap;
    }
    cv.text(f, m_name.toRawUTF8(), x, ty, false);
    if (m_group.isNotEmpty()) {   // the picker arrow after the name: down when closed, up while the picker is open
        const int ax = x + LcdCanvas::textWidth(f, m_name.toRawUTF8()) + kArrowGap, ay = h / 2 - 1;
        for (int r = 0; r < 3; ++r) {
            const int half = m_open ? r : 2 - r;   // half-width of this row: 0,1,2 up / 2,1,0 down
            for (int c = 2 - half; c <= 2 + half; ++c) cv.set(ax + c, ay + r, false);
        }
    }
    cv.draw(g, 0, 0);
}

void MachineBar::mouseDown(const juce::MouseEvent&)
{
    if (m_group.isNotEmpty() && onOpen) onOpen();
}

// ---------------------------------------------------------------------------
// TrackStrip

juce::Rectangle<int> TrackStrip::keyArea(int key) const
{
    if (key == kGlobal) return {0, 0, kGlobalW, kLcdH};
    return {kGlobalW + key * kCellW, 0, kCellW, kLcdH};
}

int TrackStrip::keyAt(juce::Point<int> local) const
{
    const auto lcd = local / kScale;
    for (int key = 0; key <= kGlobal; ++key)
        if (keyArea(key).contains(lcd)) return key;
    return -1;
}

void TrackStrip::setMachineName(int track, const juce::String& shortName)
{
    if (m_names[size_t(track)] != shortName) { m_names[size_t(track)] = shortName; repaint(); }
}

void TrackStrip::triggered(uint32_t tracks)
{
    if (tracks == 0) return;
    for (int t = 0; t < 16; ++t)
        if ((tracks >> t) & 1u) m_flash[size_t(t)] = kFlashTicks;
    repaint();
}

void TrackStrip::tick()
{
    bool changed = false;
    for (auto& f : m_flash)
        if (f > 0 && --f == 0) changed = true;
    if (changed) repaint();
}

uint32_t TrackStrip::flashing() const
{
    uint32_t bits = 0;
    for (int t = 0; t < 16; ++t) bits |= m_flash[size_t(t)] > 0 ? 1u << t : 0u;
    return bits;
}

void TrackStrip::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    const auto& bold = spec::kFontBold8;
    for (int key = 0; key <= kGlobal; ++key) {
        const auto r = keyArea(key);
        cv.dotsH(r.getX(), r.getRight(), 0);
        cv.dotsV(r.getX(), 0, kLcdH - 1);
        const bool sel = key == m_selected;
        if (sel) cv.fillRect(r.getX() + 1, 1, r.getWidth() - 1, kLcdH - 2, true);
        const bool ink = !sel;   // drawing colour inside the key
        if (key == kGlobal) { cv.textCentred(bold, "GLOBAL", r.getX() + 1, r.getWidth() - 1, (kLcdH - bold.h) / 2, ink); continue; }
        // the track's number (the key's legend; the small face where the bold one is too wide, as the stand-in's),
        // its machine's short name, the trigger bar under it
        const juce::String num(key + 1);
        const auto& nf = LcdCanvas::textWidth(bold, num.toRawUTF8()) <= r.getWidth() - 3 ? bold : spec::kFontSmall4x5;
        cv.textCentred(nf, num.toRawUTF8(), r.getX() + 1, r.getWidth() - 1, 2 + (bold.h - nf.h) / 2, ink);
        cv.textCentred(spec::kFontTiny3x5, m_names[size_t(key)].toRawUTF8(), r.getX() + 1, r.getWidth() - 1, 11, ink);
        if (m_flash[size_t(key)] > 0) cv.fillRect(r.getX() + 3, 17, r.getWidth() - 5, 2, ink);
    }
    cv.dotsV(kLcdW - 1, 0, kLcdH - 1);
    cv.dotsH(0, kLcdW - 1, kLcdH - 1);
    cv.draw(g, 0, 0);
}

void TrackStrip::mouseDown(const juce::MouseEvent& e)
{
    const int key = keyAt(e.getPosition());
    if (key < 0) return;
    if (key == m_selected && key != kGlobal) {   // the selected track's key again: a pad
        m_playing = key;
        if (onPlay) onPlay(key, true);
        return;
    }
    if (onSelect) onSelect(key);
}

void TrackStrip::mouseUp(const juce::MouseEvent&)
{
    if (m_playing < 0) return;
    if (onPlay) onPlay(m_playing, false);
    m_playing = -1;
}

void TrackStrip::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(keyAt(e.getPosition()) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------
// KitBox

void KitBox::setKit(const juce::String& name, const juce::String& bank)
{
    const auto n = name.toUpperCase(), b = bank.toUpperCase();
    if (n == m_name && b == m_bank) return;
    m_name = n;
    m_bank = b;
    if (onContentChanged) onContentChanged();
    repaint();
}

int KitBox::preferredWidth() const
{
    const int w = juce::jmax(LcdCanvas::textWidth(spec::kFontBold8, m_name.toRawUTF8()), LcdCanvas::textWidth(spec::kFontTiny3x5, m_bank.toRawUTF8())) + 8;
    return juce::jlimit(kMinW, kMaxW, w) * kScale;
}

void KitBox::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    LcdCanvas cv(w, kLcdH);
    cv.dottedFrame(0, 0, w, kLcdH);
    const auto clip = [w](const spec::Font& f, juce::String s) {
        while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > w - 6) s = s.dropLastCharacters(1);
        return s;
    };
    cv.text(spec::kFontTiny3x5, clip(spec::kFontTiny3x5, m_bank.isNotEmpty() ? m_bank : juce::String("KIT")).toRawUTF8(), 3, 3);
    cv.text(spec::kFontBold8, clip(spec::kFontBold8, m_name).toRawUTF8(), 3, 10);
    cv.draw(g, 0, 0);
}

// ---------------------------------------------------------------------------
// LcdButton

void LcdButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const bool inv = !down;   // solid block normally, outlined while pressed
    if (inv) cv.fillRect(0, 0, w, h, true);
    else cv.dottedFrame(0, 0, w, h);
    cv.textCentred(spec::kFontBold8, getButtonText().toRawUTF8(), 0, w, (h - spec::kFontBold8.h) / 2, !inv);
    cv.draw(g, 0, 0);
    if (highlighted && !down) { g.setColour(lcd::paper.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------
// ScaleGrip

void ScaleGrip::paint(juce::Graphics& g)
{
    // dots on every other grip pixel, filling the lower-right half, in the corner of the component
    const int x0 = getWidth() - kDots * kScale, y0 = getHeight() - kDots * kScale;
    g.setColour(kPrint);
    for (int y = 0; y < kDots; y += 2)
        for (int x = 0; x < kDots; x += 2)
            if (x + y >= kDots - 1) g.fillRect(x0 + x * kScale, y0 + y * kScale, kScale, kScale);
}

void ScaleGrip::mouseDown(const juce::MouseEvent& e)
{
    m_startScreen = e.getScreenPosition().toFloat();
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>()) {
        m_start = editor->getBounds();
        if (auto* c = editor->getConstrainer()) c->resizeStart();
    }
}

void ScaleGrip::mouseDrag(const juce::MouseEvent& e)
{
    auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
    if (editor == nullptr) return;
    // the drag in the editor's own units (its top-left stays put while it grows or shrinks)
    const auto d = (editor->getLocalPoint(nullptr, e.getScreenPosition().toFloat()) - editor->getLocalPoint(nullptr, m_startScreen)).roundToInt();
    const auto r = m_start.withSize(m_start.getWidth() + d.x, m_start.getHeight() + d.y);
    if (auto* c = editor->getConstrainer()) c->setBoundsForComponent(editor, r, false, false, true, true);
    else editor->setBounds(r);
}

void ScaleGrip::mouseUp(const juce::MouseEvent&)
{
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* c = editor->getConstrainer()) c->resizeEnd();
}

// ---------------------------------------------------------------------------
// OneView

OneView::OneView(MdOneProcessor& p) : m_proc(p), m_kits(p, m_library)
{
    skin::apply(skin::load());
    m_artPath = m_proc.firmwarePath();
    loadLcdArt(m_artPath);   // the OS file's faces before anything measures text

    // the LCD: the header, the track pages, the GLOBAL tab's pages, the strip, the picker over them
    addAndMakeVisible(m_machineBar);
    addChildComponent(m_globalTitle);
    addAndMakeVisible(m_kitBox);
    addAndMakeVisible(m_strip);
    for (auto* c : {(juce::Component*)&m_machineBar, (juce::Component*)&m_globalTitle, (juce::Component*)&m_kitBox})
        c->setOpaque(true);
    m_globalTitle.setMachine({}, "GLOBAL");
    m_machineBar.onContentChanged = [this] { resized(); };
    m_kitBox.onContentChanged = [this] { resized(); };
    m_kitBox.onClick = [this] { showGlobal(); };
    m_strip.onPlay = [this](int track, bool down) { playTrack(track, down); };
    m_strip.onSelect = [this](int key) { if (key == TrackStrip::kGlobal) showGlobal(); else selectTrack(key); };

    m_pages[Syn] = std::make_unique<KnobPage>("SYN");
    m_pages[Effects] = std::make_unique<KnobPage>("AMP/EFX");
    m_pages[Routing] = std::make_unique<KnobPage>("ROUTE");
    m_pages[Lfo] = std::make_unique<KnobPage>("LFO");
    m_pages[Global] = std::make_unique<KnobPage>("GLOBAL");
    m_pages[Mixer] = std::make_unique<MixerPage>();
    for (auto& page : m_pages) addChildComponent(*page);
    addChildComponent(m_kits);
    addChildComponent(m_initKit);
    addChildComponent(m_export);

    // GLOBAL: TEMPO CHAN NOTES ACCNT over INIT KIT and EXPORT (buttons over the blank bottom row)
    auto& global = page(Global);
    auto& tempo = m_proc.tempoParameter();
    global.bind(0, &tempo, cellParam("TEMPO", spec::Display::Numeric, juce::roundToInt((120.0 - kTempoMin) * 127.0 / (kTempoMax - kTempoMin)), 127),
                [](double v) { return juce::String(v, 1); });
    global.cell(0).setDragSteps(120.0, 1.0, 0.1);   // a whole BPM per 1.3 px of drag; with shift a tenth
    global.bind(1, &m_proc.midiChannelParameter(), cellParam("CHAN", spec::Display::Text, 0, 16),
                [](double v) { const int c = juce::roundToInt(v); return c == 0 ? juce::String("OMNI") : juce::String(c); });
    global.bind(2, &m_proc.noteMapParameter(), cellParam("NOTES", spec::Display::Text, 0, 1, kNoteMapShort));
    global.bind(3, &m_proc.accentParameter(), cellParam("ACCNT", spec::Display::Numeric, 0, 127));
    for (int k = 4; k < 8; ++k) global.bind(k, nullptr, spec::kBlankParam);
    m_initKit.onClick = [this] { m_proc.loadDefaultKit(); m_kits.showMessage("INIT KIT LOADED"); };
    m_export.onClick = [this] { openExportChooser(); };
    // MIXER: every track's LEV
    auto& mixer = page(Mixer);
    for (int t = 0; t < 16; ++t)
        mixer.bind(t, &m_proc.parameter(t, params::kLevel), cellParam(juce::String(t + 1).toRawUTF8(), spec::Display::Numeric, 100, 127));
    m_kits.onFolder = [this] { m_library.folder().revealToUser(); };

    m_machineBar.onOpen = [this] { if (m_picker.isOpen()) m_picker.close(); else openMachinePicker(); };
    m_picker.onOpenChanged = [this](bool open) { m_machineBar.setOpen(open); };
    m_picker.onPicked = [this](int) {
        m_proc.handleParameterChanges();   // the SYN parameters take the machine's defaults and names now
        pollMachines();
    };
    addChildComponent(m_picker);   // last: over the pages
    addMouseListener(this, true);  // to close the picker on a press anywhere else

    // the chassis: the OS picker and the status under the LCD
    m_select.onClick = [this] { openOsChooser(); };
    m_select.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2c));
    m_select.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6e6e4));
    m_select.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff151516));
    m_status.setFont(juce::Font(juce::FontOptions(13.0f)));
    m_status.setMinimumHorizontalScale(0.6f);
    m_fwPath.setFont(juce::Font(juce::FontOptions(11.0f)));
    m_fwPath.setMinimumHorizontalScale(0.5f);
    m_fwPath.setColour(juce::Label::textColourId, kDim);
    for (auto* l : {&m_status, &m_fwPath}) { l->setBorderSize({0, 0, 0, 0}); l->setInterceptsMouseClicks(false, false); }
    addAndMakeVisible(m_select);
    addAndMakeVisible(m_status);
    addAndMakeVisible(m_fwPath);
    addAndMakeVisible(m_grip);

    // the base size: the LCD glass in its bezel, then the button row and two status lines
    const int glassW = (kLcdContentW + 2 * kGlassPad) * kScale, glassH = (kLcdContentH + 2 * kGlassPad) * kScale;
    setSize(2 * kMargin + 2 * kBezel + glassW, kMargin + 2 * kBezel + glassH + 12 + kButtonRowH + 6 + 16 + 14 + 12);
    m_machines.fill(-1);
    m_proc.takeTriggeredTracks();   // what played while no editor was open does not flash
    timerCallback();   // the machine table, the names, the status from the processor's state
    bindTrack();
    updateHeader();
    startTimerHz(30);
}

OneView::~OneView()
{
    stopTimer();
    if (m_playingNote >= 0) m_proc.keyboardState().noteOff(1, m_playingNote, 0.0f);
}

void OneView::paint(juce::Graphics& g)
{
    // brushed aluminium: a soft vertical gradient under faint horizontal streaks (the same streaks every time)
    g.setGradientFill(juce::ColourGradient(kAluTop, 0.0f, 0.0f, kAluBottom, 0.0f, float(getHeight()), false));
    g.fillAll();
    juce::Random rnd(7);
    for (int y = 0; y < getHeight(); ++y)
        for (int s = 0; s < 3; ++s) {
            const int x = rnd.nextInt(getWidth()), len = 20 + rnd.nextInt(juce::jmax(1, getWidth() / 2));
            g.setColour((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha(0.02f + 0.05f * rnd.nextFloat()));
            g.fillRect(x - len / 2, y, len, 1);
        }

    // the bezel: glossy black, a faint sheen towards the top right, a bevelled outer edge
    const auto bz = m_bezel.toFloat();
    g.setColour(kBezelFill);
    g.fillRoundedRectangle(bz, 12.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.0f), bz.getX(), bz.getBottom(),
                                           juce::Colours::white.withAlpha(0.12f), bz.getRight(), bz.getY(), false));
    g.fillRoundedRectangle(bz, 12.0f);
    g.setColour(kBezelOuter);
    g.drawRoundedRectangle(bz.reduced(0.5f), 12.0f, 1.0f);
    g.setColour(kBezelInner);
    g.drawRoundedRectangle(bz.reduced(1.5f), 11.0f, 1.0f);

    // the LCD: a dark rim, the backlight darkening over its last few pixels, flat paper where anything is drawn
    g.setColour(juce::Colours::black);
    g.fillRect(m_glass.expanded(1));
    const auto& paper = lcd::paper;
    const auto edge = juce::Colour::fromFloatRGBA(paper.getFloatRed() * 0.8f, paper.getFloatGreen() * 0.8f, paper.getFloatBlue() * 0.8f, 1.0f);
    for (int d = 0; d < kGlassEdge; ++d) {
        g.setColour(edge.interpolatedWith(paper, float(d) / float(kGlassEdge)));
        g.drawRect(m_glass.reduced(d), 1);
    }
    g.setColour(paper);
    g.fillRect(m_glass.reduced(kGlassEdge));

    // the product name, on the LCD at the header's right end (as the MPC skin's), in its bold face
    drawLcdText(g, spec::kFontBold8, kProductName, m_nameplate.getX(), m_nameplate.getY(), kScale, lcd::ink);
}

// While a .syx is dragged over the editor: a dotted ink frame, two LCD px wide, just inside the glass
void OneView::paintOverChildren(juce::Graphics& g)
{
    if (!m_dragOver) return;
    g.setColour(lcd::ink);
    const auto r = m_glass.reduced(kScale);
    for (int x = r.getX(); x < r.getRight(); x += 2 * kScale) {
        g.fillRect(x, r.getY(), kScale, 2 * kScale);
        g.fillRect(x, r.getBottom() - 2 * kScale, kScale, 2 * kScale);
    }
    for (int y = r.getY(); y < r.getBottom(); y += 2 * kScale) {
        g.fillRect(r.getX(), y, 2 * kScale, kScale);
        g.fillRect(r.getRight() - 2 * kScale, y, 2 * kScale, kScale);
    }
}

void OneView::resized()
{
    const int glassW = (kLcdContentW + 2 * kGlassPad) * kScale, glassH = (kLcdContentH + 2 * kGlassPad) * kScale;
    m_bezel = {kMargin, kMargin, glassW + 2 * kBezel, glassH + 2 * kBezel};
    m_glass = m_bezel.reduced(kBezel);
    m_lcd = {m_glass.getX() + kGlassPad * kScale, m_glass.getY() + kGlassPad * kScale};
    const auto at = [this](int x, int y) { return m_lcd + juce::Point<int>(x, y) * kScale; };   // LCD px -> screen px

    // the header: the machine block (or GLOBAL) at the left; the product name at the right, the loaded kit before it
    m_machineBar.setBounds(juce::Rectangle<int>(m_machineBar.preferredWidth(), m_machineBar.preferredHeight()).withPosition(at(0, 0)));
    m_globalTitle.setBounds(juce::Rectangle<int>(m_globalTitle.preferredWidth(), m_globalTitle.preferredHeight()).withPosition(at(0, 0)));
    const int nameW = LcdCanvas::textWidth(spec::kFontBold8, kProductName);
    m_nameplate = juce::Rectangle<int>(nameW * kScale, spec::kFontBold8.h * kScale).withPosition(at(kLcdContentW - nameW, (MachineBar::kLcdH - spec::kFontBold8.h) / 2));
    const int kitW = m_kitBox.preferredWidth();
    m_kitBox.setBounds(juce::Rectangle<int>(kitW, KitBox::kLcdH * kScale).withPosition(at(kLcdContentW - nameW - 8, 0) - juce::Point<int>(kitW, 0)));
    m_strip.setBounds(juce::Rectangle<int>(TrackStrip::kLcdW * kScale, TrackStrip::kLcdH * kScale).withPosition(at(0, kStripY)));

    // the pages: SYN | AMP/EFX over ROUTE | LFO; GLOBAL: MIXER over GLOBAL | KITS
    const int x2 = KnobPage::kLcdW + kColumnGap, y2 = kRow2Y;
    const auto place = [&](juce::Component& c, int x, int y, int w) { c.setBounds(juce::Rectangle<int>(w, KnobPage::kHeight).withPosition(at(x, y))); };
    place(page(Syn), 0, kPagesY, KnobPage::kWidth);
    place(page(Effects), x2, kPagesY, KnobPage::kWidth);
    place(page(Routing), 0, y2, KnobPage::kWidth);
    place(page(Lfo), x2, y2, KnobPage::kWidth);
    place(page(Mixer), 0, kPagesY, MixerPage::kWidth);
    place(page(Global), 0, y2, KnobPage::kWidth);
    place(m_kits, x2, y2, KnobPage::kWidth);
    // INIT KIT and EXPORT over the GLOBAL page's bottom row, two cells each
    const int by = y2 + KnobPage::kGridY + kCell;
    m_initKit.setBounds(juce::Rectangle<int>((2 * kCell - 7) * kScale, 15 * kScale).withPosition(at(4, by + 9)));
    m_export.setBounds(juce::Rectangle<int>((2 * kCell - 7) * kScale, 15 * kScale).withPosition(at(2 * kCell + 4, by + 9)));
    // the picker unrolls from under the header over the pages and the strip
    m_picker.setTargetBounds(juce::Rectangle<int>(kLcdContentW * kScale, (kLcdContentH - kPagesY) * kScale).withPosition(at(0, kPagesY)));

    // under the bezel: the OS picker, then the status lines
    auto r = getLocalBounds().withTrimmedTop(m_bezel.getBottom() + 12).withTrimmedBottom(12).reduced(kMargin, 0);
    auto row = r.removeFromTop(kButtonRowH);
    m_select.setBounds(row.removeFromLeft(150));
    r.removeFromTop(6);
    m_status.setBounds(r.removeFromTop(16));
    m_fwPath.setBounds(r.removeFromTop(14));
    const int gripPx = ScaleGrip::kDots * kScale + 6;   // the grip takes presses out to the window's corner
    m_grip.setBounds(getWidth() - gripPx, getHeight() - gripPx, gripPx, gripPx);
}

void OneView::mouseDown(const juce::MouseEvent& e)
{
    if (!m_picker.isOpen()) return;
    auto* c = e.eventComponent;
    if (c == &m_picker || m_picker.isParentOf(c) || c == &m_machineBar) return;
    m_picker.close();
}

// ---- tracks and tabs ----

void OneView::selectTrack(int t)
{
    t = juce::jlimit(0, 15, t);
    m_picker.close(false);
    const bool changed = t != m_track;
    m_track = t;
    m_global = false;
    if (changed) bindTrack();
    updateHeader();
}

void OneView::showGlobal()
{
    m_picker.close(false);
    m_global = true;
    updateHeader();
}

void OneView::updateHeader()
{
    m_strip.setSelected(m_global ? TrackStrip::kGlobal : m_track);
    for (auto p : {Syn, Effects, Routing, Lfo}) page(p).setVisible(!m_global);
    for (auto p : {Global, Mixer}) page(p).setVisible(m_global);
    for (auto* c : {(juce::Component*)&m_kits, (juce::Component*)&m_initKit, (juce::Component*)&m_export, (juce::Component*)&m_globalTitle})
        c->setVisible(m_global);
    m_machineBar.setVisible(!m_global);
    resized();
    repaint();
}

void OneView::openMachinePicker(bool animate)
{
    if (m_global || !m_picker.canOpen()) return;
    m_picker.open(animate);
}

// Every track page's cells on the selected track's parameters; the SYN page's descriptions follow its machine
void OneView::bindTrack()
{
    const int t = m_track;
    const auto param = [this, t](int slot) { return &m_proc.parameter(t, slot); };
    for (int k = 0; k < 8; ++k) {
        page(Syn).bind(k, param(params::kKit + k), spec::kBlankParam);   // described by updateSyn
        page(Effects).bind(k, param(params::kKit + 8 + k), spec::kEffectsPage[k]);
        page(Routing).bind(k, param(params::kKit + 16 + k), spec::kRoutingPage[k]);
    }
    // LFO: TRACK PARAM SHP1 SHP2 UPDTE (the LFO page's own parameters), SPEED DEPTH SHMIX (ROUTE's LFOS LFOD LFOM)
    auto& lfo = page(Lfo);
    for (int k = 0; k < 5; ++k) lfo.bind(k, param(params::kLfo + k), spec::kLfoPage[k]);
    lfo.bind(0, param(params::kLfo), spec::kLfoPage[0], [this](double v) {   // the destination: its number and machine, "01-BD"
        const int track = juce::jlimit(0, 15, juce::roundToInt(v));
        return juce::String(track + 1).paddedLeft('0', 2) + "-" + shortName(m_proc.machine(track));
    });
    for (int k = 5; k < 8; ++k) lfo.bind(k, param(params::kKit + 21 + k - 5), spec::kLfoPage[k]);
    lfo.bind(1, param(params::kLfo + 1), spec::kLfoPage[1], [this, t](double v) {   // the destination by its label
        return lfoParamName(m_proc.parameter(t, params::kLfo).get(), juce::roundToInt(v));
    });
    m_picker.setParameter(m_proc.parameter(t, params::kMachine));
    m_shownMachine = -1;
    updateSyn();
}

// The SYN page and the machine block after the selected track's machine: its labels (blank knobs dead), the reset
// values (the machine's defaults); a machine this engine does not play is marked SILENT
void OneView::updateSyn()
{
    const int id = m_proc.machine(m_track);
    if (id == m_shownMachine) return;
    m_shownMachine = id;
    const md::engine::MachineInfo* m = nullptr;
    for (const auto& info : m_proc.machines()) if (info.id == id) m = &info;
    spec::Param params[8];
    spec::synParams(m, params);
    auto& syn = page(Syn);
    for (int k = 0; k < 8; ++k) syn.describe(k, params[k]);
    juce::StringArray message;
    if (m != nullptr) {
        const juce::String group(spec::machineGroup(m->name));
        m_machineBar.setMachine(group, juce::String(spec::machineShortName(m->name)));
        syn.setBadge(spec::groupPlayable(group.toStdString()) ? juce::String() : juce::String("SILENT"));
    } else {
        m_machineBar.setMachine({}, m_proc.machines().empty() ? juce::String("NO OS") : juce::String(id));
        syn.setBadge({});
        if (m_proc.firmwarePath().isEmpty())
            message = {"No OS file", "", "Select the Machinedrum's OS file below (OS 1.63, a free download from Elektron)"};
        else if (m_proc.machines().empty())
            message = {"The OS file did not load", "", "See the status below"};
    }
    syn.setMessage(message);
}

juce::String OneView::shortName(int machineId) const
{
    for (const auto& m : m_proc.machines())
        if (m.id == machineId) return juce::String(spec::machineShortName(m.name));
    return juce::String(machineId);   // no OS file: the id
}

juce::String OneView::lfoParamName(int track, int param) const
{
    if (param >= params::kSyn) return params::kKitNames[size_t(juce::jlimit(0, params::kKitParams - 1, param))];
    const int id = m_proc.machine(juce::jlimit(0, 15, track));
    for (const auto& m : m_proc.machines())
        if (m.id == id) return m.params[size_t(param)].empty() ? juce::String("-") : juce::String(m.params[size_t(param)]);
    return "SYN" + juce::String(param + 1);   // no OS file
}

void OneView::pollMachines()
{
    for (int t = 0; t < 16; ++t) {
        const int id = m_proc.machine(t);
        if (id == m_machines[size_t(t)]) continue;
        m_machines[size_t(t)] = id;
        const auto name = shortName(id);
        m_strip.setMachineName(t, name);
        static_cast<MixerPage&>(page(Mixer)).setMachineName(t, name);
    }
    updateSyn();
}

void OneView::machinesChanged()
{
    m_picker.setMachines(m_proc.machines());
    m_machines.fill(-1);
    m_shownMachine = -1;
    pollMachines();
}

void OneView::playTrack(int track, bool down)
{
    auto& keys = m_proc.keyboardState();
    if (m_playingNote >= 0) { keys.noteOff(1, m_playingNote, 0.0f); m_playingNote = -1; }
    if (down) {
        m_playingNote = m_proc.noteForTrack(track);
        keys.noteOn(1, m_playingNote, 100.0f / 127.0f);
    }
}

// ---- files ----

bool OneView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".syx")) return true;
    return false;
}

void OneView::filesDropped(const juce::StringArray& files, int, int)
{
    m_dragOver = false;
    juce::String bank, message;
    for (const auto& f : files) {
        if (!f.endsWithIgnoreCase(".syx")) continue;
        juce::String b;
        if (m_library.import(juce::File(f), message, &b)) bank = b;
    }
    showGlobal();
    m_kits.update();
    if (bank.isNotEmpty()) m_kits.browse(m_library.indexOf(bank));
    m_kits.showMessage(message);
}

bool OneView::exportKit(const juce::File& file)
{
    const auto bytes = md::library::encodeKit(m_proc.parametersToKit());
    return file.replaceWithData(bytes.data(), bytes.size());
}

void OneView::openExportChooser()
{
    auto name = m_proc.kitName().trim();
    if (name.isEmpty()) name = "KIT";
    m_chooser = std::make_unique<juce::FileChooser>("Export the kit as a Machinedrum kit dump (.syx)", m_library.folder().getChildFile(name + ".syx"), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            const auto target = f.hasFileExtension(".syx") ? f : f.withFileExtension(".syx");
            m_kits.showMessage(exportKit(target) ? "EXPORTED " + target.getFileName() : "COULD NOT WRITE " + target.getFileName());
            if (target.getParentDirectory() == m_library.folder()) { m_library.refresh(); m_kits.update(); }
        });
}

void OneView::openOsChooser()
{
    const juce::File current(m_proc.firmwarePath());
    const auto startDir = current.existsAsFile() ? current.getParentDirectory()
                                                 : juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    m_chooser = std::make_unique<juce::FileChooser>("Select Machinedrum OS .syx (Elektron_SPS1-1UW_OS1.63.syx)", startDir, "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); timerCallback(); }
    });
}

// ---- the timer (30 Hz): everything here reads atomics or message-thread state; nothing waits on the audio thread ----

void OneView::timerCallback()
{
    ++m_tick;
    if (!m_proc.engineReady() && m_tick % 60 == 0)
        m_proc.refreshSharedOsPath();   // another plugin may have configured the OS file meanwhile (every 2 s)
    if (m_proc.firmwarePath() != m_artPath) {   // OS file chosen or changed: draw with its LCD artwork from now on
        m_artPath = m_proc.firmwarePath();
        if (loadLcdArt(m_artPath)) { resized(); repaint(); }
    }
    // the machine table: the OS file, whether it loaded, how many machines
    const auto key = m_proc.firmwarePath() + "|" + (m_proc.engineReady() ? "1" : "0") + "|" + juce::String(int(m_proc.machines().size()));
    if (key != m_tableKey) { m_tableKey = key; machinesChanged(); }
    pollMachines();
    m_strip.triggered(m_proc.takeTriggeredTracks());
    m_strip.tick();
    page(Lfo).refreshText();   // TRACK and PARAM follow the destination track's machine
    m_kitBox.setKit(m_proc.kitName(), m_proc.kitBank());
    if (m_tick % 45 == 0) m_library.refresh();   // the kit folder, every 1.5 s (a directory listing when nothing changed)
    m_kits.update();
    if (m_tick % 15 == 1) {   // the status, twice a second
        const bool ready = m_proc.engineReady();
        m_status.setText(m_proc.statusText(), juce::dontSendNotification);
        m_status.setColour(juce::Label::textColourId, ready ? kText : kError);
        m_fwPath.setText("OS: " + (m_proc.firmwarePath().isEmpty() ? juce::String("(none)") : m_proc.firmwarePath()), juce::dontSendNotification);
    }
}

// ---------------------------------------------------------------------------
// OneEditor

OneEditor::OneEditor(MdOneProcessor& p) : ScaledEditor(p, std::make_unique<OneView>(p)) {}

} // namespace md::plugin::one
