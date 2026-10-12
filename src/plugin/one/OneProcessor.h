// Moduledrum processor: the Machinedrum SPS-1 UW engine (the OS's own voice DSP program and control code, run from the
// user's OS file) as an instrument plugin. Firmware handling follows Monomodule's MnmOneProcessor: the OS file is
// chosen at run time, shared by every Moduledrum plugin, and a file that fails to load is reported while the plugin
// keeps running without an engine.
//
// Audio path (.clew increment 0003): MIDI notes trigger tracks 1-16 with their velocity, through the note map (the
// hardware's white keys from C2 by default, or the chromatic 36-51 / 0-15 map); the host's tempo drives the LFOs and the
// E12 retrigs. The engine renders its 32-frame blocks at 44.1 kHz synchronously inside processBlock, as Monomodule's
// MonoVoice does, and Monomodule's band-limited converter (src/core/dsp/Resampler) takes them to any other host rate.
// Outputs: the dry main mix, one mono bus per track and the reverb and delay sends (stereo). A track routed to its own
// bus leaves the main mix and still feeds the sends, as a track routed to an individual output does on the hardware
// (EngineT::dryMute).
//
// The sound (.clew increment 0004) is the host parameters (OneParams.h: per track the machine, the 24 kit parameters,
// LEV and the LFO page; tempo, MIDI channel, accent and note map), raw integers as in a kit. processBlock reads them at
// the start of every block and passes what changed to the engine; nothing on that path locks or allocates. A machine
// change resets SYN1-8 to the machine's defaults, as on the MD: the engine takes the defaults at once, the SYN
// parameters follow on the message thread (with their new names, and the host told that names changed). The loaded
// kit (md::library::Kit) is the canonical form of the sound: parameters and kit convert both ways (kitToParameters,
// parametersToKit), and the kit's other contents (name, master effects, LFO state, trig and mute groups) are kept, so
// a session exports as a kit dump with md::library::encodeKit. The state is a versioned XML of all of it.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
#include "Firmware.h"
#include "MdEngine.h"
#include "dsp/Resampler.h"
#include "md/library/MdSysex.h"
#include "one/OneParams.h"

namespace md::plugin::one {

class MdOneProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MdOneProcessor();
    ~MdOneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Moduledrum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Firmware management (message thread), same contract as Monomodule's plugins.
    juce::String firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const juce::String& path, bool persist = true);
    void refreshSharedOsPath();   // another plugin may have configured the OS file meanwhile
    void clearFirmware();
    juce::String statusText() const;   // message thread; never waits on (or holds up) the audio thread
    bool engineReady() const { return m_engineReady.load(); }
    // 32-frame engine blocks rendered by processBlock since the engine was (re)loaded
    uint64_t engineBlocks() const { return m_engineBlocks.load(); }
    // The OS's machine table (names, SYN labels, defaults) of the loaded engine, else null. Read-only, for the editor;
    // message thread, valid until the next OS load.
    const md::engine::MachineRunner* machineTable() const { return m_engine ? &m_engine->os() : nullptr; }
    // The same table as a copy (message thread; empty until an OS file has loaded): ids, names, SYN1-8 labels and defaults
    const std::vector<md::engine::MachineInfo>& machines() const { return m_machineTable; }

    static constexpr double kEngineRate = 44100.0;   // the Machinedrum's own rate
    static constexpr int kWarmUpBlocks = 16;          // silent blocks rendered after boot, before the engine goes live
    static constexpr int kTracks = 16;
    static constexpr int kParams = md::engine::HostModel<>::kParams;   // SYN1-8, AMD..SRR, DIST, VOL, PAN, DEL, REV, LFOS, LFOD, LFOM
    static constexpr int kStateSchema = 2;

    // Output buses: 0 = main (stereo, the dry mix), 1-16 = tracks 1-16 (mono; a stereo layout gets the track on both
    // sides), 17 = reverb send, 18 = delay send (stereo). Only the main bus is on by default.
    static constexpr int kTrackBus = 1, kReverbBus = kTrackBus + kTracks, kDelayBus = kReverbBus + 1, kNumBuses = kDelayBus + 1;

    // ---- Host parameters (OneParams.h) ----
    // A track's parameter by slot (params::kMachine, params::kKit + HostModel index, params::kLevel, params::kLfo + 0-4):
    // what an editor attaches its knobs to. All are juce::AudioParameterInt with the raw value as their int.
    juce::AudioParameterInt& parameter(int track, int slot) const { return *m_param[size_t(track)][size_t(slot)]; }
    juce::AudioParameterFloat& tempoParameter() const { return *m_tempoParam; }
    juce::AudioParameterInt& midiChannelParameter() const { return *m_midiChannelParam; }
    juce::AudioParameterInt& accentParameter() const { return *m_accentParam; }
    juce::AudioParameterChoice& noteMapParameter() const { return *m_noteMapParam; }

    // The sound through the parameters, for code (the host and an editor use the parameters themselves). Setters notify
    // the host; values are raw as in a kit, clamped to each parameter's range; parameters in HostModel order.
    // setMachine: SYN1-8 take the new machine's defaults (from the OS's table, once an OS file is loaded); on the message
    // thread the SYN parameters show them when it returns, from any other thread at the next timer tick (the engine
    // has them at its next block either way). As on the MD, a machine takes effect at the track's next trigger.
    void setMachine(int track, int machineId);
    int machine(int track) const { return parameter(track, params::kMachine).get(); }
    void setTrackParam(int track, int param, int value) { setRaw(track, params::kKit + param, value); }
    int trackParam(int track, int param) const { return parameter(track, params::kKit + param).get(); }
    void setTrackLevel(int track, int level) { setRaw(track, params::kLevel, level); }   // the kit's LEV
    int trackLevel(int track) const { return parameter(track, params::kLevel).get(); }
    // The LFO page: setting 0-4 = destination track, destination parameter, shape 1, shape 2, update type
    void setTrackLfo(int track, int setting, int value) { setRaw(track, params::kLfo + setting, value); }
    int trackLfo(int track, int setting) const { return parameter(track, params::kLfo + setting).get(); }
    // Track output: false = the main mix (default); true = its own bus, out of the main mix (the sends still get it).
    // While the host has that bus disabled the track stays in the main mix. Not a parameter (a routing setting, saved
    // with the state); any thread.
    void setTrackOutput(int track, bool ownBus) { m_ownBus[size_t(track)].store(ownBus); }
    bool trackOutput(int track) const { return m_ownBus[size_t(track)].load(); }

    // MIDI: the channel the note map listens on, 1-16, or 0 = every channel (omni, the default)
    void setMidiChannel(int channel);
    int midiChannel() const { return m_midiChannelParam->get(); }
    // Tempo for the LFOs and E12 retrigs when the host gives none (BPM, 30-300)
    void setTempo(double bpm);
    double tempo() const { return double(m_tempoParam->get()); }
    double currentBpm() const { return m_currentBpm.load(); }   // what the engine runs at: the host's tempo or tempo()
    // Accent amount 0-127 (HostModel::setAccentAmount): above 0, notes at velocity 127 play as accented trigs
    // (the trig's velocity becomes 128 + 2 x accent, as the OS computes it for the sequencer's accent)
    void setAccent(int amount);
    int accent() const { return m_accentParam->get(); }
    using NoteMap = params::NoteMap;
    void setNoteMap(NoteMap map);
    NoteMap noteMap() const { return NoteMap(m_noteMapParam->getIndex()); }
    // Track 0-15 for a MIDI note under the current note map, -1 for a note it does not use; noteForTrack for pads
    int trackForNote(int note) const { return params::trackForNote(note, noteMap()); }
    int noteForTrack(int track) const { return params::noteForTrack(track, noteMap()); }
    // Notes from the editor (an on-screen keyboard or pads): played on any MIDI channel setting, through the note map
    juce::MidiKeyboardState& keyboardState() { return m_keyboard; }
    // Tracks triggered since the last call (bit t = track t+1), for trigger LEDs in the editor (any thread)
    uint32_t takeTriggeredTracks() { return m_triggered.exchange(0, std::memory_order_relaxed); }

    // ---- Kits ----
    // Loads a kit as the MD does: every track's machine, 24 parameters (SYN from the kit, not the machine's defaults),
    // LEV and LFO page go to the parameters (the host is notified), and the kit's other contents are kept for export.
    // `bank` names where it came from (shown with the kit's name; saved with the state). Message thread.
    void loadKit(const md::library::Kit& kit, const juce::String& bank = {});
    void loadDefaultKit();   // the default kit a fresh instance plays (TRX and EFM machines, see OneProcessor.cpp)
    // The kit's machines, parameters, levels and LFO pages -> the host parameters (machine before SYN on each track)
    void kitToParameters(const md::library::Kit& kit);
    // The loaded kit with the parameters' current values: what md::library::encodeKit exports. Values the parameters
    // cannot hold (an LFO shape over 5, say) are kept from the kit while the parameter shows their clamped value.
    md::library::Kit parametersToKit() const;
    juce::String kitName() const;   // the loaded kit's name ("DEFAULT" for the default kit)
    juce::String kitBank() const;   // where it came from, "" if not given

    // Machine changes made on another thread (host automation) push the machine's defaults into the SYN parameters and
    // rename them from a 30 Hz timer; this does it now. Message thread. Tests and code that need the parameters
    // current right after such a change call it.
    void handleParameterChanges();
    // A SYN parameter's name: "T<n> " + the machine's label for that knob ("-" for an unused one, SYN<k> without an OS)
    juce::String synName(int track, int k) const;
    // The OS's name of a machine, as its table has it ("TRXBD"), or its id as text when no OS file is loaded or the id
    // is unused. Any thread but the audio thread.
    juce::String machineName(int machineId) const;
    // The default of SYN k (0-7) for a machine, -1 if unknown (no OS file, or an unused id); any thread, lock-free
    int synDefault(int machineId, int k) const;

    // Host samples the rate conversion delays the output by (0 at 44.1 kHz); reported as the plugin's latency
    int conversionLatency() const { return m_outLag; }

private:
    using Engine = md::engine::Engine;
    class MachineParam;
    class SynParam;
    static BusesProperties makeBuses();
    void createParameters();
    void loadEngine();
    void machineTableChanged();
    void prepareClock(double sampleRate, int samplesPerBlock);
    void applyParameters();
    void queueNote(int64_t hostSample, int track, int velocity, bool accent);
    bool renderEngineBlock();
    void processChunk(int offset, int n);
    void writeOutputs(int offset, int n, int nEngine);
    void setRaw(int track, int slot, int value);
    static void setRawValue(juce::RangedAudioParameter& p, float value);
    void machineChanged(int track, int machineId);   // MachineParam::valueChanged, any thread
    void synChanged(int track, int k, int value);    // SynParam::valueChanged, any thread
    void refreshSynNames(uint32_t tracks);
    int machineFromText(const juce::String& text) const;   // a machine's name or id as text -> its id
    void timerCallback() override { handleParameterChanges(); }

    juce::String m_firmwarePath;
    // The engine refers to its firmware: both are replaced together under m_engineLock, the engine destroyed first.
    std::unique_ptr<md::fw::Firmware> m_firmware;
    std::unique_ptr<Engine> m_engine;
    Engine::Output m_out;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};
    std::atomic<uint64_t> m_engineBlocks{0};
    juce::String m_status;
    std::vector<md::engine::MachineInfo> m_machineTable;   // message thread

    // The parameters (owned by juce::AudioProcessor), by track and slot
    std::array<std::array<juce::AudioParameterInt*, params::kTrackParams>, kTracks> m_param{};
    juce::AudioParameterFloat* m_tempoParam = nullptr;
    juce::AudioParameterInt* m_midiChannelParam = nullptr;
    juce::AudioParameterInt* m_accentParam = nullptr;
    juce::AudioParameterChoice* m_noteMapParam = nullptr;

    // The machine and SYN1-8 as the engine is to play them. Written by the parameters' valueChanged (any thread): a
    // machine change stores the machine's SYN defaults first and the machine last, so processBlock (which reads the
    // machine first, acquire) never pairs a new machine with the old machine's SYN values. The SYN parameters catch
    // up on the message thread (handleParameterChanges). Every other parameter is read from its parameter directly.
    struct EngineSound {
        std::atomic<int> machine{0};
        std::array<std::atomic<int>, params::kSyn> syn{};
        std::atomic<uint8_t> synPending{0};   // SYN k (bit k) still to take the machine's default once an OS is loaded
    };
    std::array<EngineSound, kTracks> m_sound;
    std::atomic<uint32_t> m_machineChanged{0};   // tracks whose SYN parameters and names are to follow their machine
    // The OS's SYN1-8 defaults per machine id, 7 bits each, bit 63 = known (lock-free: read by valueChanged and
    // getDefaultValue on any thread); written when an OS file loads
    std::array<std::atomic<uint64_t>, 256> m_synDefaults{};
    // Names for the host (getName, getText; any thread but the audio thread): the OS's machine names and SYN labels,
    // and each SYN parameter's current name. Written on the message thread when an OS loads or a machine changes.
    struct MachineLabels { std::array<char, 8> name{}; std::array<std::array<char, 8>, params::kSyn> syn{}; bool known = false; };
    std::array<MachineLabels, 256> m_labels{};
    std::array<std::array<juce::String, params::kSyn>, kTracks> m_synNames;
    mutable juce::SpinLock m_labelLock;

    // The loaded kit: its non-parameter contents, and the parameters' values when it was loaded (message thread and
    // state calls, never the audio thread)
    md::library::Kit m_kit;
    juce::String m_kitBank;
    mutable juce::CriticalSection m_kitLock;

    // What processBlock last gave the engine (audio thread, under m_engineLock; -1 = not yet)
    struct Applied { int machine = -1; std::array<int, kParams> param{}; int level = -1; std::array<int, params::kLfoSettings> lfo{}; };
    std::array<Applied, kTracks> m_applied;
    long m_appliedTempo = -1;   // BPM x 24, as HostModel::setTempo stores it
    int m_appliedAccent = -1;
    std::array<std::atomic<bool>, kTracks> m_ownBus{};
    std::atomic<double> m_currentBpm{120.0};
    juce::MidiKeyboardState m_keyboard;
    juce::MidiBuffer m_keyboardMidi;   // the editor's notes for this block
    std::atomic<uint32_t> m_triggered{0};

    // Engine output streams at 44.1 kHz: main L/R, reverb L/R, delay L/R, then the 16 tracks' own outputs (non-zero
    // only for the tracks routed to their own bus). Floats in [-1, 1): the engine's 24-bit samples / 2^23.
    static constexpr int kStreamMain = 0, kStreamRev = 2, kStreamDel = 4, kStreamTrack = 6, kStreams = kStreamTrack + kTracks;
    std::array<std::array<float, Engine::kBlock>, kStreams> m_block{};   // the last engine block
    int m_blockAvail = 0;                                                // its frames not yet consumed
    std::array<std::vector<float>, kStreams> m_eng;                      // this chunk's frames, per stream
    uint32_t m_ownMask = 0;                                              // tracks out of the main mix, on their own bus

    // The clock, as Monomodule's (see Resampler.h). The engine runs at 44.1 kHz whatever the host's rate: host sample s
    // is engine time s * 44100 / rate, exactly. A host block is processed in chunks of at most m_maxChunk samples; each
    // renders the engine frames up to the chunk's last sample, and every output is m_outLag samples later, so the
    // output converter only reads frames already rendered. With no audio input there is no input converter (Monomodule's
    // m_inLag is 0 here). Notes wait in m_notes for their engine frame and trigger at the first 32-frame engine block
    // boundary at or after it. At 44.1 kHz nothing is converted and nothing waits: the engine frames are the samples.
    double m_hostRate = kEngineRate;
    bool m_needsResample = false;
    std::atomic<double> m_statusRate{0.0};      // the host rate while converting, else 0 (statusText)
    std::atomic<int> m_statusLag{0};
    int m_maxChunk = 512, m_maxEngine = 512;    // host samples / engine frames per chunk at most
    int m_outLag = 0;                           // host samples
    int64_t m_hostPos = 0, m_enginePos = 0;     // processed since prepareToPlay
    dsp::SincKernel m_toHost;                   // engine -> host rate
    dsp::RateMap m_engineAt;                    // engine position of a host sample
    std::array<dsp::History, kStreams> m_hist;  // each stream's engine-rate history (host rate != 44.1 kHz)
    std::array<int64_t, kStreams> m_lastSound{};   // engine frame of each stream's last non-zero sample (-1: none)
    std::vector<float> m_taps;                  // scratch: one sample's filter taps
    // Where each stream goes this block: up to two host channels (a mono track on a stereo bus takes both)
    struct Sink { int stream; float* ch[2]; };
    std::array<Sink, kStreams> m_sinks{};
    int m_numSinks = 0;
    struct PendingNote { int64_t frame; int track; int velocity; bool accent; };
    std::vector<PendingNote> m_notes;           // by engine frame, oldest first (fixed capacity)
    static constexpr size_t kMaxPendingNotes = 1024;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MdOneProcessor)
};

} // namespace md::plugin::one
