// Moduledrum: the machine picker (.clew increment 0008). It unrolls from under the header over the track strip and the
// pages: one column per machine group of the OS's machine table, in id order (GND TRX EFM E12 P-I INP MID CTR ROM RAM;
// a group of more than 16 machines takes several columns under one header), each machine by its short name in the
// LCD's bold face. The track's machine is inverted. Machines this engine cannot play (spec::groupPlayable: INP, MID,
// CTR, RAM, and ROM, which needs sample data from a real unit) are greyed and cannot be picked; a kit that uses one
// still shows it as the current machine. The footer names the machine under the mouse and its SYN knobs. A click
// picks and closes; the machine block toggles the picker; Escape or a click elsewhere closes it. Drawn at LCD
// resolution like the pages.
// From shnolk/monomodule src/plugin/one/MachinePicker.{h,cpp} (AGPL-3.0): the overlay, its unrolling, the keys; the
// columns by group follow sd88me/mpc-vst-machinedrum tools/mdskin/mk_skin.py (AGPL-3.0).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Lcd.h"
#include "MachineRunner.h"

namespace md::plugin::one {

class MachinePicker : public juce::Component, private juce::Timer {
public:
    static constexpr int kColGap = 1, kHeaderH = 11, kRowH = 10, kRowsPerColumn = 16, kFooterH = 10;   // LCD px
    MachinePicker();
    // The OS's machine table (MdOneProcessor::machines()); empty: nothing to pick (no OS file)
    void setMachines(const std::vector<md::engine::MachineInfo>& table);
    void setParameter(juce::RangedAudioParameter& machineParam);   // the machine parameter of the shown track
    void setTargetBounds(juce::Rectangle<int> fullyOpen);         // in the parent, screen px
    void open(bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return m_wantOpen; }                     // requested state (true while unrolling)
    bool canOpen() const { return !m_entries.empty(); }
    // Sets the machine as a click on it does (a complete gesture on the parameter) and closes; false, and nothing
    // happens, for a greyed or unknown machine
    bool pick(int machineId);
    bool playable(int machineId) const;
    std::function<void(bool)> onOpenChanged;   // the machine block follows (arrow direction)
    std::function<void(int)> onPicked;         // after the parameter changed
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Entry {
        int id;
        juce::String name, shortName, group, syn;   // syn: the SYN labels, for the footer
        bool playable;
        int column;
        juce::Rectangle<int> r;   // LCD px in the fully open picker
    };
    struct Column {
        juce::String group;
        int first, count;         // entries [first, first + count)
        int groupColumns;         // columns of this group from this one on, when it is its group's first (else 0)
        juce::Rectangle<int> r;
    };
    void layout();
    int entryAt(juce::Point<int> local) const;
    int entryOf(int machineId) const;
    void timerCallback() override;
    void applyAnimation();

    std::vector<Entry> m_entries;   // by id
    std::vector<Column> m_columns;
    std::unique_ptr<juce::ParameterAttachment> m_attach;
    int m_current = -1;             // the machine id the parameter holds
    int m_cursor = -1;              // hovered / keyboard-selected entry
    juce::Rectangle<int> m_target;  // bounds when fully open; the layout is always computed for these
    bool m_wantOpen = false;
    float m_anim = 0.0f;            // 0 = rolled up, 1 = fully open
};

} // namespace md::plugin::one
