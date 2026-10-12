#include "MachinePicker.h"
#include <algorithm>
#include <cmath>

namespace md::plugin::one {

namespace {

constexpr int kAnimHz = 60;
constexpr float kAnimSeconds = 0.16f;

// Why a group is greyed, for the footer (the tiny face has capitals and digits, no apostrophe or comma)
juce::String whySilent(const juce::String& group)
{
    if (group == "INP") return "NEEDS THE MACHINEDRUM AUDIO INPUTS";
    if (group == "MID") return "A MIDI MACHINE: NO SOUND OF ITS OWN";
    if (group == "CTR") return "A CONTROL MACHINE: NOT IN THIS ENGINE";
    if (group == "RAM") return "RAM RECORDING: NOT IN THIS ENGINE";
    if (group == "ROM") return "NEEDS ROM SAMPLES FROM A REAL MACHINEDRUM";
    return {};
}

} // namespace

MachinePicker::MachinePicker()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
}

void MachinePicker::setMachines(const std::vector<md::engine::MachineInfo>& table)
{
    // groups in the order of their first machine, each group's machines by id (ROM's ids are in two runs, RAM's between)
    std::vector<juce::String> groups;
    for (const auto& m : table) {
        const juce::String g(spec::machineGroup(m.name));
        if (std::find(groups.begin(), groups.end(), g) == groups.end()) groups.push_back(g);
    }
    m_entries.clear();
    m_columns.clear();
    for (const auto& g : groups) {
        const int first = int(m_entries.size());
        for (const auto& m : table) {
            if (juce::String(spec::machineGroup(m.name)) != g) continue;
            juce::StringArray syn;
            for (const auto& label : m.params) if (!label.empty()) syn.add(label);
            m_entries.push_back({int(m.id), juce::String(m.name), juce::String(spec::machineShortName(m.name)), g, syn.joinIntoString(" "),
                                 spec::groupPlayable(g.toStdString()), 0, {}});
        }
        const int count = int(m_entries.size()) - first;
        const int columns = (count + kRowsPerColumn - 1) / kRowsPerColumn;   // more than 16: several columns, one header
        for (int c = 0; c < columns; ++c)
            m_columns.push_back({g, first + c * kRowsPerColumn, std::min(kRowsPerColumn, count - c * kRowsPerColumn), c == 0 ? columns : 0, {}});
    }
    for (int c = 0; c < int(m_columns.size()); ++c)
        for (int i = 0; i < m_columns[size_t(c)].count; ++i) m_entries[size_t(m_columns[size_t(c)].first + i)].column = c;
    m_cursor = -1;
    if (m_entries.empty()) close(false);
    layout();
    repaint();
}

void MachinePicker::setParameter(juce::RangedAudioParameter& machineParam)
{
    m_attach = std::make_unique<juce::ParameterAttachment>(machineParam, [this](float v) { m_current = juce::roundToInt(v); repaint(); });
    m_attach->sendInitialUpdate();
}

void MachinePicker::setTargetBounds(juce::Rectangle<int> fullyOpen)
{
    m_target = fullyOpen;
    layout();
    applyAnimation();
}

void MachinePicker::layout()
{
    const int w = m_target.getWidth() / kScale, n = int(m_columns.size());
    for (int c = 0; c < n; ++c) {
        auto& col = m_columns[size_t(c)];
        const int x0 = c * (w + kColGap) / n, x1 = (c + 1) * (w + kColGap) / n - kColGap;   // spread to the full width
        col.r = {x0, 0, x1 - x0, kHeaderH + 1 + kRowsPerColumn * kRowH};
        for (int i = 0; i < col.count; ++i) m_entries[size_t(col.first + i)].r = {x0, kHeaderH + 1 + i * kRowH, x1 - x0, kRowH};
    }
}

void MachinePicker::open(bool animate)
{
    if (!canOpen()) return;
    m_cursor = entryOf(m_current);
    const bool was = m_wantOpen;
    m_wantOpen = true;
    setVisible(true);
    toFront(false);
    if (isShowing()) grabKeyboardFocus();
    if (!animate) { m_anim = 1.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kAnimHz);
    if (!was && onOpenChanged) onOpenChanged(true);
    repaint();
}

void MachinePicker::close(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = false;
    m_cursor = -1;
    if (!animate) { m_anim = 0.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kAnimHz);
    if (was && onOpenChanged) onOpenChanged(false);
}

// The picker unrolls from the top edge: its bounds grow down from the target's top, the layout is fixed for the fully
// open size, so the strip and the pages are covered progressively and the columns are clipped.
void MachinePicker::applyAnimation()
{
    const float eased = 1.0f - (1.0f - m_anim) * (1.0f - m_anim) * (1.0f - m_anim);   // ease-out
    const int h = juce::roundToInt(float(m_target.getHeight()) * eased);
    setBounds(m_target.withHeight(juce::jmax(0, h)));
    if (m_anim <= 0.0f && !m_wantOpen) setVisible(false);
}

void MachinePicker::timerCallback()
{
    const float step = 1.0f / (kAnimSeconds * float(kAnimHz));
    m_anim = m_wantOpen ? juce::jmin(1.0f, m_anim + step) : juce::jmax(0.0f, m_anim - step);
    applyAnimation();
    if (m_anim <= 0.0f || m_anim >= 1.0f) stopTimer();
}

int MachinePicker::entryOf(int machineId) const
{
    for (int i = 0; i < int(m_entries.size()); ++i) if (m_entries[size_t(i)].id == machineId) return i;
    return -1;
}

bool MachinePicker::playable(int machineId) const
{
    const int i = entryOf(machineId);
    return i >= 0 && m_entries[size_t(i)].playable;
}

bool MachinePicker::pick(int machineId)
{
    if (!playable(machineId) || !m_attach) return false;
    m_attach->setValueAsCompleteGesture(float(machineId));
    close();
    if (onPicked) onPicked(machineId);
    return true;
}

int MachinePicker::entryAt(juce::Point<int> local) const
{
    const auto lcd = local / kScale;
    for (int i = 0; i < int(m_entries.size()); ++i) if (m_entries[size_t(i)].r.contains(lcd)) return i;
    return -1;
}

// ---------------------------------------------------------------------------

void MachinePicker::paint(juce::Graphics& g)
{
    const int w = m_target.getWidth() / kScale, h = m_target.getHeight() / kScale;
    LcdCanvas cv(w, h);
    const auto& bold = spec::kFontBold8;
    for (int c = 0; c < int(m_columns.size()); ++c) {
        const auto& col = m_columns[size_t(c)];
        if (col.groupColumns > 0) {   // the group's header, across all its columns
            const auto& last = m_columns[size_t(c + col.groupColumns - 1)];
            const int x0 = col.r.getX(), hw = last.r.getRight() - x0;
            cv.fillRect(x0, 0, hw, kHeaderH, true);
            const auto* title = col.group.toRawUTF8();
            const auto& f = LcdCanvas::textWidth(bold, title) <= hw - 2 ? bold : spec::kFontSmall4x5;
            cv.textCentred(f, title, x0, hw, (kHeaderH - f.h) / 2, false);
        }
        if (c > 0) cv.dotsV(col.r.getX() - 1, kHeaderH + 1, col.r.getBottom() - 1);   // between columns
    }
    for (int i = 0; i < int(m_entries.size()); ++i) {
        const auto& e = m_entries[size_t(i)];
        const bool current = e.id == m_current;
        if (current) cv.fillRect(e.r.getX(), e.r.getY(), e.r.getWidth(), e.r.getHeight(), true);
        else if (i == m_cursor && e.playable) cv.dottedFrame(e.r.getX(), e.r.getY(), e.r.getWidth(), e.r.getHeight());
        cv.textCentred(bold, e.shortName.toRawUTF8(), e.r.getX(), e.r.getWidth(), e.r.getY() + (kRowH - bold.h) / 2, !current);
        if (!e.playable) cv.shade(e.r.getX(), e.r.getY(), e.r.getWidth(), e.r.getHeight());   // greyed: a half-tone
    }
    // the footer: the machine under the mouse, or what grey means
    const int fy = h - kFooterH;
    cv.dotsH(0, w - 1, fy - 1);
    juce::String footer = "GREY: NO SOUND IN THIS ENGINE (INPUT / MIDI / CONTROL / RAM / ROM)";
    if (m_cursor >= 0) {
        const auto& e = m_entries[size_t(m_cursor)];
        footer = e.group + "-" + e.shortName + ":  " + (e.playable ? e.syn : whySilent(e.group));
    }
    cv.text(spec::kFontTiny3x5, footer.toUpperCase().toRawUTF8(), 2, fy + (kFooterH - spec::kFontTiny3x5.h) / 2 + 1);
    cv.draw(g, 0, 0);
}

void MachinePicker::mouseMove(const juce::MouseEvent& e)
{
    const int i = entryAt(e.getPosition());
    const bool can = i >= 0 && m_entries[size_t(i)].playable;
    setMouseCursor(can ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i != m_cursor) { m_cursor = i; repaint(); }
}

void MachinePicker::mouseExit(const juce::MouseEvent&)
{
    if (m_cursor >= 0) { m_cursor = -1; repaint(); }
}

void MachinePicker::mouseDown(const juce::MouseEvent& e)
{
    const int i = entryAt(e.getPosition());
    if (i >= 0) pick(m_entries[size_t(i)].id);   // a greyed machine does nothing
}

bool MachinePicker::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (m_entries.empty()) return false;
    if (k == juce::KeyPress::returnKey) {
        const int i = m_cursor >= 0 ? m_cursor : entryOf(m_current);
        if (i >= 0) pick(m_entries[size_t(i)].id);
        return true;
    }
    const bool up = k == juce::KeyPress::upKey, down = k == juce::KeyPress::downKey;
    const bool left = k == juce::KeyPress::leftKey, right = k == juce::KeyPress::rightKey;
    if (!(up || down || left || right)) return false;
    int i = m_cursor >= 0 ? m_cursor : juce::jmax(0, entryOf(m_current));
    int c = m_entries[size_t(i)].column, row = i - m_columns[size_t(c)].first;
    if (up) row = juce::jmax(0, row - 1);
    if (down) row = juce::jmin(m_columns[size_t(c)].count - 1, row + 1);
    if (left) c = juce::jmax(0, c - 1);
    if (right) c = juce::jmin(int(m_columns.size()) - 1, c + 1);
    m_cursor = m_columns[size_t(c)].first + juce::jmin(row, m_columns[size_t(c)].count - 1);
    repaint();
    return true;
}

} // namespace md::plugin::one
