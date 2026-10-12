---
id: 4
status: done
tags:
- engine
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T17:02:08Z
---
## Why
Automation and project recall: every knob of all 16 tracks as a host parameter, and the full state (kit) saved with the
project. The MPC port already defines the parameter set (`resources/mpc-vst-machinedrum/vst/params.json`,
`vst/gen_params.py`, read the docstring: the 8+6 AMP/EFX + ROUTE split matches the hardware's three pages). Depends on
increment 0003 (the processor renders) and uses the kit library from 0005 (`src/core/md/library/`).

## Decisions
- **One `juce::AudioProcessorValueTreeState`-free design is fine but not required**; what matters is that parameters are
  raw 0-127 integers (`juce::AudioParameterInt`) with the hardware's names, grouped per track (`juce::AudioProcessorParameterGroup`
  "T1".."T16"), in a stable order (append-only once released). Per track: `machine` (0-191, the raw OS id),
  `syn1..syn8`, `amd amf eqf eqg fltf fltw fltq srr`, `dist vol pan del rev`, `lfos lfod lfom` (HostModel raw order
  0-23, see `src/core/md/HostModel.h` kParams), `level` (kit LEV), plus the LFO settings `lfo_track lfo_param lfo_shape1
  lfo_shape2 lfo_type` (HostModel::setLfo). Globals: `tempo` (used when the host gives none), `midi_channel`,
  `accent`. No voice budget, no ROM switch, no randomise (MPC-only).
- SYN labels follow the machine: `getName()` of syn1..8 returns the machine's own label from `MachineRunner`
  (`machineTable()` on the processor; `"-"` for unused cells). Hosts cache names, so also notify
  `AudioProcessorListener::audioProcessorChanged` with a parameter-info change when a machine changes.
- **Machine change semantics as on the MD**: `setMachine` resets SYN1-8 to the machine's defaults; the processor must push
  those defaults back into the host parameters so the UI and automation see them (the MPC port's engine.cpp does this,
  "resets untouched ones to the machine's own defaults").
- Parameter -> engine: apply on the audio thread at block boundaries from an atomic snapshot (no locks in processBlock);
  engine -> parameter only on machine change and kit load.
- **State**: `getStateInformation` writes a `juce::ValueTree` (version tag, OS path not included, all parameters,
  the loaded kit's name/bank for display, the LFO settings) as XML; `setStateInformation` restores it and re-applies
  to the engine. Loading a kit (`md::library::Kit` via `applyKit`) also sets every host parameter from the kit so
  state and automation stay in sync; `kitToParameters` / `parametersToKit` are the two directions, and `Kit` is the
  canonical in-memory form (so a session can be exported as a kit `.syx` with `encodeKit`).

## Acceptance criteria
- [x] All per-track and global parameters listed above exist, automatable, with correct ranges and hardware names
  (500: 16 x 31 + 4; `md-plugin-check --check-state`, ctest `plugin-state`)
- [x] Changing `machine` relabels syn1..8 in the host and resets them to the machine's defaults
- [x] Headless test (extend `md-plugin-check`): load kit 0 of the user's file through the processor, render the kit-test
  sequence, save state, create a fresh processor, restore state, render again: identical hash. Also: the same
  sequence rendered through `applyKit` directly (tests/test_kits.cpp, `kKit0Hash`) matches when the sequence is the same
  (the processor warms its engine up for 16 silent blocks after boot, so "the same sequence" includes them: `applyKit`
  on the engine after 16 silent blocks = the processor's `da62047690565e38`; without them it is `kKit0Hash`, checked too)
- [x] A kit loaded, then exported with `parametersToKit` + `encodeKit`, decodes back to the same parameter values
  (all 64 kits of the user's file export to the same dump byte for byte)
- [ ] `auval` passes (parameter count ~600 is fine; check it does not time out)
  (not run: auval only sees the installed plugin, and this build is not installed. The worktree's AU was exercised
  in-process instead, see Verification: 500 parameters enumerate in under 1 ms, so no time-out risk)
- [ ] Standalone: turning a knob in the (0008) editor or the host's generic UI changes the sound
  (headless equivalent done: parameters set as a host sets them change the render, through the processor and through
  the AU in-process; no screen in this session, and the editor's knobs are 0008)

## Also in this increment: the MIDI note map
0003 implemented the chromatic 36-51 and 0-15 maps. The hardware maps tracks 1-16 to the *white keys from C2*
(36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62, MD manual appendix B). Make that the default and keep the two
chromatic maps as a `note_map` setting (`MdOneProcessor::trackForNote` is the one place). Extend the `plugin-render`
regression (`src/plugin/Check.cpp`, `kRenderHashOs163`) to the new default map and record the new hash; the test must
set the map explicitly so each map has its own expected hash.

## What 0003 left for this (see `.clew/archive/0003-*.md`)
- Sound API on the processor: `setMachine(t, id)` (message thread; resets SYN1-8 to the machine's defaults),
  `setTrackParam(t, p 0-23, v)`, `setTrackLevel`, `setTrackOutput`, `setMidiChannel`, `setTempo`, with getters. Values
  are atomics; `applyKit()` (the processor's, not the library's) pushes only what changed into `HostModel` at the start
  of each `processBlock`. Replace those atomics with the host parameters' values.
- Restore order: a machine change resets SYN1-8, so restore/automation must apply `machine` before `syn*` (the MPC
  port's "touched" flags or Monomodule's per-machine shadows both solve it).
- State is schema 1 and already saves `midiChannel`, `tempo`, `ownBusTracks`; bump the schema when adding the kit.

## Notes
- Keep the parameter id strings identical to the MPC port's keys (`track0_syn1`, `track0_vol`, ...) where they exist,
  so kit/preset tooling can be shared later.
- `HostModel::param()` reads the raw value; `voiceParams()` is after smoothing and LFO (for the UI's live view, later).

## Tasks
- [x] `src/plugin/one/OneParams.h`: the layout (slots, ids, names, ranges), the note maps
- [x] `MdOneProcessor`: parameters replace 0003's atomics; machine change (SYN defaults at once in the engine, SYN
  parameters and names on the message thread, host told); `applyParameters` at each block; accent; note map
- [x] Kits: `loadKit`, `loadDefaultKit`, `kitToParameters`, `parametersToKit`, `kitName`/`kitBank`
- [x] State schema 2 (all parameters + the kit dump), schema 1 read
- [x] Note map: white keys default, chromatic as `note_map`; `plugin-render` with an explicit map and a hash per map
- [x] `md-plugin-check --check-state`, ctest `plugin-state`

## Decisions taken while implementing (2026-10-12)
- **Layout** (`OneParams.h`, append only): per track `track<t>_machine` (0-191), `_syn1`..`_syn8`, `_amd _amf _eqf _eqg
  _fltf _fltw _fltq _srr _dist _vol _pan _del _rev _lfos _lfod _lfom` (HostModel order 0-23), `_level`, `_lfo_track
  _lfo_param _lfo_shp1 _lfo_shp2 _lfo_type` = 31 per track in group `T<t+1>`; then group `Global`: `tempo`,
  `midi_channel`, `accent`, `note_map`. **500 parameters.** Ids are the MPC port's keys; its keys are `lfo_shp1/2`,
  not this increment's `lfo_shape1/2`, so the port's spelling won ("identical where they exist"). The order is ours
  (the port's file is grouped by when each key was added). Names: `T1 MACHINE`, `T1 <SYN label>`, `T1 AMD` .. `T1 LFOM`,
  `T1 LEV`, `T1 LFO TRACK/PARAM/SHP1/SHP2/UPDTE` (the LFO page's labels, SpecData.h), `TEMPO`, `MIDI CHANNEL`,
  `ACCENT`, `NOTE MAP`. SYN names are `T<n> ` + the machine's label from the OS table, `T<n> -` for an unused knob,
  `T<n> SYN<k>` with no OS. Value texts: the machine as the OS names it (`TRXBD`; typed back by name or id), LFO track
  `T1`..`T16`, LFO param by the destination's label, update `FREE/TRIG/HOLD`, channel 0 `OMNI`; others the raw number.
- All track parameters are `juce::AudioParameterInt` (raw value = the int). JUCE's AU wrapper treats AudioParameterInt
  as continuous (only `isDiscrete()` parameters get indexed units), so AU hosts see 0..1 and the value text; VST3 sees
  the step count. Kept: indexed AU parameters would make 0-127 knobs menus in Logic. `tempo` is an
  `AudioParameterFloat` 30-300 (0.01 steps; the OS keeps BPM x 24), `note_map` an `AudioParameterChoice`.
- **Parameters -> engine:** processBlock reads every parameter's own atomic at the start of each block and passes what
  changed to HostModel (`applyParameters`, replacing 0003's `applyKit` and its atomics). Exception, machine and SYN1-8:
  the engine reads them from `m_sound`, written by the parameters' `valueChanged` (synchronous in `setValue`, any
  thread). A machine change stores the machine's SYN defaults there first and the machine last (release/acquire), so the
  engine never pairs the new machine with the old SYN values, even before the SYN parameters have caught up. SYN values
  set after the machine (a host restoring automation in index order: machine comes first) go over the defaults. The
  machine's defaults live in a lock-free table (`m_synDefaults`, 8 x 7 bits per id) filled when an OS loads.
- **SYN parameters follow on the message thread:** `handleParameterChanges()` (a 30 Hz timer on the processor; called
  directly by `setMachine`, `kitToParameters` and `setStateInformation` when on the message thread) writes the engine's
  SYN values into the SYN parameters that differ (`setValueNotifyingHost`, so hosts and automation see them), renames
  them and calls `updateHostDisplay(parameterInfoChanged)`. A timer, not an AsyncUpdater: a machine automated by the host
  arrives on the audio thread, and `triggerAsyncUpdate` would post a message (a lock) from there; the timer polls an
  atomic mask. Machines set before any OS is loaded keep their SYN values pending (as 0003) until one loads.
- **getName** is cheap: SYN names are cached per track (`m_synNames`, a SpinLock around a String copy), the others are
  the parameters' own; measured ~20 ns per call over all 500.
- **Kit = canonical form:** the processor keeps the loaded `md::library::Kit` (name, slot, master effects, LFO state,
  trig/mute groups, the machine words' upper bytes). `loadKit(kit, bank)` stores it and calls `kitToParameters` (machine
  before the rest on each track); `parametersToKit()` is that kit with the parameters' values. A kit value outside a
  parameter's range (an LFO shape > 5, say) is kept while the parameter holds its clamped value, so an untouched kit
  exports byte for byte. The default kit (0003's TRX/EFM set, named `DEFAULT`) has a cleared MD kit's master effects
  (gate box 0 0 64 0 0 127 127 96, echo 32 0 32 0 0 127 0 96, EQ 64 x 7 + 127, dynamix 127 x 6 + 0 0, as the free slots
  of the user's file have them) and no trig/mute groups.
- **State schema 2** (XML): `<DrummoduleState schema="2" ownBusTracks="..">`, `<PARAMETERS>` with one `<PARAM id value>`
  per parameter (raw; tempo in BPM), `<KIT name bank position sysex="F000203C...F7">` = `encodeKit(parametersToKit())`
  in hex. Restore: the kit (non-parameter contents), then every track's parameters machine first (a parameter missing
  from PARAMETERS takes the kit's value), then the globals. **The OS path is not in the state** (as decided above; 0003
  wrote `firmwarePath`, which a schema 1 state still has but is no longer read: the OS is the shared setting). Schema 1
  restores `midiChannel`, `tempo`, `ownBusTracks`. Track outputs (`setTrackOutput`) stay a setting, not a parameter.
- **Note map:** `note_map` 0 = white keys (default), 1 = chromatic = 36-51 *and* 0-15 together, exactly 0003's map, so
  `kRenderHashOs163` still holds with it. Two options, not three: the two chromatic ranges do not overlap, and the 0003
  hash was recorded with both active. `params::trackForNote(note, map)` / `noteForTrack(track, map)` (OneParams.h) are the
  one place; the processor has `trackForNote(note)` / `noteForTrack(track)` for the current map.
- **Accent:** `accent` = HostModel's accent amount. With it above 0, a note at velocity 127 plays as an accented trig
  (VEL = 128 + 2 x accent, the OS's formula for the sequencer's accent). 0 (default) = no accents, so velocity 127 plays
  as before (the hashes hold). There is no other way to send an accent over MIDI here; revisit if the MD's own MIDI
  implementation says otherwise.
- `loadDefaultKit()` no longer resets the track outputs (routing is not part of a kit).

## Verification (2026-10-12, macOS 26.6 arm64, Apple clang 21, `-DMD_COPY_PLUGINS=OFF`)
- `build/src/cli/md-hash <OS 1.63>` -> `hash 04ef1db4fe767721` (engine untouched).
- `MD_OS_SYX=<OS> MD_KITS_SYX=<MD_presets_1_50.syx> ctest --test-dir build --output-on-failure`: 8/8 passed (romart,
  firmware, golden-hash, kits, resampler, plugin-os-load, plugin-render ~13 s, plugin-state ~13 s).
- `plugin-render`: chromatic map `a28b9dc178776939` (0003's hash unchanged with the map set explicitly), white keys
  `14291ff9302212d4` (same notes: tracks 1-9 play); the sequence moved to the white keys plays the chromatic render's
  samples; 38 -> track 2, 62 -> track 16, all 16 white keys = ffff, black keys / 0-15 / 35 / 63 silent; accent 64 at
  velocity 127: peak ratio 2.016 (256 / 127), velocity 126 unaccented.
- `plugin-state`: layout (500, ids in order and unique, groups, ranges, names, defaults, automatable, getName ~20 ns);
  machine set by the host: SYN parameters unchanged until the message thread runs, then reset to EFM-BD's defaults with
  SYN3 (set after the machine) kept, renamed (`T1 PTCH .. T1 MFB`), one parameter-info change and SYN1's new value sent to
  the host, SYN defaults = the machine's; a note right after the host's change (SYN parameters not yet updated) renders
  the same as `setMachine`; an edited session (machine set by the host, SYN, LFO page, LEV, accent, tempo 133.5, channel
  3, a track output) restored into a fresh processor: all 500 parameters equal, the same state bytes (20906), the same
  render; schema 2 contents, no OS path; schema 1 restores its settings. Kits: all 64 kits loaded and exported are the
  same dump byte for byte; kit 0 exported decodes to the same machines, parameters, levels, LFO pages; an edit shows in
  the export. **Kit 0 through the processor** (golden::Pattern's 224 triggers as notes on the white keys, 12 s, main +
  sends hashed as the kit test does): `da62047690565e38` (`kKit0ProcessorHashOs163`) = `applyKit` straight on the engine
  after 16 silent blocks; without them that render is `kKit0Hash` `f5c618b7df5550fa`; state saved, restored into a fresh
  processor, rendered again: `da62047690565e38`.
- **CPU** (`md-plugin-check --bench 20`, two runs, load average ~7 then ~3): engine alone 30.7 / 30.9 %; processor
  30.9 / 30.8 % (44.1 kHz), 31.1 / 31.2 % (48 kHz), 31.3 / 31.4 % (96 kHz), as 0003 (31.8 / 32.0 / 32.1 %). Callbacks
  after the first second, 99.9th percentile / max against the budget: 700-816 / 1016-1176 us of 1451 us (44.1 kHz),
  630-707 / 1191-1274 us of 1333 us (48 kHz), 352-363 / 400-409 us of 667 us (96 kHz); **none over budget** in either run.
  The max values above 0003's are scheduler noise (the engine alone, which reads no parameter, had a 1776 us block in
  the second run). Parameter application reads ~500 atomics per host block, no lock, no allocation.
- **AU, in-process** (auval only sees the installed copy, which is not this build): a scratch tool registered the
  worktree's `Drummodule.component` factory (`DrummoduleAUFactory`) under a private subtype (`aumu Mdx4 Drmd`) with
  `AudioComponentRegister`, a scratch `MD_SETTINGS_DIR` naming the OS: it initialises; ParameterList = 500, ParameterInfo
  for all in under 1 ms; 17 clumps; names `T1 MACHINE`, `T1 PTCH` .. `T1 STRT`, `T16 LEV`, `T16 LFO UPDTE`, `TEMPO`;
  values 0..1 (machine 16/191, LEV 100/127); machine set to 32 with `AudioUnitSetParameter` and the run loop pumped:
  `T1 STRT` became `T1 MOD`, SYN1 reset from 5 to 36, one ParameterList notification; ClassInfo restored into a second
  instance: 0 of 500 parameters differ, SYN names follow; note 36 renders (-7.8 dBFS).
- **Standalone: not verified here.** It was launched three times with a scratch `MD_SETTINGS_DIR` (ran 22 s, 14 s and a
  few seconds, no crash, RSS ~500-730 MB) before the coordinator stopped it: its window kept appearing on the user's
  screen. In the first two runs the scratch settings file was rewritten with an OS path in ~/Downloads and the engine
  booted a second time, which only the editor's OS chooser does (`setFirmwarePath(path, persist = true)`), so most
  likely someone at the screen picked the file; the third run, quit the same way (SIGTERM), booted once and left the
  file unchanged. Nothing in the processor writes that setting by itself.
- `md-plugin-check --snapshot` not re-run: no editor file changed.

## Remaining
- `auval -v aumu Mdo1 Drmd` on this build once it is installed (after the merge, or packaging in 0010).
- Listening test in the standalone or a host: the generic UI's knobs, automation of `machine` while playing (the SYN
  knobs following), a project saved and reopened.
- 0008: the editor's knobs on these parameters (below).
- Not parameters (by design, not forgotten): the track outputs (`setTrackOutput`, saved in the state), the kit's master
  effects / trig and mute groups (in the kit, for 0009 and later).

## Notes for 0008 (editor)
- **Knobs:** each cell binds to `p.parameter(track, slot)` (a `juce::AudioParameterInt&`; slot = `params::kMachine`,
  `params::kKit + HostModel index 0-23`, `params::kLevel`, `params::kLfo + 0-4`) with a `juce::SliderParameterAttachment`
  (or Monomodule's per-cell invisible slider bound the same way); the slider's range is the parameter's raw range
  (`getRange()`), so values are the raw 0-127 the hardware shows. Globals: `tempoParameter()`, `midiChannelParameter()`,
  `accentParameter()`, `noteMapParameter()`. Use `beginChangeGesture`/`endChangeGesture` (the attachment does).
- **Machine picker:** set `parameter(track, params::kMachine)` (attachment or `setMachine(track, id)`); the SYN page then
  reads the SYN parameters (reset to the machine's defaults when `setMachine` returns on the message thread, or at the next
  timer tick for a host's change). Labels: `synName(track, k)` (minus the `T<n> ` prefix) or the OS table
  (`machines()` / `machineTable()`); `machineName(id)`; `synDefault(id, k)`.
- **Live values:** poll the parameters in the editor's timer (`parameter(t, s).get()` is an atomic read) or listen
  (`AudioProcessorParameter::Listener`, called on the thread that set the value: hop to the message thread). Repaint the
  SYN labels on `audioProcessorChanged` with `parameterInfoChanged`, or on a machine value change. After-LFO values:
  `HostModel::voiceParams()` (not exposed yet; needs a lock-free copy from the audio thread).
- **Kits:** `md::library::loadSyxFile(path)` -> `Bank`; skip `Kit::isFree()`; `p.loadKit(bank.kits[i], bankName)` on the
  message thread; `kitName()` / `kitBank()` for the GLOBAL page; export with
  `md::library::encodeKit(p.parametersToKit())`. `loadDefaultKit()` for an "init" action.
- **Pads / keyboard:** `noteForTrack(track)` gives the note of the current map; `keyboardState().noteOn(1, note, vel)`.
