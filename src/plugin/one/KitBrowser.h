// Moduledrum: the kit browser, the GLOBAL tab's KITS page (.clew increments 0005 and 0008). The banks of the kit folder
// (KitLibrary: one per .syx file) on the left, the browsed bank's used kits on the right in two columns of eight (the
// wheel scrolls a column at a time when a bank has more), drawn on the LCD like the other pages. A click on a bank
// browses it; a click on a kit loads it into the processor (MdOneProcessor::loadKit: every track's machine, parameters,
// LEV and LFO page, the host told), and the loaded kit is inverted. The footer names the loaded kit and its bank, or
// for a few seconds what a drop did. FOLDER (in the title bar) shows the kit folder in the Finder. In the LCD style of
// Monomodule's library list (shnolk/monomodule src/plugin/one/LibraryUi.cpp, AGPL-3.0); the bank / kit naming after
// sd88me/mpc-vst-machinedrum's GLOBAL page (tools/mdskin/mk_skin.py, AGPL-3.0).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "KitLibrary.h"
#include "Lcd.h"
#include "OneProcessor.h"

namespace md::plugin::one {

class KitsPage : public juce::Component {
public:
    // A page's size (KnobPage's), its title bar, and the lists' geometry, LCD px
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = 11 + 2 * kCell + 1;
    static constexpr int kTitleH = 10, kListY = 12, kRowH = 7, kRows = 8, kBanksW = 40, kKitCols = 2;
    static constexpr int kKitColW = (kLcdW - 1 - kBanksW - 1) / kKitCols;
    static constexpr int kFooterY = kListY + kRows * kRowH;   // the lists' bottom rule; the footer bar under it
    static constexpr int kMessageTicks = 120;                  // a message shows this many editor timer ticks (4 s)

    KitsPage(MdOneProcessor&, KitLibrary&);
    // The editor's timer: the loaded kit and the library's banks; repaints when they changed
    void update();
    void browse(int bank);               // shows that bank's kits (an index into KitLibrary::banks())
    int browsedBank() const { return m_bank; }
    bool loadKit(int bank, int kit);     // as a click on it does
    int loadedKit() const;               // the browsed bank's kit that is loaded, or -1
    void showMessage(const juce::String& text);   // in the footer for a while (what a drop did)
    // Where things are, LCD px (tests drive the page with clicks there)
    juce::Rectangle<int> bankArea(int bank) const;       // -1 width when scrolled out of view
    juce::Rectangle<int> kitArea(int kit) const;
    juce::Rectangle<int> folderButton() const;
    std::function<void()> onKitLoaded;
    std::function<void()> onFolder;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int bankAt(juce::Point<int> lcd) const;
    int kitAt(juce::Point<int> lcd) const;
    int kitCount() const;
    int kitColumns() const { return (kitCount() + kRows - 1) / kRows; }
    void clampScroll();

    MdOneProcessor& m_proc;
    KitLibrary& m_lib;
    int m_revision = -1;                 // the library's scan the page shows
    int m_bank = -1;                     // browsed
    juce::String m_bankName;             // the browsed bank, kept across rescans
    int m_bankScroll = 0, m_kitScroll = 0;   // first bank row / first kit column shown
    int m_hoverBank = -1, m_hoverKit = -1;
    bool m_hoverFolder = false;
    juce::String m_loadedName, m_loadedBank; // the processor's kit
    int m_loadedIndex = -1;              // in m_loadedBank, when loaded from here (two kits may share a name)
    juce::String m_message;
    int m_messageTicks = 0;
};

} // namespace md::plugin::one
