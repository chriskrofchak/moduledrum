// Settings shared by every Moduledrum plugin on this machine. From shnolk/monomodule src/plugin/SharedSettings.h
// (AGPL-3.0), with this project's folder and root tag.
#pragma once
#include <cstdlib>
#include <juce_core/juce_core.h>

namespace md::plugin {

// The settings shared by all Machinedrum plugins on this machine, as attributes of one file:
// ~/Library/Application Support/Moduledrum/Machinedrum/settings.xml on macOS. osPath (the OS file, below), uiScale
// (the editor's size, WindowScale.h), skin / skinInk / skinPaper (the LCD colours, Skin.h). The kit folder (kits/,
// below) is in the same folder.
// Note: on macOS userApplicationDataDirectory is ~/Library, so Application Support is appended
// explicitly to land in the conventional location.
// MD_SETTINGS_DIR overrides the folder (tests, and trying the unconfigured state without losing the real setting).
inline juce::File sharedSettingsFile()
{
    if (const char* env = std::getenv("MD_SETTINGS_DIR"); env && *env) return juce::File(juce::String(env)).getChildFile("settings.xml");
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Moduledrum").getChildFile("Machinedrum").getChildFile("settings.xml");
}

// One attribute of the shared settings file (the other attributes are kept).
inline juce::String loadSharedSetting(const juce::String& name, const juce::String& fallback = {})
{
    if (auto xml = juce::parseXML(sharedSettingsFile()))
        if (xml->hasTagName("MachinedrumSettings")) return xml->getStringAttribute(name, fallback);
    return fallback;
}

inline void saveSharedSetting(const juce::String& name, const juce::String& value)
{
    auto f = sharedSettingsFile();
    f.getParentDirectory().createDirectory();
    auto xml = juce::parseXML(f);
    if (!xml || !xml->hasTagName("MachinedrumSettings")) xml = std::make_unique<juce::XmlElement>("MachinedrumSettings");
    xml->setAttribute(name, value);
    xml->writeTo(f);
}

inline juce::String loadSharedOsPath() { return loadSharedSetting("osPath"); }
inline void saveSharedOsPath(const juce::String& path) { saveSharedSetting("osPath", path); }

// The user's kit folder, beside the settings file (so MD_SETTINGS_DIR moves it too): every .syx in it is a bank of the
// editor's kit browser (KitLibrary.h). Not created here.
inline juce::File sharedKitFolder() { return sharedSettingsFile().getParentDirectory().getChildFile("kits"); }

} // namespace md::plugin
