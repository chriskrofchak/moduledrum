// Moduledrum editor: the Machinedrum's LCD, drawn with the OS file's own fonts and dials. The window is the LCD and
// nothing else, as Monomodule's: no chassis around it. The OS-file picker, the skin and the engine status are in the
// MENU at the header's right end; a footer carries the version and the corner grip. Resizable from 50 to 200 % with its
// aspect ratio locked (ScaledEditor.h); the size is a shared setting. Modelled on shnolk/monomodule
// src/plugin/one/OneEditor.{h,cpp} (AGPL-3.0).
//
// The LCD (.clew increment 0008), laid out as sd88me/mpc-vst-machinedrum's MPC skin: the header (the selected track's
// machine, which opens the machine picker; the loaded kit; the product name), the selected track's four pages in
// Monomodule's 2x2: SYN | AMP/EFX over ROUTE | LFO, and along the bottom the GLOBAL tab and the 16 tracks. Every cell is a knob on its host parameter (OneParams.h), turned as
// in Monomodule: drag (up or right turns it up), the mouse wheel, shift for fine steps, a click on the value to type
// it or pick it from a list, a double-click on the dial for the default. The GLOBAL tab: the 16 LEV faders (MIXER),
// the global parameters (TEMPO, CHAN, NOTES, ACCNT) with INIT KIT and EXPORT, and the kit browser (KitBrowser.h). A
// .syx dropped anywhere on the editor is copied into the kit folder and shown there; a Machinedrum OS file dropped
// becomes the OS file.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>
#include "OneProcessor.h"
#include "Lcd.h"
#include "MachinePicker.h"
#include "KitBrowser.h"
#include "SkinDialog.h"
#include "KitLibrary.h"
#include "ScaledEditor.h"

namespace md::plugin::one {

// Invisible drag surface over one knob cell; the page draws the cell from the slider's value (Monomodule's KnobCell).
// A press anywhere in the cell drags the knob: up or right turns it up, a full turn takes kDragPixels, kFine times as
// many with shift held. The mouse wheel turns it (shift: one fine step per notch). A clean click (no drag) on the value
// fires onValueClick, which opens the inline editor or the value list; a double-click elsewhere resets it.
class KnobCell : public juce::Slider {
public:
    static constexpr double kDragPixels = 160.0;   // Monomodule's drag sensitivity
    static constexpr double kFine = 8.0;
    KnobCell();
    std::function<void()> onValueClick;
    void setValueArea(juce::Rectangle<int> r, juce::MouseCursor valueCursor) { m_valueArea = r; m_valueCursor = valueCursor; }
    // A parameter whose range is not its steps (the tempo): `span` of value per full drag, a drag snapping to `step`
    // and, with shift, to `fineStep` (also the wheel's step). 0: the slider's range and interval.
    void setDragSteps(double span, double step, double fineStep) { m_span = span; m_step = step; m_fineStep = fineStep; }
    void paint(juce::Graphics&) override {}
    void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); notifyPage(); }
    void mouseExit(const juce::MouseEvent& e) override { Slider::mouseExit(e); notifyPage(); }
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void startedDragging() override { notifyPage(); }
    void stoppedDragging() override { notifyPage(); }

private:
    void notifyPage() { if (auto* p = getParentComponent()) p->repaint(); }
    double snapped(double v, bool fine) const;
    juce::Rectangle<int> m_valueArea;
    juce::MouseCursor m_valueCursor{juce::MouseCursor::IBeamCursor};
    double m_span = 0.0, m_step = 0.0, m_fineStep = 0.0;
    double m_dragValue = 0.0;              // the unsnapped value the drag has reached
    juce::Point<float> m_lastDrag;
};

// One hardware page: inverted title bar over the 2x4 knob grid, drawn at LCD resolution. Each cell is a KnobCell
// attached to a host parameter (juce::SliderParameterAttachment: turning it is a gesture on the parameter, a change from
// the host or automation moves it) and drawn from its descriptor (spec::Param). Text and Shape cells and the cells whose
// value is a name (List, Readout) open a list of their values on a click; dials open an inline editor.
class KnobPage : public juce::Component {
public:
    static constexpr int kTitleH = 10, kGridY = kTitleH + 1;               // title bar, a blank row (room for the tie arch), grid
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = kGridY + 2 * kCell + 1;
    static constexpr int kWidth = kLcdW * kScale, kHeight = kLcdH * kScale;
    // A cell's value as text, for a cell that names its values itself (LFO PARAM, CHAN) or a parameter that is not raw
    // steps (TEMPO)
    using TextFn = std::function<juce::String(double value)>;

    explicit KnobPage(const char* title, int numCells = 8);
    ~KnobPage() override;
    // Binds cell k to a host parameter (null: a blank, dead cell) and describes it
    void bind(int k, juce::RangedAudioParameter* param, const spec::Param& d, TextFn text = {});
    // Describes a bound cell anew (the SYN page after a machine change): its label and kind, and the value a
    // double-click resets it to (the parameter's default, which for SYN1-8 is the machine's)
    void describe(int k, const spec::Param& d);
    void setTitle(const juce::String& title) { if (title != m_title) { m_title = title; repaint(); } }
    // Text shown inside the page's frame in place of the knobs (no OS file loaded); empty: the knobs.
    void setMessage(const juce::StringArray& lines);
    // Tag drawn at the right end of the title bar (e.g. "SILENT"); empty clears it.
    void setBadge(const juce::String& badge);
    // Names that depend on other parameters (LFO PARAM follows the destination track's machine): re-read, and
    // repainted when they changed (the editor's timer)
    void refreshText();

    int numCells() const { return int(m_cells.size()); }
    const juce::StringArray& message() const { return m_message; }
    const juce::String& badge() const { return m_badge; }
    KnobCell& cell(int k) { return m_cells[size_t(k)]->slider; }
    const spec::Param& descriptor(int k) const { return m_cells[size_t(k)]->d; }
    juce::RangedAudioParameter* parameter(int k) const { return m_cells[size_t(k)]->param; }
    juce::String cellText(int k) const;   // what cell k shows as its value
    // The inline value editor of a dial cell (a click on its value opens it); endEdit commits or drops what was typed
    void beginEdit(int k);
    void endEdit(bool commit);
    juce::TextEditor& valueEditor() { return m_editor; }
    int editing() const { return m_editing; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;   // any press outside the inline editor commits it

protected:
    // Where cell k is, in LCD px (its 32x32 area from the dotted corner), and its value's click target
    virtual juce::Rectangle<int> cellArea(int k) const;
    virtual juce::Rectangle<int> valueBox(int k) const;
    virtual void paintCells(LcdCanvas& cv) const;
    void paintTitle(LcdCanvas& cv) const;
    int rawOf(int k) const;   // the value as the cell draws it: raw, or 0-127 across a range of other steps
    bool isHot(int k) const;  // hovered, dragged or being edited
    void showValueList(int k);
    void setTyped(int k, const juce::String& text);

    struct Cell {
        KnobCell slider;
        spec::Param d = spec::kBlankParam;
        TextFn text;
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
        juce::String shown;   // the text last drawn (refreshText)
    };
    std::vector<std::unique_ptr<Cell>> m_cells;
    juce::String m_title, m_badge;
    juce::StringArray m_message;
    juce::TextEditor m_editor;
    int m_editing = -1;
    juce::Component::SafePointer<juce::Component> m_listenedTop;   // window we listen on while editing
};

// MIXER (the GLOBAL tab's top row, both pages wide): the 16 tracks' LEV as faders in the LCD's level-bar style
// (Monomodule's LEV column), with the track's number, its machine and the raw value. Each fader is a KnobCell.
class MixerPage : public KnobPage {
public:
    static constexpr int kLcdW = 2 * KnobPage::kLcdW + 28, kWidth = kLcdW * kScale;   // the width of two pages and their gap
    MixerPage();
    void setMachineName(int track, const juce::String& shortName);

protected:
    juce::Rectangle<int> cellArea(int k) const override;
    juce::Rectangle<int> valueBox(int k) const override;
    void paintCells(LcdCanvas& cv) const override;

private:
    std::array<juce::String, 16> m_names;
};

// The machine block: an inverted block with the machine's group and name, then the picker arrow; the block hugs its
// contents (Monomodule's MachineBar, the group set in type: the MD OS has no group logos). A click opens the machine
// picker (onOpen); the arrow points up while it is open. With no group it is a plain title block (GLOBAL, NO OS).
class MachineBar : public juce::Component {
public:
    static constexpr int kLcdH = 20, kTextX = 4, kNameGap = 5, kArrowGap = 4, kArrowW = 5, kPadR = 4;   // LCD px
    // group empty: `name` alone and no arrow (nothing to pick from)
    void setMachine(const juce::String& group, const juce::String& name);
    void setOpen(bool open) { if (m_open != open) { m_open = open; repaint(); } }
    std::function<void()> onOpen;
    std::function<void()> onContentChanged;   // the width follows the name; the parent must re-layout
    int preferredWidth() const { return widthLcd() * kScale; }
    int preferredHeight() const { return kLcdH * kScale; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    int widthLcd() const;
    juce::String m_group, m_name;
    bool m_open = false;
};

// The GLOBAL tab and the 16 tracks along the bottom of the LCD, where the MPC skin has its tab bar (GLOBAL first): each
// track's key shows its number and its machine's short name, the selected key is inverted, and a track triggered by
// MIDI (or the editor) flashes a bar under its name. A click selects; a press on the selected track's key plays it
// (onPlay, while held), as a pad would.
class TrackStrip : public juce::Component {
public:
    static constexpr int kGlobalW = 45, kCellW = 15, kLcdH = 20;   // LCD px
    static constexpr int kLcdW = kGlobalW + 16 * kCellW + 1;
    static constexpr int kGlobal = 16;    // the GLOBAL tab, as a 17th key (the first from the left)
    static constexpr int kFlashTicks = 4; // how long a trigger shows, in the editor's timer ticks (30 Hz)
    TrackStrip() { setOpaque(true); }
    std::function<void(int)> onSelect;    // a track 0-15, or kGlobal
    std::function<void(int track, bool down)> onPlay;
    void setSelected(int key) { if (key != m_selected) { m_selected = key; repaint(); } }
    int selected() const { return m_selected; }
    void setMachineName(int track, const juce::String& shortName);
    const juce::String& machineName(int track) const { return m_names[size_t(track)]; }
    void triggered(uint32_t tracks);      // bit t: track t played (MdOneProcessor::takeTriggeredTracks)
    void tick();                          // the editor's timer: the flashes fade
    uint32_t flashing() const;            // bit t: track t's bar is lit
    int keyAt(juce::Point<int> local) const;
    juce::Rectangle<int> keyArea(int key) const;   // LCD px
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    int m_selected = 0;
    int m_playing = -1;                   // the track held down as a pad
    std::array<juce::String, 16> m_names;
    std::array<int, 16> m_flash{};
};

// The loaded kit, in the header left of the product name: its bank (the file it came from) over its name, in a dotted
// frame. A click shows the GLOBAL tab, where the kit browser is.
class KitBox : public juce::Component {
public:
    static constexpr int kLcdH = MachineBar::kLcdH, kMinW = 40, kMaxW = 120;   // LCD px
    KitBox() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void setKit(const juce::String& name, const juce::String& bank);
    std::function<void()> onClick;
    std::function<void()> onContentChanged;   // the width follows the names
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    juce::String m_name, m_bank;
};

// A push button drawn on the LCD: an inverted block with its text in the bold face, outlined while pressed (Monomodule's
// LcdButton)
class LcdButton : public juce::Button {
public:
    explicit LcdButton(const juce::String& text) : juce::Button(text) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// The corner grip, a dotted triangle: dragging it resizes the plugin editor that shows this view scaled
// (scale::ScaledEditor) through the editor's constrainer, so the aspect ratio stays locked. It works in every host and
// format, including AUv2, where hosts do not resize plugin windows themselves. From Monomodule's ScaleGrip.
class ScaleGrip : public juce::Component {
public:
    static constexpr int kDots = 5;   // the triangle, in grip dots of kScale screen px
    ScaleGrip() { setMouseCursor(juce::MouseCursor::BottomRightCornerResizeCursor); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> m_start;          // the editor's bounds when the drag began
    juce::Point<float> m_startScreen;      // the mouse then (screen coordinates: the grip itself moves as the view scales)
};

// The editor's content, laid out at its base size; OneEditor shows it scaled.
class OneView : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit OneView(MdOneProcessor&);
    ~OneView() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;   // the drop-target frame
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;  // a press outside the picker closes it

    // .syx files dropped on the editor go into the kit folder (KitLibrary::import); the GLOBAL tab then shows them. A
    // dropped file that holds no kit but is a Machinedrum OS is selected as the OS file.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { m_dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { m_dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The pages' keys (page()), the track pages then the GLOBAL tab's
    enum Page { Syn, Effects, Routing, Lfo, Global, Mixer, kNumPages };
    void selectTrack(int t);   // binds every page to track t (0-15) and shows them
    void showGlobal();         // the GLOBAL tab
    int selectedTrack() const { return m_track; }
    bool globalShown() const { return m_global; }
    void openMachinePicker(bool animate = true);
    // dev/snapshot/tests
    void refresh() { timerCallback(); }   // pick up the processor's state without the message loop
    KnobPage& page(Page p) const { return *m_pages[size_t(p)]; }
    MachinePicker& machinePicker() { return m_picker; }
    TrackStrip& trackStrip() { return m_strip; }
    MachineBar& machineBar() { return m_machineBar; }
    KitBox& kitBox() { return m_kitBox; }
    KitsPage& kitsPage() { return m_kits; }
    KitLibrary& kitLibrary() { return m_library; }
    bool exportKit(const juce::File& file);   // the loaded kit with the parameters' values, as a kit dump (EXPORT)
    void showMenu();                          // the MENU button's menu
    // Shows the engine status (two lines under the strip, from MdOneProcessor::statusText) or hides it; the view grows
    // or shrinks by kStatusH (MENU > Show Engine Status, like Monomodule's)
    void setStatusShown(bool shown);
    bool statusShown() const { return m_showStatus; }
    void applySkin(const skin::Skin& s);      // makes s the skin of every Moduledrum window (shared settings) and repaints
    void showSkinDialog() { m_skinDialog.open(); }   // MENU > Skin > Custom... (and UI snapshots)

    // LCD geometry (LCD px), as the MPC skin's: the header (the machine block, the kit, the product name, MENU), the 2x2
    // pages with a wide gap between the columns, then the strip, then the footer (the version, the grip); the margin
    // around it all
    static constexpr int kGlassPad = 5, kHeaderGap = 3, kColumnGap = 28, kRowGap = 7, kStripGap = 6;
    static constexpr int kMenuW = 32, kMenuH = 10, kMenuGap = 8;   // the MENU block, and the gap to the product name
    static constexpr int kFooterGap = 4, kFooterH = 8;
    static constexpr int kLcdContentW = 2 * KnobPage::kLcdW + kColumnGap;
    static constexpr int kPagesY = MachineBar::kLcdH + kHeaderGap, kRow2Y = kPagesY + KnobPage::kLcdH + kRowGap;
    static constexpr int kStripY = kRow2Y + KnobPage::kLcdH + kStripGap;
    static constexpr int kLcdContentH = kStripY + TrackStrip::kLcdH;
    static constexpr int kStatusH = 36;   // the engine status lines when shown (screen px at the base size)

private:
    void timerCallback() override;
    void openOsChooser();
    void openExportChooser();
    void machinesChanged();            // an OS file loaded (or none): the picker, the SYN page, the names
    void bindTrack();                  // every track page's cells to m_track's parameters
    void updateSyn();                  // the SYN page and the machine block after the track's machine
    void updateHeader();               // which header and pages show (a track or GLOBAL)
    void pollMachines();               // every track's machine: the strip's names, the SYN page
    void playTrack(int track, bool down);   // a track's key as a pad
    juce::String shortName(int machineId) const;
    juce::String lfoParamName(int track, int param) const;   // an LFO destination's label

    MdOneProcessor& m_proc;
    KitLibrary m_library;
    MachineBar m_machineBar, m_globalTitle;
    KitBox m_kitBox;
    TrackStrip m_strip;
    std::array<std::unique_ptr<KnobPage>, kNumPages> m_pages;
    KitsPage m_kits;
    LcdButton m_initKit{"INIT KIT"}, m_export{"EXPORT"}, m_menu{"MENU"};
    MachinePicker m_picker;
    SkinDialog m_skinDialog;
    juce::Label m_status, m_fwPath;   // the engine status, shown on request (setStatusShown)
    ScaleGrip m_grip;
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::Rectangle<int> m_nameplate, m_footer;   // screen px: the product name in the header, the footer under the strip
    juce::Point<int> m_lcd;                       // the LCD content's origin, screen px
    bool m_showStatus = false;
    int m_track = 0;              // the track the pages show
    bool m_global = false;        // the GLOBAL tab shows
    std::array<int, 16> m_machines{};   // each track's machine as last shown (-1: not yet)
    int m_shownMachine = -1;      // the machine the SYN page describes
    juce::String m_tableKey;      // which machine table the editor shows (the OS file and whether it loaded)
    int m_playingNote = -1;       // the note of a track key held as a pad
    int m_tick = 0;
    juce::String m_artPath;       // the OS path the LCD artwork was last requested for
    bool m_dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OneView)
};

// The plugin's editor (MdOneProcessor::createEditor): OneView, scaled.
class OneEditor : public scale::ScaledEditor {
public:
    explicit OneEditor(MdOneProcessor& p);
    OneView& oneView() const { return static_cast<OneView&>(view()); }
};

// The OneView inside a plugin editor made by MdOneProcessor::createEditor (dev tools), or null
inline OneView* oneViewOf(juce::AudioProcessorEditor* editor)
{
    auto* scaled = dynamic_cast<scale::ScaledEditor*>(editor);
    return scaled != nullptr ? dynamic_cast<OneView*>(&scaled->view()) : nullptr;
}

} // namespace md::plugin::one
