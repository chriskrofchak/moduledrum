// Moduledrum: the host parameter layout (.clew increment 0004). Every knob of the 16 tracks is a host parameter holding
// the raw value the Machinedrum keeps in a kit (an int, 0-127 for the 24 track parameters and LEV, the OS's machine id
// for the machine), named as the hardware names it. The order is stable: append only, never reorder or rename an id,
// or saved projects and automation lose their parameters.
//
// Per track t (0-15), group "T<t+1>", in this order:
//   track<t>_machine                    0-191, the OS's machine id (HostModel::setMachine)
//   track<t>_syn1 .. _syn8              SYN1-8; named after the machine's own knobs (the OS's table), "-" for unused
//   track<t>_amd amf eqf eqg fltf fltw fltq srr dist vol pan del rev lfos lfod lfom
//                                       the other 16 kit parameters, HostModel's raw order 8-23
//   track<t>_level                      the kit's LEV (HostModel::setLevel)
//   track<t>_lfo_track _lfo_param _lfo_shp1 _lfo_shp2 _lfo_type
//                                       the track's LFO page (HostModel::setLfo): destination track 0-15 and parameter
//                                       0-23, shapes 0-5, update 0-2 (FREE, TRIG, HOLD)
// Globals, group "Global": tempo (BPM when the host gives none), midi_channel (0 = omni), accent (the accent amount;
// notes at velocity 127 play accented while it is above 0), note_map (which MIDI notes trigger the tracks).
// The master effects (.clew increment 0009), group "Master FX": master_fx (on/off: the master section's output is the
// main bus, or the dry mix as before), then the kit's 32 master bytes in the kit dump's order, 0-127, in four
// sub-groups: REV (gate box reverb) fx_rev_dvol .. fx_rev_lev, ECHO (rhythm echo) fx_echo_time .. fx_echo_lev, EQ (master
// EQ) fx_eq_lf .. fx_eq_gain, DYN (dynamix) fx_dyn_atck .. fx_dyn_mix. The editor's pages run ECHO, REV, EQ, DYN, as the
// hardware's menu does; the parameters keep the storage order (md::library::MasterFx::bytes()).
//
// The track and global ids are the keys of sd88me/mpc-vst-machinedrum's vst/params.json (its gen_params.py), so kit and
// preset tooling can be shared; that port's VST index order is not kept (its file is grouped by when each key was added).
// It has no master effects: their ids are ours.
#pragma once
#include <juce_core/juce_core.h>
#include <array>

namespace md::plugin::one::params {

constexpr int kTracks = 16;
constexpr int kKitParams = 24;   // HostModel's raw order: SYN1-8, AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST VOL PAN DEL REV LFOS LFOD LFOM
constexpr int kSyn = 8;
constexpr int kLfoSettings = 5;

// A track's parameters in order: slot 0 the machine, 1-24 the kit parameters (slot = 1 + HostModel index), 25 LEV,
// 26-30 the LFO page
enum Slot : int {
    kMachine = 0,
    kKit = 1,                       // + HostModel parameter index 0-23
    kLevel = kKit + kKitParams,
    kLfo = kLevel + 1,              // + 0-4: track, param, shape 1, shape 2, update
    kSlots = kLfo + kLfoSettings
};
constexpr int kTrackParams = kSlots;
constexpr int kGlobals = 4;
constexpr int kMasterBlocks = 4, kMasterBlockParams = 8;
constexpr int kMasterParams = kMasterBlocks * kMasterBlockParams;   // = md::engine::master::kParams
constexpr int kMasterGroupParams = 1 + kMasterParams;               // master_fx, then the 32 bytes
constexpr int kTotal = kTracks * kTrackParams + kGlobals + kMasterGroupParams;

constexpr int kMaxMachine = 191;
constexpr std::array<const char*, kKitParams> kKitKeys = {
    "syn1", "syn2", "syn3", "syn4", "syn5", "syn6", "syn7", "syn8",
    "amd", "amf", "eqf", "eqg", "fltf", "fltw", "fltq", "srr",
    "dist", "vol", "pan", "del", "rev", "lfos", "lfod", "lfom"};
// The hardware's labels (EFFECTS and ROUTING pages); SYN1-8 until a machine names them
constexpr std::array<const char*, kKitParams> kKitNames = {
    "SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
    "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
    "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
constexpr std::array<const char*, kLfoSettings> kLfoKeys = {"lfo_track", "lfo_param", "lfo_shp1", "lfo_shp2", "lfo_type"};
constexpr std::array<const char*, kLfoSettings> kLfoNames = {"LFO TRACK", "LFO PARAM", "LFO SHP1", "LFO SHP2", "LFO UPDTE"};
constexpr std::array<int, kLfoSettings> kLfoMax = {kTracks - 1, kKitParams - 1, 5, 5, 2};
constexpr std::array<const char*, 3> kLfoTypeNames = {"FREE", "TRIG", "HOLD"};

// Highest raw value of a track slot
constexpr int maxOf(int slot) { return slot == kMachine ? kMaxMachine : slot >= kLfo ? kLfoMax[size_t(slot - kLfo)] : 127; }

inline juce::String trackId(int track, int slot)
{
    const juce::String t = "track" + juce::String(track) + "_";
    if (slot == kMachine) return t + "machine";
    if (slot < kLevel) return t + kKitKeys[size_t(slot - kKit)];
    if (slot == kLevel) return t + "level";
    return t + kLfoKeys[size_t(slot - kLfo)];
}
inline juce::String trackName(int track, int slot)   // the fixed name; SYN1-8 follow the machine instead
{
    const juce::String t = "T" + juce::String(track + 1) + " ";
    if (slot == kMachine) return t + "MACHINE";
    if (slot < kLevel) return t + kKitNames[size_t(slot - kKit)];
    if (slot == kLevel) return t + "LEV";
    return t + kLfoNames[size_t(slot - kLfo)];
}
inline juce::String groupId(int track) { return "T" + juce::String(track + 1); }

constexpr const char* kTempoId = "tempo";
constexpr const char* kMidiChannelId = "midi_channel";
constexpr const char* kAccentId = "accent";
constexpr const char* kNoteMapId = "note_map";

// The master effects: block b (0 reverb, 1 echo, 2 EQ, 3 dynamix: the kit dump's order), parameter i = 8b + k
constexpr const char* kMasterFxId = "master_fx";
constexpr const char* kMasterGroupId = "MasterFX";
enum MasterBlock : int { kRev = 0, kEcho = 1, kEq = 2, kDyn = 3 };
constexpr std::array<const char*, kMasterBlocks> kMasterBlockKeys = {"rev", "echo", "eq", "dyn"};
constexpr std::array<const char*, kMasterBlocks> kMasterBlockNames = {"REV", "ECHO", "EQ", "DYN"};   // the hardware's menu tabs
// The hardware's labels (MD manual p. 29-31; md::library::MasterFx)
constexpr std::array<const char*, kMasterParams> kMasterNames = {
    "DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV",       // gate box reverb
    "TIME", "MOD", "MFRQ", "FB", "FILTF", "FILTW", "MONO", "LEV",   // rhythm echo
    "LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN",               // master EQ
    "ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"};    // dynamix
// The editor's (and the hardware's) page order: ECHO, REV, EQ, DYN
constexpr std::array<int, kMasterBlocks> kMasterPageBlocks = {kEcho, kRev, kEq, kDyn};

inline juce::String masterId(int i)   // fx_rev_dvol .. fx_dyn_mix
{
    return juce::String("fx_") + kMasterBlockKeys[size_t(i / kMasterBlockParams)] + "_" + juce::String(kMasterNames[size_t(i)]).toLowerCase();
}
inline juce::String masterName(int i) { return juce::String(kMasterBlockNames[size_t(i / kMasterBlockParams)]) + " " + kMasterNames[size_t(i)]; }
// The four sub-groups of "Master FX"
inline juce::String masterBlockGroupId(int block) { return juce::String("MasterFX") + kMasterBlockNames[size_t(block)]; }

// MIDI note -> track. WhiteKeys is the hardware's default map (MD manual, appendix B: tracks 1-16 on the white keys from
// C2); Chromatic is the MPC port's convention, notes 36-51 and also 0-15 (its drum-pad layout).
enum class NoteMap : int { WhiteKeys = 0, Chromatic = 1 };
constexpr std::array<const char*, 2> kNoteMapNames = {"WHITE KEYS (MD)", "CHROMATIC 36-51 / 0-15"};
constexpr std::array<int, kTracks> kWhiteKeys = {36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62};

// Track 0-15 for a note, -1 for a note the map does not use
constexpr int trackForNote(int note, NoteMap map)
{
    if (map == NoteMap::Chromatic)
        return note >= 36 && note < 36 + kTracks ? note - 36 : note >= 0 && note < kTracks ? note : -1;
    for (int t = 0; t < kTracks; ++t)
        if (kWhiteKeys[size_t(t)] == note) return t;
    return -1;
}
// The note that plays track 0-15 (Chromatic: the 36-51 one)
constexpr int noteForTrack(int track, NoteMap map) { return map == NoteMap::Chromatic ? 36 + track : kWhiteKeys[size_t(track)]; }

} // namespace md::plugin::one::params
