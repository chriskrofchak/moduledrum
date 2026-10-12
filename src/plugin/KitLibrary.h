// The user's kit folder as banks of kits, for the editor's kit browser (.clew increments 0005 and 0008). Every .syx
// file in the kit folder (~/Library/Application Support/Moduledrum/Machinedrum/kits/, SharedSettings.h) is a bank, read
// with md::library::loadSyxFile; its used kits (free slots left out) are what the browser lists. The folder is rescanned
// when its files change (names, sizes, modification times), and a .syx dropped on the editor is copied into it.
// Message thread. The rescan-on-change follows sd88me/mpc-vst-machinedrum's kit catalog (vst/engine.cpp, AGPL-3.0),
// which rescans its kit folder every few seconds; the folder beside the settings file follows Monomodule's
// SharedSettings (AGPL-3.0).
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "SharedSettings.h"
#include "md/library/MdSysex.h"

namespace md::plugin {

class KitLibrary {
public:
    struct Bank {
        juce::File file;
        juce::String name;                     // the file name without its extension (md::library::Bank::name)
        std::vector<md::library::Kit> kits;    // its used kits, in file order (free slots left out)
    };

    // `folder`: the kit folder (by default the shared one); created if it does not exist. Scans it.
    explicit KitLibrary(juce::File folder = sharedKitFolder());
    const juce::File& folder() const { return m_folder; }
    // The banks with at least one used kit, by file name
    const std::vector<Bank>& banks() const { return m_banks; }
    int indexOf(const juce::String& bankName) const;   // -1 when no bank has that name
    // Rescans the folder when its .syx files changed since the last scan (any time with `force`); true when it rescanned.
    // Cheap when nothing changed: a directory listing.
    bool refresh(bool force = false);
    // Copies a kit file into the folder, under its own name or, when another file has that name, "<name> 2.syx" and
    // so on; when the folder already holds the same file (by contents) nothing is copied. False, with the reason in
    // `message`, for a file that holds no kit (an OS file, a pattern dump) or that cannot be read or copied; true with
    // what was done in `message` and the bank it is in `bankName`.
    bool import(const juce::File& file, juce::String& message, juce::String* bankName = nullptr);
    // Files of the last scan that could not be read, one line each
    const juce::StringArray& problems() const { return m_problems; }
    int revision() const { return m_revision; }   // counts the scans (a view repaints when it changes)

private:
    juce::Array<juce::File> syxFiles() const;

    juce::File m_folder;
    std::vector<Bank> m_banks;
    juce::String m_signature;   // names, sizes and modification times of the files last scanned
    juce::StringArray m_problems;
    int m_revision = 0;
};

} // namespace md::plugin
