#include "OneProcessor.h"
#include "OneEditor.h"
#include "SharedSettings.h"
#include "dsp56kEmu/dspconfig.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace md::plugin::one {

namespace {

// The OS file's version tag as the Machinedrum shows it: "163 " -> "1.63"
juce::String versionText(const std::string& tag)
{
    const auto v = juce::String(tag).trim();
    return v.length() == 3 && v.containsOnly("0123456789") ? v.substring(0, 1) + "." + v.substring(1) : v;
}

// The default kit a fresh instance plays: a TR-style set on the TRX machines, EFM hats and cymbal on tracks 15-16,
// SYN1-8 at each machine's defaults and the other parameters at the values the MPC port's instrument starts from (EQ
// flat, filter open, VOL 100, PAN centre, no sends, LFO depth 0), LEV 100, each LFO on its own track (HostModel's
// default). Its master effects and groups are those of a cleared MD kit.
constexpr std::array<int, 16> kDefaultMachines = {
    16, 17, 18, 27, 19, 20, 21, 22,   // TRX BD SD XT XC CP RS CB CH
    23, 24, 25, 26, 28, 29, 38, 39};  // TRX OH CY MA CL B2 S2, EFM HH CY
constexpr std::array<int, 16> kDefaultParams = {   // HostModel parameters 8-23
    0, 0, 64, 64, 0, 127, 0, 0,                     // AMD AMF EQF EQG FLTF FLTW FLTQ SRR
    0, 100, 64, 0, 0, 0, 0, 0};                     // DIST VOL PAN DEL REV LFOS LFOD LFOM
constexpr int kDefaultLevel = 100;

md::library::Kit defaultKit()
{
    md::library::Kit k;
    k.setName("DEFAULT");
    for (size_t t = 0; t < 16; ++t) {
        k.machine[t] = uint8_t(kDefaultMachines[t]);
        k.model[t] = k.machine[t];
        for (size_t p = 8; p < 24; ++p) k.params[t][p] = uint8_t(kDefaultParams[p - 8]);
        k.level[t] = uint8_t(kDefaultLevel);
        k.lfo[t].destTrack = uint8_t(t);
        k.trigGroup[t] = k.muteGroup[t] = 0xff;
    }
    // a cleared kit's master effects on an MD (OS 1.63): gate box, rhythm echo, EQ flat, dynamix
    k.masterFx.reverb = {0, 0, 64, 0, 0, 127, 127, 96};
    k.masterFx.delay = {32, 0, 32, 0, 0, 127, 0, 96};
    k.masterFx.eq = {64, 64, 64, 64, 64, 64, 64, 127};
    k.masterFx.dynamics = {127, 127, 127, 127, 127, 127, 0, 0};
    return k;
}

// A kit's value for a track's parameter slot (OneParams.h), and the other way
int kitValue(const md::library::Kit& k, int track, int slot)
{
    const auto t = size_t(track);
    if (slot == params::kMachine) return k.machine[t];
    if (slot < params::kLevel) return k.params[t][size_t(slot - params::kKit)];
    if (slot == params::kLevel) return k.level[t];
    const auto& l = k.lfo[t];
    const uint8_t v[params::kLfoSettings] = {l.destTrack, l.destParam, l.shape1, l.shape2, l.type};
    return v[slot - params::kLfo];
}

uint8_t* kitField(md::library::Kit& k, int track, int slot)
{
    const auto t = size_t(track);
    if (slot == params::kMachine) return &k.machine[t];
    if (slot < params::kLevel) return &k.params[t][size_t(slot - params::kKit)];
    if (slot == params::kLevel) return &k.level[t];
    auto& l = k.lfo[t];
    uint8_t* v[params::kLfoSettings] = {&l.destTrack, &l.destParam, &l.shape1, &l.shape2, &l.type};
    return v[slot - params::kLfo];
}

juce::String labelText(const std::array<char, 8>& s) { return juce::String(s.data()); }

constexpr float kSampleScale = 1.0f / 8388608.0f;   // 24-bit engine samples -> [-1, 1)

} // namespace

// ---- Parameters ----

// The machine: the OS's machine id. Its value text is the OS's name for the machine. A change of value resets the
// track's SYN1-8 to the machine's defaults (machineChanged, on the thread that set it).
class MdOneProcessor::MachineParam final : public juce::AudioParameterInt {
public:
    MachineParam(MdOneProcessor& p, int track)
        : juce::AudioParameterInt(juce::ParameterID{params::trackId(track, params::kMachine), 1}, params::trackName(track, params::kMachine),
                                  0, params::kMaxMachine, kDefaultMachines[size_t(track)],
                                  juce::AudioParameterIntAttributes()
                                      .withStringFromValueFunction([&p](int v, int) { return p.machineName(v); })
                                      .withValueFromStringFunction([&p](const juce::String& s) { return p.machineFromText(s); })),
          m_p(p), m_track(track) {}

    // A machine change rewrites SYN1-8 (the machine's defaults): hosts must know that setting it changes other parameters
    // (AU: kAudioUnitParameterFlag_IsGlobalMeta, without which auval -oop fails "Parameter values are different since last
    // set"; .clew increment 0019). It is the only parameter that rewrites others.
    bool isMetaParameter() const override { return true; }

private:
    void valueChanged(int v) override { m_p.machineChanged(m_track, v); }
    MdOneProcessor& m_p;
    const int m_track;
};

// SYN1-8: named after the track's machine (the host is told when that changes), defaulting to the machine's default
class MdOneProcessor::SynParam final : public juce::AudioParameterInt {
public:
    SynParam(MdOneProcessor& p, int track, int k)
        : juce::AudioParameterInt(juce::ParameterID{params::trackId(track, params::kKit + k), 1}, params::trackName(track, params::kKit + k), 0, 127, 0),
          m_p(p), m_track(track), m_k(k) {}

    juce::String getName(int maximumStringLength) const override { return m_p.synName(m_track, m_k).substring(0, maximumStringLength); }
    float getDefaultValue() const override
    {
        const int d = m_p.synDefault(m_p.m_sound[size_t(m_track)].machine.load(std::memory_order_relaxed), m_k);
        return convertTo0to1(float(std::max(d, 0)));
    }

private:
    void valueChanged(int v) override { m_p.synChanged(m_track, m_k, v); }
    MdOneProcessor& m_p;
    const int m_track, m_k;
};

void MdOneProcessor::createParameters()
{
    using Group = juce::AudioProcessorParameterGroup;
    const auto intParam = [](int t, int slot, int def, juce::AudioParameterIntAttributes a = {}) {
        return std::make_unique<juce::AudioParameterInt>(juce::ParameterID{params::trackId(t, slot), 1}, params::trackName(t, slot),
                                                         0, params::maxOf(slot), def, a);
    };
    for (int t = 0; t < kTracks; ++t) {
        auto g = std::make_unique<Group>(params::groupId(t), params::groupId(t), "|");
        auto& slots = m_param[size_t(t)];
        auto machine = std::make_unique<MachineParam>(*this, t);
        slots[params::kMachine] = machine.get();
        g->addChild(std::move(machine));
        for (int k = 0; k < kParams; ++k) {
            std::unique_ptr<juce::AudioParameterInt> p;
            if (k < params::kSyn) p = std::make_unique<SynParam>(*this, t, k);
            else p = intParam(t, params::kKit + k, kDefaultParams[size_t(k - params::kSyn)]);
            slots[size_t(params::kKit + k)] = p.get();
            g->addChild(std::move(p));
        }
        auto level = intParam(t, params::kLevel, kDefaultLevel);
        slots[params::kLevel] = level.get();
        g->addChild(std::move(level));
        // The LFO page: the destination track as T1-T16, the destination parameter by its label, the update type by name
        const auto trackText = juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int v, int) { return "T" + juce::String(v + 1); })
            .withValueFromStringFunction([](const juce::String& s) { return s.trimCharactersAtStart("Tt ").getIntValue() - 1; });
        const auto paramText = juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([this, t](int v, int) {
                if (v >= params::kSyn) return juce::String(params::kKitNames[size_t(juce::jlimit(0, kParams - 1, v))]);
                const auto label = synName(juce::jlimit(0, kTracks - 1, parameter(t, params::kLfo).get()), v);
                return label.fromFirstOccurrenceOf(" ", false, false);
            });
        const auto typeText = juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int v, int) { return juce::String(params::kLfoTypeNames[size_t(juce::jlimit(0, 2, v))]); })
            .withValueFromStringFunction([](const juce::String& s) {
                for (int i = 0; i < 3; ++i) if (s.trim().equalsIgnoreCase(params::kLfoTypeNames[size_t(i)])) return i;
                return s.getIntValue();
            });
        const juce::AudioParameterIntAttributes lfoText[params::kLfoSettings] = {trackText, paramText, {}, {}, typeText};
        for (int i = 0; i < params::kLfoSettings; ++i) {
            auto p = intParam(t, params::kLfo + i, i == 0 ? t : 0, lfoText[i]);
            slots[size_t(params::kLfo + i)] = p.get();
            g->addChild(std::move(p));
        }
        addParameterGroup(std::move(g));

        auto& s = m_sound[size_t(t)];
        s.machine.store(kDefaultMachines[size_t(t)]);
        for (auto& v : s.syn) v.store(0);
        s.synPending.store(0xff);   // the machine's defaults once an OS file is loaded
        for (int k = 0; k < params::kSyn; ++k) m_synNames[size_t(t)][size_t(k)] = params::trackName(t, params::kKit + k);
    }

    auto global = std::make_unique<juce::AudioProcessorParameterGroup>("Global", "Global", "|");
    auto tempo = std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{params::kTempoId, 1}, "TEMPO",
        juce::NormalisableRange<float>(30.0f, 300.0f, 0.01f), 120.0f,
        juce::AudioParameterFloatAttributes().withLabel("BPM").withStringFromValueFunction([](float v, int) { return juce::String(v, 2); }));
    auto channel = std::make_unique<juce::AudioParameterInt>(juce::ParameterID{params::kMidiChannelId, 1}, "MIDI CHANNEL", 0, 16, 0,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([](int v, int) { return v == 0 ? juce::String("OMNI") : juce::String(v); })
            .withValueFromStringFunction([](const juce::String& s) { return s.trim().equalsIgnoreCase("OMNI") ? 0 : s.getIntValue(); }));
    auto accent = std::make_unique<juce::AudioParameterInt>(juce::ParameterID{params::kAccentId, 1}, "ACCENT", 0, 127, 0);
    juce::StringArray maps;
    for (const char* n : params::kNoteMapNames) maps.add(n);
    auto noteMap = std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{params::kNoteMapId, 1}, "NOTE MAP", maps, int(NoteMap::WhiteKeys));
    m_tempoParam = tempo.get();
    m_midiChannelParam = channel.get();
    m_accentParam = accent.get();
    m_noteMapParam = noteMap.get();
    global->addChild(std::move(tempo), std::move(channel), std::move(accent), std::move(noteMap));
    addParameterGroup(std::move(global));
    jassert(getParameters().size() == params::kTotal);
}

// Any thread (the parameter's setValue): the engine is to play the new machine with its SYN defaults. The SYN
// parameters and names follow in handleParameterChanges.
void MdOneProcessor::machineChanged(int track, int machineId)
{
    auto& s = m_sound[size_t(track)];
    if (s.machine.load(std::memory_order_relaxed) == machineId) return;   // hosts resend unchanged values
    const uint64_t d = m_synDefaults[size_t(machineId) & 0xff].load(std::memory_order_relaxed);
    if (d >> 63) {
        for (int k = 0; k < params::kSyn; ++k) s.syn[size_t(k)].store(int((d >> (7 * k)) & 0x7f), std::memory_order_relaxed);
        s.synPending.store(0);
    } else {
        s.synPending.store(0xff);   // no OS file yet (or an id it does not have): the defaults come when one loads
    }
    s.machine.store(machineId, std::memory_order_release);   // after its SYN values (processBlock reads the machine first)
    m_machineChanged.fetch_or(1u << track);
}

void MdOneProcessor::synChanged(int track, int k, int value)
{
    auto& s = m_sound[size_t(track)];
    s.syn[size_t(k)].store(value, std::memory_order_relaxed);
    s.synPending.fetch_and(uint8_t(~(1u << k)));
}

// Message thread: the SYN parameters of the tracks whose machine changed take the values the engine plays (the new
// machine's defaults, unless set since), and are renamed after the machine's knobs.
void MdOneProcessor::handleParameterChanges()
{
    const uint32_t changed = m_machineChanged.exchange(0);
    if (changed == 0) return;
    for (int t = 0; t < kTracks; ++t) {
        if (!((changed >> t) & 1u)) continue;
        for (int k = 0; k < params::kSyn; ++k) {
            auto& p = parameter(t, params::kKit + k);
            const int v = m_sound[size_t(t)].syn[size_t(k)].load(std::memory_order_relaxed);
            if (p.get() != v) setRawValue(p, float(v));
        }
    }
    refreshSynNames(changed);
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));   // SYN1-8 are named after the machine's knobs
}

void MdOneProcessor::refreshSynNames(uint32_t tracks)
{
    const juce::SpinLock::ScopedLockType sl(m_labelLock);
    for (int t = 0; t < kTracks; ++t) {
        if (!((tracks >> t) & 1u)) continue;
        const auto& m = m_labels[size_t(parameter(t, params::kMachine).get()) & 0xff];
        for (int k = 0; k < params::kSyn; ++k) {
            const juce::String label = m.known ? labelText(m.syn[size_t(k)]) : juce::String(params::kKitNames[size_t(k)]);
            m_synNames[size_t(t)][size_t(k)] = "T" + juce::String(t + 1) + " " + (label.isEmpty() ? juce::String("-") : label);
        }
    }
}

juce::String MdOneProcessor::synName(int track, int k) const
{
    const juce::SpinLock::ScopedLockType sl(m_labelLock);
    return m_synNames[size_t(track)][size_t(k)];
}

juce::String MdOneProcessor::machineName(int machineId) const
{
    {
        const juce::SpinLock::ScopedLockType sl(m_labelLock);
        const auto& m = m_labels[size_t(machineId) & 0xff];
        if (m.known) return labelText(m.name);
    }
    return juce::String(machineId);
}

int MdOneProcessor::machineFromText(const juce::String& text) const
{
    const auto clean = [](const juce::String& s) { return s.removeCharacters("- ").toUpperCase(); };
    const auto wanted = clean(text);
    if (wanted.containsOnly("0123456789")) return wanted.getIntValue();
    const juce::SpinLock::ScopedLockType sl(m_labelLock);
    for (int id = 0; id <= params::kMaxMachine; ++id)
        if (m_labels[size_t(id)].known && clean(labelText(m_labels[size_t(id)].name)) == wanted) return id;
    return 0;
}

int MdOneProcessor::synDefault(int machineId, int k) const
{
    const uint64_t d = m_synDefaults[size_t(machineId) & 0xff].load(std::memory_order_relaxed);
    return (d >> 63) && juce::isPositiveAndBelow(k, params::kSyn) ? int((d >> (7 * k)) & 0x7f) : -1;
}

// Message thread, after an OS file loaded (or failed to): the defaults and names of its machines; SYN values still
// waiting for an OS take their machine's defaults now (those set since stay).
void MdOneProcessor::machineTableChanged()
{
    std::array<MachineLabels, 256> labels{};
    std::array<uint64_t, 256> defaults{};
    for (const auto& m : m_machineTable) {
        auto& l = labels[m.id];
        l.known = true;
        m.name.copy(l.name.data(), l.name.size() - 1);
        uint64_t d = uint64_t(1) << 63;
        for (int k = 0; k < params::kSyn; ++k) {
            m.params[size_t(k)].copy(l.syn[size_t(k)].data(), l.syn[size_t(k)].size() - 1);
            d |= uint64_t(m.defaults[size_t(k)] & 0x7f) << (7 * k);
        }
        defaults[m.id] = d;
    }
    {
        const juce::SpinLock::ScopedLockType sl(m_labelLock);
        m_labels = labels;
    }
    for (size_t id = 0; id < defaults.size(); ++id) m_synDefaults[id].store(defaults[id]);
    for (int t = 0; t < kTracks; ++t) {
        auto& s = m_sound[size_t(t)];
        const int id = s.machine.load();
        const uint8_t pending = s.synPending.load();
        if (pending == 0 || synDefault(id, 0) < 0) continue;
        for (int k = 0; k < params::kSyn; ++k)
            if ((pending >> k) & 1) {
                s.syn[size_t(k)].store(synDefault(id, k));
                auto& p = parameter(t, params::kKit + k);
                if (p.get() != synDefault(id, k)) setRawValue(p, float(synDefault(id, k)));
            }
        s.synPending.store(0);
    }
    refreshSynNames(0xffff);
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));   // SYN names and machine names
}

void MdOneProcessor::setRawValue(juce::RangedAudioParameter& p, float value)
{
    p.setValueNotifyingHost(p.convertTo0to1(value));
}

void MdOneProcessor::setRaw(int track, int slot, int value)
{
    if (juce::isPositiveAndBelow(track, kTracks) && juce::isPositiveAndBelow(slot, params::kTrackParams))
        setRawValue(parameter(track, slot), float(juce::jlimit(0, params::maxOf(slot), value)));
}

void MdOneProcessor::setMachine(int track, int machineId)
{
    setRaw(track, params::kMachine, machineId);
    if (juce::MessageManager::existsAndIsCurrentThread()) handleParameterChanges();
}

void MdOneProcessor::setMidiChannel(int channel) { setRawValue(*m_midiChannelParam, float(juce::jlimit(0, 16, channel))); }
void MdOneProcessor::setTempo(double bpm) { setRawValue(*m_tempoParam, float(juce::jlimit(30.0, 300.0, bpm))); }
void MdOneProcessor::setAccent(int amount) { setRawValue(*m_accentParam, float(juce::jlimit(0, 127, amount))); }
void MdOneProcessor::setNoteMap(NoteMap map) { setRawValue(*m_noteMapParam, float(int(map))); }

// ---- Kits ----

void MdOneProcessor::kitToParameters(const md::library::Kit& kit)
{
    for (int t = 0; t < kTracks; ++t)
        for (int slot = 0; slot < params::kTrackParams; ++slot)   // the machine first: it resets SYN1-8, the kit's values go over them
            setRaw(t, slot, kitValue(kit, t, slot));
    if (juce::MessageManager::existsAndIsCurrentThread()) handleParameterChanges();
}

md::library::Kit MdOneProcessor::parametersToKit() const
{
    md::library::Kit kit;
    {
        const juce::ScopedLock sl(m_kitLock);
        kit = m_kit;
    }
    for (int t = 0; t < kTracks; ++t)
        for (int slot = 0; slot < params::kTrackParams; ++slot) {
            uint8_t& field = *kitField(kit, t, slot);
            const int v = parameter(t, slot).get();
            if (juce::jlimit(0, params::maxOf(slot), int(field)) != v) field = uint8_t(v);   // else keep what the kit had
        }
    return kit;
}

void MdOneProcessor::loadKit(const md::library::Kit& kit, const juce::String& bank)
{
    {
        const juce::ScopedLock sl(m_kitLock);
        m_kit = kit;
        m_kitBank = bank;
    }
    kitToParameters(kit);
}

void MdOneProcessor::loadDefaultKit()
{
    auto kit = defaultKit();
    uint32_t unknown = 0;   // tracks whose machine's SYN defaults are not known yet (no OS file)
    for (int t = 0; t < kTracks; ++t)
        for (int k = 0; k < params::kSyn; ++k) {
            const int d = synDefault(kit.machine[size_t(t)], k);
            if (d < 0) unknown |= 1u << t;
            else kit.params[size_t(t)][size_t(k)] = uint8_t(d);
        }
    loadKit(kit);
    for (int t = 0; t < kTracks; ++t)
        if ((unknown >> t) & 1u) m_sound[size_t(t)].synPending.store(0xff);
}

juce::String MdOneProcessor::kitName() const
{
    const juce::ScopedLock sl(m_kitLock);
    return m_kit.name;
}

juce::String MdOneProcessor::kitBank() const
{
    const juce::ScopedLock sl(m_kitLock);
    return m_kitBank;
}

// ---- Engine ----

juce::AudioProcessor::BusesProperties MdOneProcessor::makeBuses()
{
    // Main: the dry mix, always on. The track buses and the sends are off until the host (or its user) enables them;
    // AU hosts get every bus (AUs have no disabled buses).
    auto b = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
    for (int t = 0; t < kTracks; ++t)
        b = b.withOutput("Track " + juce::String(t + 1), juce::AudioChannelSet::mono(), false);
    return b.withOutput("Reverb send", juce::AudioChannelSet::stereo(), false)
            .withOutput("Delay send", juce::AudioChannelSet::stereo(), false);
}

MdOneProcessor::MdOneProcessor() : AudioProcessor(makeBuses())
{
    m_notes.reserve(kMaxPendingNotes);   // the audio thread only fills it up to this capacity
    prepareClock(kEngineRate, 512);      // in case a host processes before prepareToPlay
    createParameters();                  // the default kit, SYN1-8 pending until an OS file gives their defaults
    m_kit = defaultKit();
    m_firmwarePath = loadSharedOsPath();   // never a compiled-in path: fresh installs start unconfigured
    loadEngine();
    startTimerHz(30);
}

MdOneProcessor::~MdOneProcessor() { stopTimer(); }

void MdOneProcessor::loadEngine()
{
    std::unique_ptr<fw::Firmware> fwNew;
    std::unique_ptr<Engine> engNew;
    std::vector<md::engine::MachineInfo> table;
    juce::String status;
    if (m_firmwarePath.isEmpty())
        status = "No Machinedrum OS file selected";
    else if (!juce::File(m_firmwarePath).existsAsFile())
        status = "OS file not found: " + m_firmwarePath;
    else try {
        const std::filesystem::path path(m_firmwarePath.toStdString());
        fwNew = std::make_unique<fw::Firmware>(fw::loadFirmware(path));   // throws on anything but a Machinedrum OS
        auto c = fw::parseContainer(fw::parseSysex(fw::readFile(path)));
        engNew = std::make_unique<Engine>(*fwNew, std::move(c.sections.at(0).data));   // boots the voice DSP
        // Warm-up, off the audio thread: with nothing triggered, the booted engine must render silence.
        Engine::Output out;
        for (int b = 0; b < kWarmUpBlocks; ++b) {
            if (!engNew->render(out)) throw std::runtime_error("voice DSP fault during warm-up: " + engNew->fault());
            const auto silent = [](const auto& frames) {
                return std::all_of(frames.begin(), frames.end(), [](const auto& f) { return std::all_of(f.begin(), f.end(), [](int32_t s) { return s == 0; }); });
            };
            if (!silent(out.mix.main) || !silent(out.mix.frame) || !silent(out.mix.rev) || !silent(out.mix.del))
                throw std::runtime_error("the engine is not silent after boot");
        }
        // Voice budget and silence release stay off (HostModel's defaults): every track runs its voice, as on the MD.
        table = engNew->os().machines();
        status = "OS " + versionText(fwNew->version) + " loaded, engine ready ("
               + (dsp56k::g_jitSupported ? "JIT" : "interpreter") + ")";
    } catch (const std::exception& e) {
        status = "OS file load failed: " + juce::String(e.what());
        engNew.reset(); fwNew.reset();
    }
    // The machines' defaults and names first: SYN values waiting for an OS take their defaults before the new engine's
    // first block
    m_machineTable = std::move(table);
    machineTableChanged();
    {
        const juce::ScopedLock sl(m_engineLock);
        m_engineReady = false;
        m_engine = std::move(engNew);   // the old engine goes first, while its firmware still exists
        m_firmware = std::move(fwNew);
        m_engineBlocks = 0;
        m_status = status;
        // the new engine starts from HostModel's defaults: every parameter and the tempo go to it at the next block
        for (auto& a : m_applied) { a.machine = -1; a.param.fill(-1); a.level = -1; a.lfo.fill(-1); }
        m_appliedTempo = -1;
        m_appliedAccent = -1;
        m_blockAvail = 0;
        m_notes.clear();
        m_engineReady = m_engine != nullptr;
    }
}

void MdOneProcessor::setFirmwarePath(const juce::String& path, bool persist)
{
    m_firmwarePath = path;
    loadEngine();
    if (persist && m_engineReady) saveSharedOsPath(path);   // a failed load must not poison the other plugins
}

void MdOneProcessor::refreshSharedOsPath()
{
    const auto p = loadSharedOsPath();
    if (p.isNotEmpty() && p != m_firmwarePath && juce::File(p).existsAsFile()) setFirmwarePath(p, false);
}

void MdOneProcessor::clearFirmware()
{
    m_firmwarePath.clear();
    loadEngine();
    saveSharedOsPath({});
}

// Message thread, without m_engineLock: processBlock skips any block it cannot lock, so an editor polling the status
// must never hold it. m_status and m_engine change on the message thread only (loadEngine); the audio thread writes an
// engine's fault before it clears m_engineReady, so the fault is read only once that is clear; prepareClock publishes
// the conversion.
juce::String MdOneProcessor::statusText() const
{
    juce::String s = m_status;
    if (m_engine && !m_engineReady.load() && !m_engine->fault().empty())
        s += " | DSP FAULT: " + juce::String(m_engine->fault());
    else if (m_engine)
        s += " | engine time " + juce::String(double(m_engineBlocks.load()) * Engine::kBlock / kEngineRate, 1) + " s";
    if (const double rate = m_statusRate.load(); rate > 0.0)
        s += " | host rate " + juce::String(rate, 0) + " Hz: converted from the engine's 44100 (latency " + juce::String(m_statusLag.load()) + " samples)";
    return s;
}

// Audio thread, under m_engineLock: the parameters -> HostModel, only what changed since the last block. Reads atomics
// only (the parameters' values and m_sound): no lock, no allocation.
void MdOneProcessor::applyParameters()
{
    auto& h = m_engine->host();
    for (int t = 0; t < kTracks; ++t) {
        const auto& s = m_sound[size_t(t)];
        const auto& p = m_param[size_t(t)];
        auto& a = m_applied[size_t(t)];
        const int m = s.machine.load(std::memory_order_acquire);
        if (m != a.machine) {
            a.machine = m;
            h.setMachine(t, uint8_t(m));   // pending until the track's next trigger; resets HostModel's SYN1-8 to defaults
            for (int k = 0; k < params::kSyn; ++k) a.param[size_t(k)] = -1;   // ours go over them
        }
        for (int k = 0; k < kParams; ++k) {
            const int v = k < params::kSyn ? s.syn[size_t(k)].load(std::memory_order_relaxed) : p[size_t(params::kKit + k)]->get();
            if (v != a.param[size_t(k)]) { a.param[size_t(k)] = v; h.setParam(t, k, v); }
        }
        const int level = p[params::kLevel]->get();
        if (level != a.level) { a.level = level; h.setLevel(t, level); }
        std::array<int, params::kLfoSettings> lfo{};
        for (int i = 0; i < params::kLfoSettings; ++i) lfo[size_t(i)] = p[size_t(params::kLfo + i)]->get();
        if (lfo != a.lfo) { a.lfo = lfo; h.setLfo(t, lfo[0], lfo[1], lfo[2], lfo[3], lfo[4]); }
    }
    if (const int accent = m_accentParam->get(); accent != m_appliedAccent) {
        m_appliedAccent = accent;
        h.setAccentAmount(accent);
    }
}

// ---- Audio ----

void MdOneProcessor::prepareClock(double sampleRate, int samplesPerBlock)
{
    m_hostRate = sampleRate > 0.0 ? sampleRate : kEngineRate;
    m_needsResample = std::abs(m_hostRate - kEngineRate) > 0.5;
    m_maxChunk = std::max(1, samplesPerBlock);
    int historyFrames = 0;
    if (m_needsResample) {
        m_toHost.design(kEngineRate, m_hostRate);
        m_engineAt.set(kEngineRate, m_hostRate);
        // output m reads engine frames up to floor((m - outLag) * 44100 / rate) + H; the chunk ending at m has rendered
        // those up to floor(m * 44100 / rate): outLag * 44100 / rate >= H + 1 keeps every read inside (as Monomodule)
        m_outLag = int(std::ceil((m_toHost.halfTaps() + 1) * m_hostRate / kEngineRate));
        m_maxEngine = int(std::ceil(m_maxChunk * kEngineRate / m_hostRate)) + 2;
        historyFrames = m_maxEngine + 2 * m_toHost.halfTaps() + 64;
        m_taps.assign(size_t(m_toHost.numTaps()), 0.f);
    } else {
        m_outLag = 0;
        m_maxEngine = m_maxChunk;
    }
    m_statusRate.store(m_needsResample ? m_hostRate : 0.0);   // for statusText (message thread)
    m_statusLag.store(m_outLag);
    for (auto& s : m_eng) s.assign(size_t(m_maxEngine), 0.f);
    for (auto& h : m_hist) h.prepare(std::max(historyFrames, 1));
    m_lastSound.fill(-1);
    m_notes.clear();
    m_keyboardMidi.ensureSize(2048);
    m_hostPos = m_enginePos = 0;
    m_blockAvail = 0;
}

void MdOneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    {
        const juce::ScopedLock sl(m_engineLock);
        prepareClock(sampleRate, samplesPerBlock);
    }
    setLatencySamples(m_outLag);
}

bool MdOneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (!layouts.inputBuses.isEmpty() || layouts.outputBuses.size() != kNumBuses) return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < kNumBuses; ++i) {
        const auto& set = layouts.outputBuses.getReference(i);
        const bool track = i < kReverbBus;
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo() && !(track && set == juce::AudioChannelSet::mono())) return false;
    }
    return true;
}

// A note for track `track` at host sample `hostSample`: it waits for the first engine frame at or after that sample's
// engine time, s * 44100 / rate. Kept in frame order (the editor's notes come after the host's in the block).
void MdOneProcessor::queueNote(int64_t hostSample, int track, int velocity, bool accent)
{
    if (m_notes.size() >= kMaxPendingNotes) return;
    const PendingNote note{m_needsResample ? m_engineAt.ceilAt(hostSample) : hostSample, track, velocity, accent};
    m_notes.insert(std::upper_bound(m_notes.begin(), m_notes.end(), note, [](const PendingNote& a, const PendingNote& b) { return a.frame < b.frame; }), note);
}

// One engine block (32 frames at 44.1 kHz) into m_block. A DSP fault stops the engine (statusText() reports it); the
// rest is silence.
bool MdOneProcessor::renderEngineBlock()
{
    m_blockAvail = Engine::kBlock;
    if (!m_engineReady || !m_engine->render(m_out)) {
        m_engineReady = false;
        for (auto& s : m_block) s.fill(0.f);
        return false;
    }
    m_engineBlocks.fetch_add(1, std::memory_order_relaxed);
    const auto& mix = m_out.mix;
    for (int f = 0; f < Engine::kBlock; ++f) {
        m_block[kStreamMain][size_t(f)] = float(mix.main[size_t(f)][0]) * kSampleScale;
        m_block[kStreamMain + 1][size_t(f)] = float(mix.main[size_t(f)][1]) * kSampleScale;
        m_block[kStreamRev][size_t(f)] = float(mix.rev[size_t(f)][0]) * kSampleScale;
        m_block[kStreamRev + 1][size_t(f)] = float(mix.rev[size_t(f)][1]) * kSampleScale;
        m_block[kStreamDel][size_t(f)] = float(mix.del[size_t(f)][0]) * kSampleScale;
        m_block[kStreamDel + 1][size_t(f)] = float(mix.del[size_t(f)][1]) * kSampleScale;
    }
    // A track on its own bus: the hardware's individual-output formula (sample x VOL gain, no pan, no sends), which
    // the MPC port's taps used as well
    for (int t = 0; t < kTracks; ++t) {
        auto& dst = m_block[size_t(kStreamTrack + t)];
        if (!((m_ownMask >> t) & 1u)) { dst.fill(0.f); continue; }
        const uint32_t vol = m_engine->host().mixerInput(t).mix[1];
        for (int f = 0; f < Engine::kBlock; ++f)
            dst[size_t(f)] = float(md::engine::Mixer::solo(m_out.tracks[size_t(t)][size_t(f)], vol)) * kSampleScale;
    }
    return true;
}

// One chunk of a host block: renders the engine frames up to the chunk's last sample (in 32-frame engine blocks, the
// last one possibly only partly consumed until the next chunk), triggering the notes that are due at each engine block
// boundary, then writes the host samples.
void MdOneProcessor::processChunk(int offset, int n)
{
    const int64_t hostEnd = m_hostPos + n, e0 = m_enginePos;
    const int64_t e1 = !m_needsResample ? hostEnd : std::max(e0, m_engineAt.floorAt(hostEnd - 1) + 1);
    const int nEngine = int(e1 - e0);
    auto& h = m_engine->host();
    const auto trigger = [&](const PendingNote& note) {
        h.trigger(note.track, note.velocity, note.accent);
        m_triggered.fetch_or(1u << note.track, std::memory_order_relaxed);
    };
    size_t ev = 0;
    for (int pos = 0; pos < nEngine;) {
        if (m_blockAvail == 0) {
            // engine block boundary: the notes due by this frame trigger in this block
            for (; ev < m_notes.size() && m_notes[ev].frame <= e0 + pos; ++ev) trigger(m_notes[ev]);
            renderEngineBlock();
        }
        const int c = std::min(nEngine - pos, m_blockAvail);
        const int from = Engine::kBlock - m_blockAvail;
        for (int s = 0; s < kStreams; ++s)
            std::copy_n(m_block[size_t(s)].data() + from, c, m_eng[size_t(s)].data() + pos);
        m_blockAvail -= c;
        pos += c;
    }
    // notes due after the last boundary trigger at the next one, which is no earlier than their frame
    for (; ev < m_notes.size() && m_notes[ev].frame < e1; ++ev) trigger(m_notes[ev]);
    m_notes.erase(m_notes.begin(), m_notes.begin() + std::ptrdiff_t(ev));
    writeOutputs(offset, n, nEngine);
    m_hostPos = hostEnd;
    m_enginePos = e1;
}

// The chunk's streams into the host channels (m_sinks) at `offset`. At 44.1 kHz the engine frames are the output, bit
// for bit. Otherwise every stream keeps a history, fed whether it is heard or not (switching outputs never meets a
// gap), and output sample m is the band-limited stream at engine time (m - outLag) * 44100 / rate: one set of taps per
// sample, shared by every stream. A stream whose whole window is silent is skipped (its output stays 0).
void MdOneProcessor::writeOutputs(int offset, int n, int nEngine)
{
    if (!m_needsResample) {
        for (int k = 0; k < m_numSinks; ++k) {
            const auto& sink = m_sinks[size_t(k)];
            for (float* ch : sink.ch)
                if (ch != nullptr) juce::FloatVectorOperations::copy(ch + offset, m_eng[size_t(sink.stream)].data(), n);
        }
        return;
    }
    for (int s = 0; s < kStreams; ++s) {
        const float* x = m_eng[size_t(s)].data();
        m_hist[size_t(s)].push(x, nEngine);
        for (int i = nEngine - 1; i >= 0; --i)
            if (x[i] != 0.f) { m_lastSound[size_t(s)] = m_enginePos + i; break; }
    }
    const int half = m_toHost.halfTaps(), taps = m_toHost.numTaps();
    const int64_t first = m_hostPos - m_outLag;
    const int64_t earliest = m_engineAt.floorAt(first) - half + 1;   // the oldest frame this chunk's outputs read
    std::array<const Sink*, kStreams> active{};
    int count = 0;
    for (int k = 0; k < m_numSinks; ++k)
        if (m_lastSound[size_t(m_sinks[size_t(k)].stream)] >= earliest) active[size_t(count++)] = &m_sinks[size_t(k)];
    if (count == 0) return;
    for (int k = 0; k < n; ++k) {
        const auto at = m_engineAt.at(first + k);
        const float* t = m_toHost.taps(at, m_taps.data());
        for (int a = 0; a < count; ++a) {
            const auto& sink = *active[size_t(a)];
            const float v = m_hist[size_t(sink.stream)].dot(at.index - half + 1, t, taps);
            sink.ch[0][offset + k] = v;
            if (sink.ch[1] != nullptr) sink.ch[1][offset + k] = v;
        }
    }
}

void MdOneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    double bpm = double(m_tempoParam->get());
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0) bpm = *hostBpm;
    m_currentBpm.store(bpm);
    m_keyboardMidi.clear();
    m_keyboard.processNextMidiBuffer(m_keyboardMidi, 0, n, true);   // the editor's notes, kept apart from the host's
    const juce::ScopedTryLock sl(m_engineLock);   // an OS load in progress: skip this block, never wait
    if (!sl.isLocked() || !m_engineReady) { midi.clear(); return; }

    // Where the streams go: a host channel pointer per bus channel, nullptr for a bus the host has off
    const auto channel = [&](int bus, int ch) -> float* {
        const auto* b = getBus(false, bus);
        if (b == nullptr || !b->isEnabled() || ch >= b->getNumberOfChannels()) return nullptr;
        const int idx = getChannelIndexInProcessBlockBuffer(false, bus, ch);
        return idx < buffer.getNumChannels() ? buffer.getWritePointer(idx) : nullptr;
    };
    m_numSinks = 0;
    const auto addSink = [&](int stream, float* a, float* b) { if (a != nullptr) m_sinks[size_t(m_numSinks++)] = {stream, {a, b}}; };
    addSink(kStreamMain, channel(0, 0), nullptr);
    addSink(kStreamMain + 1, channel(0, 1), nullptr);
    addSink(kStreamRev, channel(kReverbBus, 0), nullptr);
    addSink(kStreamRev + 1, channel(kReverbBus, 1), nullptr);
    addSink(kStreamDel, channel(kDelayBus, 0), nullptr);
    addSink(kStreamDel + 1, channel(kDelayBus, 1), nullptr);
    // A track routed to its own bus leaves the main mix only while that bus is on, so it never goes silent
    uint32_t own = 0;
    for (int t = 0; t < kTracks; ++t) {
        if (!m_ownBus[size_t(t)].load(std::memory_order_relaxed)) continue;
        if (float* a = channel(kTrackBus + t, 0)) { own |= 1u << t; addSink(kStreamTrack + t, a, channel(kTrackBus + t, 1)); }
    }
    m_ownMask = own;
    m_engine->dryMute = own;

    applyParameters();
    // tempo-synced LFOs and E12 retrigs: the OS keeps BPM x 24 as an integer, so only a change of that reaches it
    if (const long word = std::lround(std::clamp(bpm, 30.0, 300.0) * 24.0); word != m_appliedTempo) {
        m_appliedTempo = word;
        m_engine->host().setTempo(bpm);
    }

    // Notes -> tracks through the note map, on the MIDI channel set (or any); the editor's on every channel. With an
    // accent amount set, a note at full velocity is an accented trig.
    const int midiChannel = m_midiChannelParam->get();
    const auto map = noteMap();
    const bool accents = m_accentParam->get() > 0;
    const auto queue = [&](const juce::MidiBuffer& events, int channelFilter) {
        for (const auto meta : events) {
            const uint8_t* d = meta.data;
            if (meta.numBytes < 3 || (d[0] & 0xf0) != 0x90 || d[2] == 0) continue;   // note-ons only: drums ignore note-off
            if (channelFilter != 0 && (d[0] & 0x0f) + 1 != channelFilter) continue;
            if (const int t = params::trackForNote(d[1], map); t >= 0) queueNote(m_hostPos + meta.samplePosition, t, d[2], accents && d[2] == 127);
        }
    };
    queue(midi, midiChannel);
    queue(m_keyboardMidi, 0);

    for (int done = 0; done < n;) {   // blocks longer than prepared are split: no allocation on the audio thread
        const int c = std::min(m_maxChunk, n - done);
        processChunk(done, c);
        done += c;
    }
    midi.clear();
}

juce::AudioProcessorEditor* MdOneProcessor::createEditor() { return new OneEditor(*this); }

// ---- State ----
//
// Schema 2 (increment 0004), XML:
//   <ModuledrumState schema="2" ownBusTracks="(bit t: track t+1 on its own bus)">
//     <PARAMETERS> <PARAM id="track0_machine" value="16"/> ... every parameter, raw (tempo in BPM) </PARAMETERS>
//     <KIT name="TRX" bank="MD_presets_1_50" position="0" sysex="F000203C...F7"/>
//   </ModuledrumState>
// KIT is parametersToKit() as a kit dump (encodeKit, hex): the loaded kit with the parameters' values, so the kit's other
// contents survive and the session exports as a kit. The OS file is not part of the state (it is the shared setting).
// Schema 1 (0003: midiChannel, tempo, ownBusTracks attributes; firmwarePath, no longer read) restores those settings.

void MdOneProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("ModuledrumState");
    state.setProperty("schema", kStateSchema, nullptr);
    int own = 0;
    for (int t = 0; t < kTracks; ++t) own |= trackOutput(t) ? 1 << t : 0;
    state.setProperty("ownBusTracks", own, nullptr);
    juce::ValueTree values("PARAMETERS");
    for (auto* p : getParameters()) {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(p);
        if (r == nullptr) continue;
        const float raw = r->convertFrom0to1(r->getValue());
        juce::ValueTree v("PARAM");
        v.setProperty("id", r->getParameterID(), nullptr);
        v.setProperty("value", r == m_tempoParam ? juce::var(double(raw)) : juce::var(juce::roundToInt(raw)), nullptr);
        values.appendChild(v, nullptr);
    }
    state.appendChild(values, nullptr);
    const auto kit = parametersToKit();
    const auto sysex = md::library::encodeKit(kit);
    juce::ValueTree k("KIT");
    k.setProperty("name", juce::String(kit.name), nullptr);
    k.setProperty("bank", kitBank(), nullptr);
    k.setProperty("position", int(kit.position), nullptr);
    k.setProperty("sysex", juce::String::toHexString(sysex.data(), int(sysex.size()), 0).toUpperCase(), nullptr);
    state.appendChild(k, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MdOneProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("ModuledrumState")) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    const int own = state.getProperty("ownBusTracks", 0);
    for (int t = 0; t < kTracks; ++t) setTrackOutput(t, ((own >> t) & 1) != 0);
    if (int(state.getProperty("schema", 1)) < 2) {   // 0003: the settings only
        setMidiChannel(state.getProperty("midiChannel", 0));
        setTempo(state.getProperty("tempo", 120.0));
        return;
    }

    // The kit (its non-parameter contents, and every kit parameter where PARAMETERS lacks one), then the parameters,
    // the machine before SYN1-8 on each track
    auto kit = defaultKit();
    juce::String bank;
    if (const auto k = state.getChildWithName("KIT"); k.isValid()) {
        juce::MemoryBlock bytes;
        bytes.loadFromHexString(k.getProperty("sysex").toString());
        md::library::Kit decoded;
        if (md::library::decodeKit(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize(), decoded)) kit = decoded;
        bank = k.getProperty("bank").toString();
    }
    std::map<juce::String, juce::var> values;
    for (const auto v : state.getChildWithName("PARAMETERS"))
        values[v.getProperty("id").toString()] = v.getProperty("value");
    const auto value = [&](const juce::String& id, const juce::var& fallback) {
        const auto it = values.find(id);
        return it != values.end() ? it->second : fallback;
    };
    {
        const juce::ScopedLock sl(m_kitLock);
        m_kit = kit;
        m_kitBank = bank;
    }
    for (int t = 0; t < kTracks; ++t)
        for (int slot = 0; slot < params::kTrackParams; ++slot)
            setRaw(t, slot, value(params::trackId(t, slot), kitValue(kit, t, slot)));
    setTempo(value(params::kTempoId, 120.0));
    setMidiChannel(value(params::kMidiChannelId, 0));
    setAccent(value(params::kAccentId, 0));
    setNoteMap(NoteMap(juce::jlimit(0, 1, int(value(params::kNoteMapId, int(NoteMap::WhiteKeys))))));
    if (juce::MessageManager::existsAndIsCurrentThread()) handleParameterChanges();
}

} // namespace md::plugin::one
