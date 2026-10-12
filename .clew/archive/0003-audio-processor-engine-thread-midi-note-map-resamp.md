---
id: 3
status: done
tags:
- engine
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T16:23:49Z
---
## Why
Port the plugin layer of `resources/mpc-vst-machinedrum/vst/engine.cpp` to JUCE without the MPC specifics, following
Monomodule's `src/plugin/one/OneProcessor.cpp` for the desktop parts.

## Decisions
- **Render synchronously in `processBlock`**, as Monomodule does (`MonoVoice::process` pulls 16-frame blocks on the audio
  thread). Not the MPC port's render-ahead thread + ring: that existed to ride out MPC OS stalls and costs 8.7 ms of latency.
  The interpreter renders 16 tracks at ~18 % of one core (increment 0001), so it fits in the callback. Warm up the engine
  after the OS loads (`warmUp`: render a few silent blocks) off the audio thread, and guard engine swaps with a
  `ScopedTryLock` as `MnmOneProcessor::processBlock` does.
- Start with the single-threaded `md::engine::EngineT<VoiceEngine>`. `ParallelVoiceEngine` (persistent threads, `sched_setaffinity`,
  `/proc/stat` core choice) is Linux/MPC-shaped; leave it out of the first version and revisit if measurements say so.
- Engine rate is 44.1 kHz (the MD's); host rate conversion reuses Monomodule's `src/core/dsp/Resampler.{h,cpp}` and the
  `m_engineAt` clock logic in `OneProcessor.cpp` (copy, attribute).
- MD note map: notes 36-51 -> tracks 1-16 (the hardware's), velocity -> `HostModel::trigger(track, vel)`. Also accept
  notes 0-15 (the MPC port's drum-pad layout) so both conventions work. MIDI channel: configurable, default omni.
- Voice budget off (`setMaxActiveVoices(16)` is the hardware behaviour), silence release off; these are MPC CPU measures.
- Tempo: `HostModel::setTempo` from `AudioPlayHead` BPM each block; fall back to a tempo parameter when the host gives none.
- Outputs: main stereo + 16 mono track buses + reverb-send stereo + delay-send stereo (`Mixer::Output` has them; the MPC
  "taps" existed only because MPC allows one output pair). A track routed to its own bus leaves the main mix (`dryMute`).

## Acceptance criteria
- [x] Notes 36-51 (and 0-15) trigger tracks 1-16 with velocity; host tempo drives LFO/E12 timing
- [x] Engine renders 32-sample blocks at 44.1 kHz inside `processBlock`; correct at 44.1, 48 and 96 kHz host rates
- [x] No Linux-only calls remain (`/proc/stat`, `sched_setaffinity`, `pthread_setaffinity_np`)
- [ ] All 16 tracks playing at a 64-sample host buffer: no dropouts in the standalone (measure CPU in Activity Monitor and note it)
  (measured headless instead, see Verification: no 64-sample callback over budget at 44.1/48/96 kHz; the listening test in
  the standalone with a MIDI keyboard remains, see Remaining)
- [x] Offline test: a fixed note sequence rendered through the processor at 44.1 kHz gives a stable hash (regression test next to md-hash's)

## Tasks
- [x] `MdOneProcessor`: engine ownership, OS load/warm-up, `prepareToPlay` latency, `processBlock` MIDI -> `HostModel` -> `EngineT::render`
- [x] Resampler port from Monomodule
- [x] Bus layout (main + 16 + 2 sends) and `isBusesLayoutSupported`
- [ ] Standalone plays from a MIDI keyboard / the on-screen keyboard (processor side done: MIDI input, `keyboardState()`;
  the on-screen keyboard component belongs to the editor, 0007/0008; not heard here, see Remaining)

## Decisions taken while implementing (2026-10-12)
- **Clock and conversion** as Monomodule's `MnmOneProcessor` (`processChunk` / `writeOutputs` / `m_engineAt`), minus the input
  side: with no audio input there is no input converter, so Monomodule's `m_inLag` is 0 and the engine never trails the host.
  Each chunk renders the engine frames up to its last sample; a 32-frame engine block is rendered when a frame is needed and
  carried over into the next chunk when only partly consumed. Latency = the output converter's `m_outLag` =
  ceil(36 x rate / 44100): 0 at 44.1 kHz, 40 samples at 48 kHz, 79 at 96 kHz (`setLatencySamples` in `prepareToPlay`).
- **Note timing:** a note at host sample s waits for engine frame ceil(s x 44100 / rate) and triggers at the first 32-frame
  engine block boundary at or after it (a 0.73 ms grid). At 44.1 kHz any host block size gives the same samples (checked with
  512 and 61).
- **Streams:** 22 engine-rate float streams (main L/R, reverb L/R, delay L/R, 16 track outputs), 24-bit int / 2^23 as the MPC
  port's (`>> 8` to 16 bit) and Monomodule's (`1 / 8388608`). At 44.1 kHz they are copied to the buses; otherwise each keeps a
  `dsp::History` and the converter runs only for streams that go to an enabled bus and had sound inside the filter window.
- **Track outputs:** a track set to its own bus (`setTrackOutput`) is added to `EngineT::dryMute` (out of the dry main mix, still
  in the sends) and its bus gets `Mixer::solo(track sample, VOL word)`, the hardware's individual-output formula the MPC port's
  taps used. Only while the host has that bus enabled: a track routed to a disabled bus stays in the main mix instead of going
  silent. Buses: "Main" (stereo, on), "Track 1".."Track 16" (mono, off by default; stereo accepted and gets the track on both
  sides), "Reverb send" / "Delay send" (stereo, off by default). AU hosts get every bus (the JUCE AU wrapper enables all; AUs
  have no disabled buses); the JUCE standalone plays the main bus only (`setPlayConfigDetails` disables the others).
- **The sound lives in the processor** as atomics any thread may set (`setMachine` on the message thread: it reads the OS's
  machine table for the SYN1-8 defaults), applied to `HostModel` at the start of each `processBlock`, only what changed. A new
  engine (OS load) gets the whole kit at its first block. Machines chosen before any OS is loaded take their SYN defaults when
  it loads (`synPending`). A fresh instance plays a **default kit** (TRX BD SD XT XC CP RS CB CH OH CY MA CL B2 S2, EFM HH CY;
  EQ flat, filter open, VOL 100, PAN centre, no sends, LFO depth 0, LEV 100) so notes sound before kits (0005) exist.
- **Tempo:** the play head's BPM, else the tempo setting (`setTempo`, default 120, saved in the state); passed on only when
  BPM x 24 (the OS's integer tempo word) changes.
- **MIDI:** note-ons only (note-offs ignored, a drum machine); `setMidiChannel` 0 = omni (default) or 1-16, saved in the
  state. The editor's notes (`keyboardState()`, a `juce::MidiKeyboardState`) bypass the channel filter.
- **Voice budget and silence release:** not called, so HostModel's defaults apply: budget `kTracks * 2` = 32 units (off: every
  track sounds even with 16 ROM machines at 2 units each; `setMaxActiveVoices(16)` would now cut voices once ROM machines are
  in use), silence release off.
- **State** (still schema 1): `firmwarePath` as before, plus `midiChannel`, `tempo`, `ownBusTracks` (bit mask). The kit is
  not saved yet (0004).
- **Resampler** in its own static library `mddsp` (`src/core/dsp/`), unchanged from Monomodule but for the namespace
  (`md::dsp`); Monomodule's `tests/test_resampler.cpp` runs as the `resampler` ctest (`md-dsp-tests`, no OS file needed).

## Findings
- **The hardware's default note map is not 36-51.** The manual's Appendix B ("DEFAULT MIDI MAPPINGS", page 113) maps tracks
  1-16 to the white keys from C2: 36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62 (and E4 upwards to patterns).
  This increment implements the decision as written (chromatic 36-51, plus 0-15); the two maps conflict (38 = track 3 here,
  track 2 on the MD), so following the hardware would mean replacing the chromatic map or making the map a setting.
  `MdOneProcessor::trackForNote` is the one place to change. Not re-opened here; for the coordinator.
- **CPU with all 16 tracks retriggered** (every 86 engine blocks, ~8 per beat at 120 BPM, the regression kit): the engine alone
  costs ~32 % of one M4 Max core, not the ~18 % of md-hash's sparser pattern; the processor path adds 0.1-0.4 points (copying,
  float conversion, the converter on the main bus). Worst single engine block after warm-up ~0.4-0.45 ms of its 0.73 ms (the
  control-tick blocks are the heavy ones), so a 64-sample buffer at 96 kHz (0.67 ms) has the least margin.
- The first renders after boot run slower (first touches of the engine's ~420 MB; macOS also tends to start a new process on
  the efficiency cores): the bench leaves out the first second for its worst-case figures.
- At 48 and 96 kHz the engine renders exactly what it renders at 44.1 kHz when notes land on the same engine block, and the
  output equals the 44.1 kHz render through the converter bit for bit. The converted output peaks +1.07 dB above the 44.1 kHz
  samples in the regression render: a band-limited (true) peak between them (4x oversampling reads +1.04 dB), not an error.
- Velocity: the MD's VOL gain is linear in velocity (127 vs 64: peak ratio 1.987).
- `HostModel::setRouting` (the DSP1 route word, the hardware's outputs A-F) would put a track into `Mixer::Output::frame`,
  which no bus plays here: kits and parameters (0004/0005) should keep HostModel's route at main (6) and express individual
  outputs with `setTrackOutput`.

## Verification (2026-10-12, macOS 26.6 arm64 M4 Max, Apple clang 21, `-DMD_COPY_PLUGINS=OFF`)
- `build/src/cli/md-hash <OS 1.63>` -> `hash 04ef1db4fe767721` (engine untouched).
- `MD_OS_SYX=<OS> ctest --test-dir build --output-on-failure`: 5/5 passed (firmware, golden-hash, resampler, plugin-os-load,
  plugin-render; ~16 s, plugin-render ~11 s). Without `MD_OS_SYX`: firmware and resampler pass, the other three Skipped.
- **Regression hash** (`md-plugin-check --check-render`, `kRenderHashOs163` in `src/plugin/Check.cpp`): `a28b9dc178776939`
  = the main output at 44.1 kHz of 32 steps (4.49 s) over all 16 tracks, two notes per step on channel 10 (both note maps,
  varied velocities) plus a channel-1 decoy per step, the test kit (TRX, EFM, E12 and P-I machines, an LFO, E12 retrigs,
  filter, SRR, DIST, pans, sends), host tempo 120. Same samples with 61-sample host blocks. Also checked there: 48/96 kHz
  latency 40/79 and max difference 0 against the offline conversion; energy -0.0014 dB, end within 0.02 ms; track 1 on its
  own bus = the main mix with its VOL at 0, its bus plays, the other track buses silent, the sends carry signal; a track
  routed to a disabled bus stays in the main mix; host tempo 140 changes the output and the tempo setting without a play head
  gives the same; channel filter and omni, notes 36/0 and 51/15 identical, 16-35 and 52+ silent, velocity, the editor's
  keyboard; `takeTriggeredTracks()` = ffff.
- **CPU** (`md-plugin-check --bench 20`, all 16 tracks every ~1/16 s, 64-sample host blocks, load average ~6 with Ableton Live
  running): engine alone 31.7 % of one core; processor 31.8 % at 44.1 kHz, 32.0 % at 48 kHz, 32.1 % at 96 kHz. Callbacks
  after the first second, 99.9th percentile / max against the budget: 649 / 709 us of 1451 us (44.1 kHz), 658 / 722 us of
  1333 us (48 kHz), 372 / 441 us of 667 us (96 kHz); none over budget. (A run during parallel builds, load average 19, showed
  scheduler stalls of up to 229 ms at 48 kHz: contention, not the code.)
- **Standalone** (`build/src/plugin/MdOne_artefacts/Release/Standalone/Drummodule.app`, `MD_SETTINGS_DIR` = a scratch folder
  whose `settings.xml` names the OS): ran 8 s without a crash, RSS ~484 MB, 7-13 % CPU idle (default audio device), quit with
  SIGTERM; settings file unchanged. No screen or MIDI access in this session: not played by ear.
- **AU:** `auval -v aumu Mdo1 Drmd` -> AU VALIDATION SUCCEEDED, but auval sees only the installed
  `~/Library/Audio/Plug-Ins/Components/Drummodule.component` (11:48, the pre-0003 build with one output bus); the worktree
  build was not installed over it. The worktree's `Drummodule.component` was exercised in-process instead (a scratch tool:
  `AudioComponentRegister` of its `DrummoduleAUFactory` under a private subtype): 19 output elements (Main 2 ch, Track 1-16
  1 ch, Reverb/Delay send 2 ch), 0 inputs; a track element accepts stereo and refuses 3 channels; initialises at 48 kHz with
  latency 40 samples; notes 36-51 on channel 10 sound on the main element; the other elements silent.
- `md-plugin-check --snapshot` still renders the editor (unconfigured / wrong-file / loaded).

## Remaining
- Listening test: the standalone at a 64-sample buffer with all 16 tracks played from a MIDI keyboard (Activity Monitor CPU).
- `auval -v aumu Mdo1 Drmd` on this build once it is installed (after the merge, or packaging in 0010).
- The note-map question above (the hardware's white-key map vs the decided chromatic 36-51).

## Notes for later increments
- **0004 (parameters/state):** the sound API is `setMachine(t, id)` (message thread; SYN1-8 <- the machine's defaults),
  `setTrackParam(t, p, v)` (HostModel order 0-23, raw 0-127), `setTrackLevel`, `setTrackOutput`, `setMidiChannel`,
  `setTempo`, with getters; `applyKit()` pushes what changed into HostModel at the start of every `processBlock`. Parameters can
  replace the `m_kit` atomics (e.g. APVTS raw values read in `applyKit`). A machine change overwrites SYN1-8 with the
  defaults, so a restore must set the machine before SYN (the MPC port's "touched" flags or Monomodule's per-machine shadows
  solve the automation case). The regression hash sets every parameter of its test kit through this API, so changing the
  default kit does not change it; changing when values reach the engine does. Tempo is a setting, not yet a host parameter.
- **0008 (editor pages):** `machines()` (the OS's table: id, name, SYN labels, defaults; message thread, empty without an OS),
  `keyboardState()` for an on-screen keyboard (plays on any channel setting), `takeTriggeredTracks()` for trigger LEDs,
  `trackForNote()` for pad labels, `currentBpm()`, `midiChannel()`, `tempo()`, `trackOutput()`, `conversionLatency()`, and
  `statusText()` (now with the host rate and latency).
- **0009 (master effects):** "Main" is the dry mix and the sends are raw on their own buses; once DSP1's effects exist, decide
  whether Main carries them and whether the send buses stay.
- The worst-case cost is the control-tick block (16 machine functions in the 68k emulator every 11th block): spreading that
  work would help tiny buffers at high rates, but changes timing (golden hash).
