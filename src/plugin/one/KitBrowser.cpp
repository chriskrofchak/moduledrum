#include "KitBrowser.h"

namespace md::plugin::one {

namespace {

// The text cut to maxW LCD pixels (the faces are proportional), in capitals: the LCD faces have none other
juce::String fit(const spec::Font& f, juce::String s, int maxW)
{
    s = s.toUpperCase();
    while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > maxW) s = s.dropLastCharacters(1);
    return s;
}

} // namespace

KitsPage::KitsPage(MdOneProcessor& p, KitLibrary& lib) : m_proc(p), m_lib(lib)
{
    setOpaque(true);
    update();
}

int KitsPage::kitCount() const
{
    return m_bank >= 0 && m_bank < int(m_lib.banks().size()) ? int(m_lib.banks()[size_t(m_bank)].kits.size()) : 0;
}

void KitsPage::clampScroll()
{
    const int banks = int(m_lib.banks().size());
    m_bankScroll = juce::jlimit(0, juce::jmax(0, banks - kRows), m_bankScroll);
    m_kitScroll = juce::jlimit(0, juce::jmax(0, kitColumns() - kKitCols), m_kitScroll);
}

void KitsPage::update()
{
    bool changed = false;
    const auto name = m_proc.kitName(), bank = m_proc.kitBank();
    if (name != m_loadedName || bank != m_loadedBank) {   // loaded elsewhere (a state, INIT KIT) or from here
        m_loadedName = name;
        m_loadedBank = bank;
        changed = true;
    }
    if (m_lib.revision() != m_revision) {   // rescanned: the browsed bank by name, else the loaded kit's, else the first
        m_revision = m_lib.revision();
        int b = m_bankName.isNotEmpty() ? m_lib.indexOf(m_bankName) : -1;
        if (b < 0) b = m_lib.indexOf(m_loadedBank);
        if (b < 0 && !m_lib.banks().empty()) b = 0;
        m_bank = b;
        m_bankName = b >= 0 ? m_lib.banks()[size_t(b)].name : juce::String();
        clampScroll();
        changed = true;
    }
    if (m_messageTicks > 0 && --m_messageTicks == 0) changed = true;
    if (changed) repaint();
}

void KitsPage::browse(int bank)
{
    if (!juce::isPositiveAndBelow(bank, int(m_lib.banks().size()))) return;
    if (bank != m_bank) m_kitScroll = 0;
    m_bank = bank;
    m_bankName = m_lib.banks()[size_t(bank)].name;
    if (bank < m_bankScroll) m_bankScroll = bank;               // keep it in view
    if (bank >= m_bankScroll + kRows) m_bankScroll = bank - kRows + 1;
    clampScroll();
    repaint();
}

bool KitsPage::loadKit(int bank, int kit)
{
    if (!juce::isPositiveAndBelow(bank, int(m_lib.banks().size()))) return false;
    const auto& b = m_lib.banks()[size_t(bank)];
    if (!juce::isPositiveAndBelow(kit, int(b.kits.size()))) return false;
    m_proc.loadKit(b.kits[size_t(kit)], b.name);
    m_loadedIndex = kit;
    m_loadedName = m_proc.kitName();
    m_loadedBank = m_proc.kitBank();
    if (onKitLoaded) onKitLoaded();
    repaint();
    return true;
}

int KitsPage::loadedKit() const
{
    if (!juce::isPositiveAndBelow(m_bank, int(m_lib.banks().size())) || m_lib.banks()[size_t(m_bank)].name != m_loadedBank) return -1;
    const auto& kits = m_lib.banks()[size_t(m_bank)].kits;
    if (juce::isPositiveAndBelow(m_loadedIndex, int(kits.size())) && juce::String(kits[size_t(m_loadedIndex)].name) == m_loadedName) return m_loadedIndex;
    for (int k = 0; k < int(kits.size()); ++k)   // loaded from a saved state: by name
        if (juce::String(kits[size_t(k)].name) == m_loadedName) return k;
    return -1;
}

void KitsPage::showMessage(const juce::String& text)
{
    m_message = text;
    m_messageTicks = text.isEmpty() ? 0 : kMessageTicks;
    repaint();
}

// ---------------------------------------------------------------------------

juce::Rectangle<int> KitsPage::bankArea(int bank) const
{
    const int row = bank - m_bankScroll;
    if (row < 0 || row >= kRows) return {};
    return {1, kListY + row * kRowH, kBanksW - 1, kRowH};
}

juce::Rectangle<int> KitsPage::kitArea(int kit) const
{
    const int col = kit / kRows - m_kitScroll, row = kit % kRows;
    if (col < 0 || col >= kKitCols) return {};
    return {kBanksW + 1 + col * kKitColW, kListY + row * kRowH, kKitColW, kRowH};
}

juce::Rectangle<int> KitsPage::folderButton() const
{
    const int w = LcdCanvas::textWidth(spec::kFontSmall4x5, "FOLDER") + 4;
    return {kLcdW - w - 1, 1, w, kTitleH - 2};
}

int KitsPage::bankAt(juce::Point<int> lcd) const
{
    for (int b = 0; b < int(m_lib.banks().size()); ++b)
        if (bankArea(b).contains(lcd)) return b;
    return -1;
}

int KitsPage::kitAt(juce::Point<int> lcd) const
{
    for (int k = 0; k < kitCount(); ++k)
        if (kitArea(k).contains(lcd)) return k;
    return -1;
}

void KitsPage::paint(juce::Graphics& g)
{
    LcdCanvas cv(kLcdW, kLcdH);
    const auto& tiny = spec::kFontTiny3x5;
    const int textDy = (kRowH - tiny.h) / 2;   // a row's text, a blank row above and below it

    // the title bar: KITS, which kits are in view when there are more than fit, FOLDER
    cv.fillRect(0, 0, kLcdW, kTitleH, true);
    cv.text(spec::kFontBold8, "KITS", 2, 1, false);
    const auto fb = folderButton();
    cv.fillRect(fb.getX(), fb.getY(), fb.getWidth(), fb.getHeight(), false);
    cv.text(spec::kFontSmall4x5, "FOLDER", fb.getX() + 2, fb.getY() + (fb.getHeight() - spec::kFontSmall4x5.h) / 2, true);
    if (m_hoverFolder) cv.invertRect(fb.getX(), fb.getY(), fb.getWidth(), fb.getHeight());
    if (const int n = kitCount(); n > kRows * kKitCols) {
        const int first = m_kitScroll * kRows + 1, last = juce::jmin(n, first + kRows * kKitCols - 1);
        const juce::String range = juce::String(first) + "-" + juce::String(last) + "/" + juce::String(n);
        cv.text(tiny, range.toRawUTF8(), fb.getX() - 4 - LcdCanvas::textWidth(tiny, range.toRawUTF8()), (kTitleH - tiny.h) / 2, false);
    }

    // the lists' dotted frame, and the divider between banks and kits
    cv.dottedFrame(0, kListY - 1, kLcdW, kFooterY - kListY + 2);
    cv.dotsV(kBanksW, kListY - 1, kFooterY);

    // banks: the browsed one inverted
    const auto& banks = m_lib.banks();
    for (int b = 0; b < int(banks.size()); ++b) {
        const auto r = bankArea(b);
        if (r.isEmpty()) continue;
        const bool on = b == m_bank;
        if (on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        else if (b == m_hoverBank) cv.dottedFrame(r.getX(), r.getY(), r.getWidth(), r.getHeight());
        cv.text(tiny, fit(tiny, banks[size_t(b)].name, r.getWidth() - 3).toRawUTF8(), r.getX() + 2, r.getY() + textDy, !on);
    }

    // the browsed bank's kits: the loaded one inverted
    const int loaded = loadedKit();
    if (juce::isPositiveAndBelow(m_bank, int(banks.size()))) {
        const auto& kits = banks[size_t(m_bank)].kits;
        for (int k = 0; k < int(kits.size()); ++k) {
            const auto r = kitArea(k);
            if (r.isEmpty()) continue;
            const bool on = k == loaded;
            if (on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
            else if (k == m_hoverKit) cv.dottedFrame(r.getX(), r.getY(), r.getWidth(), r.getHeight());
            cv.text(tiny, fit(tiny, kits[size_t(k)].name, r.getWidth() - 4).toRawUTF8(), r.getX() + 2, r.getY() + textDy, !on);
        }
    } else {   // nothing in the folder yet
        const int x0 = kBanksW + 1, w = kLcdW - 1 - x0;
        int y = kListY + 6;
        for (const auto& line : wrapLcdText(tiny, "DROP .SYX KIT FILES ON THE EDITOR TO ADD THEM HERE", w - 6)) {
            cv.textCentred(tiny, line.toRawUTF8(), x0, w, y);
            y += kRowH;
        }
    }

    // the footer: what a drop did, else the loaded kit and its bank
    const juce::String footer = m_messageTicks > 0 ? m_message
                              : m_loadedName + (m_loadedBank.isNotEmpty() ? " / " + m_loadedBank : juce::String());
    cv.fillRect(0, kFooterY + 1, kLcdW, kLcdH - kFooterY - 1, true);   // an inverted bar, as the title bar
    cv.text(tiny, fit(tiny, footer, kLcdW - 5).toRawUTF8(), 2, kFooterY + 2, false);
    cv.draw(g, 0, 0);
}

void KitsPage::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kScale;
    if (folderButton().contains(lcd)) { if (onFolder) onFolder(); return; }
    if (const int b = bankAt(lcd); b >= 0) { browse(b); return; }
    if (const int k = kitAt(lcd); k >= 0) loadKit(m_bank, k);
}

void KitsPage::mouseMove(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kScale;
    const int b = bankAt(lcd), k = kitAt(lcd);
    const bool folder = folderButton().contains(lcd);
    setMouseCursor(b >= 0 || k >= 0 || folder ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (b != m_hoverBank || k != m_hoverKit || folder != m_hoverFolder) {
        m_hoverBank = b;
        m_hoverKit = k;
        m_hoverFolder = folder;
        repaint();
    }
}

void KitsPage::mouseExit(const juce::MouseEvent&)
{
    m_hoverBank = m_hoverKit = -1;
    m_hoverFolder = false;
    repaint();
}

void KitsPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    const float amount = std::abs(d.deltaX) > std::abs(d.deltaY) ? -d.deltaX : d.deltaY;
    const int step = amount > 0 ? -1 : amount < 0 ? 1 : 0;
    if (e.getPosition().x / kScale < kBanksW) m_bankScroll += step;   // a bank row at a time
    else m_kitScroll += step;                                         // a column of kits at a time
    clampScroll();
    repaint();
}

} // namespace md::plugin::one
