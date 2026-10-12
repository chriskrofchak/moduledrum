// Display description of the Machinedrum's knobs: labels, defaults, display type and value lists, as the hardware's
// LCD presents them. Storage is always the raw value of the host parameter; everything here describes the *display*.
// From shnolk/monomodule src/plugin/one/SpecData.{h,cpp} (AGPL-3.0), reduced to what the Machinedrum needs. The SYN
// page is not a table here: its labels and defaults are the OS's own machine table (md::engine::MachineInfo, read
// from the user's OS file), so this file has only the fixed pages. The Monomachine's machine list and list icon
// families are left out; the LFO page's cell kinds (a word, a shape) follow sd88me/mpc-vst-machinedrum
// tools/mdskin/mk_skin.py (AGPL-3.0), which drew them after the MD's own LFO page.
#pragma once
#include <cstdint>
#include <string>
#include "MdRomArt.h"   // Bitmap, Font and the artwork itself (read from the user's OS file, not generated)

namespace md::engine { struct MachineInfo; }

namespace md::uispec {

// Numeric / Bipolar: a dial and the value (raw, or raw - 64). List: a rotary switch over valueCount entries spread
// over 0..127. Readout: one name per raw step. Text: the value as a word where the dial would be (the MD's LFO TRACK,
// PARAM and UPDTE cells), one name per raw step. Shape: an LFO shape drawn where the dial would be (SHP1, SHP2).
enum class Display : uint8_t { Blank, Numeric, Bipolar, List, Readout, Text, Shape };
// Toggle: the OFF/ON icon (MdRomArt.h, drawn in code). Switch: a list without glyphs, drawn as the plain ring with a
// pointer line per position.
enum class Icons : uint8_t { None, Toggle, Switch };

struct Param {
    char label[8];            // as the LCD shows it, at most 5 characters (empty when blank); a copy, so a Param
                              // made from the OS's machine table outlives the engine it came from
    Display display;
    bool tieRight;            // draw the group-tie arch to the knob on the right
    uint8_t defaultRaw;
    uint8_t maxRaw;           // 127 for the knobs; the last value of a Text / Shape cell
    uint8_t valueCount;       // 128 for continuous/readout, N < 128 for a list (Readout/Text/Shape: one name per raw step)
    Icons icons;
    const char* const* values;   // valueCount names for List/Readout/Text/Shape, else null (a page may name them itself)
};

constexpr Param kBlankParam = {"", Display::Blank, false, 0, 127, 128, Icons::None, nullptr};

// The SYN page of a machine: its eight labels and defaults from the OS's machine table, every knob a 0-127 dial (as
// on the hardware); a knob without a label is blank (dead). Null `m` (no OS loaded): all blank.
void synParams(const engine::MachineInfo* m, Param out[8]);

// A machine's name as the LCD splits it: the group ("TRX", "E12", "P-I", ...) and the machine ("BD"), from the OS's
// five-character name ("TRXBD").
std::string machineGroup(const std::string& osName);
std::string machineShortName(const std::string& osName);

// Whether this engine plays a machine group: the synthesis groups do (GND, TRX, EFM, E12, P-I). INP needs an audio
// input, MID and CTR make no sound (MIDI and control machines), RAM records, and ROM needs sample data from a real
// unit's flash, which Moduledrum does not have (.clew increment 0006): those are shown, and greyed.
bool groupPlayable(const std::string& group);

// The Machinedrum's track names (the panel's legends), track 0 = BD; and as the LFO page's TRACK cell names a track
// ("01-BD") where nothing better is known (the editor names it after the track's machine instead)
constexpr int kNumTracks = 16;
extern const char* const kTrackNames[kNumTracks];
extern const char* const kLfoTrackNames[kNumTracks];

// The fixed pages of a track, in the MD's knob order: EFFECTS (AMD AMF EQF EQG FLTF FLTW FLTQ SRR), ROUTING
// (DIST VOL PAN DEL REV LFOS LFOD LFOM) and the LFO page (TRACK PARAM SHP1 SHP2 UPDTE SPEED DEPTH SHMIX; SPEED, DEPTH
// and SHMIX are ROUTING's LFOS, LFOD and LFOM again), defaults as a new kit's. The LFO page's PARAM cell has no
// names of its own: they are the labels of the destination track's machine, which the editor supplies.
extern const Param kEffectsPage[8];
extern const Param kRoutingPage[8];
extern const Param kLfoPage[8];
constexpr int kLfoShapes = 6;
extern const char* const kLfoShapeNames[kLfoShapes];   // the value list's names of the shapes the cell draws

// Raw <-> list index (unequal buckets; identity when n = 128).
constexpr int listIndex(int raw, int n) { return ((2 * raw + 1) * n) >> 8; }
constexpr int listRawMid(int idx, int n) { return (idx * 256 + 128) / (2 * n); }
constexpr int listRawLast(int idx, int n) { int r = 127; while (r > 0 && listIndex(r, n) > idx) --r; return r; }

inline const Bitmap* icon(Icons f, int i)
{
    return f == Icons::Toggle && i >= 0 && i < 2 ? kIconToggle[i] : nullptr;   // Switch: drawn, None: a dial
}

// One name per raw step (Readout, Text, Shape)
inline bool namedPerStep(Display d) { return d == Display::Readout || d == Display::Text || d == Display::Shape; }

// Name the panel prints for a List/Readout/Text/Shape parameter at this raw value; null for other types and for a
// cell whose names the page supplies.
inline const char* valueName(const Param& p, int raw)
{
    if (p.values == nullptr) return nullptr;
    if (p.display == Display::List) return p.values[listIndex(raw, p.valueCount)];
    if (namedPerStep(p.display)) return p.values[raw < 0 ? 0 : raw >= p.valueCount ? p.valueCount - 1 : raw];
    return nullptr;
}

} // namespace md::uispec
