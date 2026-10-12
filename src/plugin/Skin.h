// Skins: the two colours the whole LCD is drawn in (its ink and paper), shared by every Moduledrum plugin through the
// shared settings file, like the OS file selection. From shnolk/monomodule src/plugin/Skin.h (AGPL-3.0), with the
// Machinedrum's own LCD as the default (the colours sd88me/mpc-vst-machinedrum tools/mdskin/mk_skin.py sampled from a
// photo of the real LCD: maroon pixels on the red backlight).
//   MACHINEDRUM     5E0C0C on FF4836 (the default)
//   INVERTED        FF4836 on 5E0C0C
//   BLACK ON WHITE  Monomodule's default
//   CUSTOM          any two colours, entered as hex
// Every drawing routine reads lcd::ink / lcd::paper (Lcd.h) at paint time, so applying a skin is a matter of setting
// the two colours and repainting; derived shades are alphas or mixes of the two. Choosing a skin in the UI is a later
// increment; load() / save() keep the setting's format ready for it.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "SharedSettings.h"

namespace md::plugin::skin {

enum class Preset : int { Machinedrum = 0, Inverted, BlackOnWhite, Custom };
constexpr int kNumPresets = 4;
constexpr const char* kPresetNames[kNumPresets] = {"MACHINEDRUM", "INVERTED", "BLACK ON WHITE", "CUSTOM"};
constexpr uint32_t kMdInk = 0xff5e0c0c, kMdPaper = 0xffff4836;

struct Skin {
    Preset preset = Preset::Machinedrum;
    juce::Colour ink{kMdInk}, paper{kMdPaper};   // the colours in use (for a preset: its own)
    bool operator==(const Skin& o) const { return preset == o.preset && ink == o.ink && paper == o.paper; }
    bool operator!=(const Skin& o) const { return !(*this == o); }
};

inline Skin presetSkin(Preset p, juce::Colour customInk = juce::Colour(kMdInk), juce::Colour customPaper = juce::Colour(kMdPaper))
{
    switch (p) {
    case Preset::Inverted:     return {p, juce::Colour(kMdPaper), juce::Colour(kMdInk)};
    case Preset::BlackOnWhite: return {p, juce::Colour(0xff000000), juce::Colour(0xffffffff)};
    case Preset::Custom:       return {p, customInk.withAlpha(1.0f), customPaper.withAlpha(1.0f)};
    case Preset::Machinedrum:  break;
    }
    return {Preset::Machinedrum, juce::Colour(kMdInk), juce::Colour(kMdPaper)};
}

// The colours in use. Lcd.h's lcd::ink / lcd::paper are references to these.
inline juce::Colour& inkColour() { static juce::Colour c{kMdInk}; return c; }
inline juce::Colour& paperColour() { static juce::Colour c{kMdPaper}; return c; }
inline Skin& current() { static Skin s; return s; }

// "RRGGBB" or "#RRGGBB" (also "RGB"); false when the text is not a colour.
inline bool parseHex(juce::String text, juce::Colour& out)
{
    text = text.trim().trimCharactersAtStart("#").toUpperCase();
    if (text.length() == 3) text = juce::String::charToString(text[0]) + text[0] + text[1] + text[1] + text[2] + text[2];
    if (text.length() != 6 || !text.containsOnly("0123456789ABCDEF")) return false;
    out = juce::Colour(0xff000000u | uint32_t(text.getHexValue32()));
    return true;
}
inline juce::String hexOf(juce::Colour c) { return c.toDisplayString(false).toUpperCase(); }

// Makes `s` the skin in use (message thread; the caller repaints).
inline void apply(const Skin& s) { current() = s; inkColour() = s.ink; paperColour() = s.paper; }

// The skin saved in the shared settings ("skin" = preset index, "skinInk"/"skinPaper" = the custom colours).
inline Skin load()
{
    const int p = juce::jlimit(0, kNumPresets - 1, loadSharedSetting("skin", "0").getIntValue());
    juce::Colour ink(kMdInk), paper(kMdPaper);
    parseHex(loadSharedSetting("skinInk"), ink);
    parseHex(loadSharedSetting("skinPaper"), paper);
    return presetSkin(Preset(p), ink, paper);
}
inline void save(const Skin& s)
{
    saveSharedSetting("skin", juce::String(int(s.preset)));
    if (s.preset == Preset::Custom) { saveSharedSetting("skinInk", hexOf(s.ink)); saveSharedSetting("skinPaper", hexOf(s.paper)); }
}

} // namespace md::plugin::skin
