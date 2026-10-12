---
id: 8
status: done
tags:
- ui
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T17:48:24Z
---
## Why
Turn the static LCD of increment 0007 into the instrument's editor: every page of every track editable with the mouse,
a machine picker, a global mixer page and a kit browser (the editor half of increment 0005). Depends on 0004 (the host
parameters the knobs bind to) and 0007 (the canvas).

## What 0007 left for this (see `.clew/archive/0007-*.md`)
- `KnobPage::setPage(params, values)` stands in for Monomodule's `bind()`. Add the per-cell invisible sliders and the
  value editor from `resources/monomodule/src/plugin/one/OneEditor.cpp`; `drawKnobCell` already takes a highlight flag.
- `OneView::kPreviewMachine`, `m_machineId`, `m_track` are placeholders for the machine parameter and the track
  selector; remove the PREVIEW badge with them.
- `MachineBar` has `m_open` and the arrow; it needs an `onOpen` callback for the picker. `spec::machineGroup` and
  `machineShortName` split the OS machine names, for picker columns by group (the MPC skin's `mk_skin.py`: columns are
  the OS machine groups, ROM split into columns of 16; our picker can be a Monomodule-style `MachinePicker` overlay).
- `kEffectsPage` and `kRoutingPage` tables exist. The LFO page needs new cell kinds (text and shape icons; see
  `cell_dynamic` in `resources/mpc-vst-machinedrum/tools/mdskin/mk_skin.py` and the MD's own LFO page: TRACK PARAM SHP1
  SHP2 / UPDTE SPEED DEPTH SHMIX).
- `OneView::resized()` lays everything out from `kLcdContentW`/`kLcdContentH`; growing those and calling `setSize`
  is picked up by the scaled holder.
- Artwork is shared process-wide; only the editor that requested the load repaints immediately.

## What 0004 left for this (see `.clew/archive/0004-*.md`, "Notes for 0008")
- Every knob is `MdOneProcessor::parameter(track, slot)` (`juce::AudioParameterInt`, raw range; slots in
  `src/plugin/one/OneParams.h`), globals `tempoParameter()` etc.: bind cells with `juce::SliderParameterAttachment`.
- The machine picker sets `parameter(t, params::kMachine)`; SYN1-8 reset and rename themselves (`synName`, `machineName`,
  `synDefault`); repaint on `parameterInfoChanged` or a machine value change. Kits: `loadKit(kit, bank)`, `kitName()`,
  `kitBank()`, export `encodeKit(parametersToKit())`. Pads: `noteForTrack(t)` (white keys by default).
- Nothing in `OneEditor.*` was changed by 0004; `OneView::kPreviewMachine` and `m_track` still stand in.

## Decisions
- Layout: Monomodule's 2x2 per track (SYN | AMP/EFX over ROUTE | LFO), as the MPC skin did, plus a 16-track strip
  (track number + machine short name, click to select; MIDI-triggered tracks flash) and a GLOBAL tab with the 16 LEV
  faders, tempo and the kit browser. Keep the LCD look for all of it (no native widgets inside the LCD).
- Knob interaction exactly like Monomodule: vertical drag, mouse wheel, double-click the value box to type, shift for
  fine; each cell is a `juce::Slider` attachment to the 0004 parameter so automation and the UI stay in sync.
- Kit browser: `md::library::loadSyxFile` on every `.syx` in the user's kit folder
  (`~/Library/Application Support/Moduledrum/Machinedrum/kits/`, create it, rescan on a timer when mtimes change) and
  any file dropped on the editor; free slots (`Kit::isFree()`) hidden; selecting a kit calls the processor's kit load
  (0004's `kitToParameters`). Bank = file; show "bank / kit" like the MPC port's GLOBAL page.
- Machine picker: overlay listing the OS machine table grouped (GND, TRX, EFM, E12, P-I, INP, MID, CTR, ROM, RAM);
  machines the engine cannot play here (INP, MID, CTR, RAM, and ROM without sample data) shown greyed.

## Acceptance criteria
- [x] Track strip selects any of the 16 tracks; the four pages show that track's values and update live when a parameter
  changes from the host or automation
- [x] SYN labels follow the machine; AMP/EFX, ROUTE and LFO pages complete, with the LFO destination shown by name
- [x] Machine picker sets the `machine` parameter; the SYN page relabels and shows the machine's defaults
- [x] Knob drag / wheel / typed value edit; shift = fine; values are the raw 0-127 the hardware shows
- [x] GLOBAL: 16 LEV faders, tempo, kit browser with bank/kit names, drag-and-drop of a `.syx`
- [x] `md-plugin-check --snapshot` renders every page and the picker; the PNGs reviewed by eye at 100 % and 200 %
- [x] Nothing firmware-derived committed

## Decisions taken while implementing (2026-10-12)
- **Layout = the MPC skin's** (the user's reference screenshot, `resources/mpc-vst-machinedrum/docs/img/machinemodule.png`):
  the header has the machine block top left and MODULEDRUM top right in the LCD's bold face (on the LCD now, no longer
  printed on the chassis), the loaded kit (bank over name, dotted frame) just left of the name; the four pages in the
  2x2 with the skin's wide gap between the columns (28 LCD px; rows 7 apart); along the bottom, where the MPC has its tab
  bar, the strip: GLOBAL first, then tracks 1-16 (number in bold, machine short name in tiny). LCD content 286x208 LCD
  px, the editor's base size 948x782 (was 477x461); `ScaledHolder` keeps the window scale. 0007's TRACK box is gone (the
  strip says which track is selected); its pad role moved to the strip: a press on the selected track's key plays the
  track (the note of the current map, velocity 100, while held), so the standalone can be heard without a MIDI keyboard.
- **Cells:** `KnobCell` (Monomodule's invisible slider) with a `juce::SliderParameterAttachment` to the 0004 parameter;
  `KnobPage::bind(k, parameter, descriptor, textFn)` replaces 0007's `setPage`; `describe()` relabels without rebinding
  (SYN after a machine change) and resets the double-click value to the parameter's default (for SYN1-8 the machine's).
  Interaction as Monomodule: a press anywhere in the cell drags (up or right turns it up, 160 px for the whole range), one
  gesture per drag; a clean click on the value opens the inline editor (dials) or a value list (named values); a
  double-click on the dial resets; the wheel is the slider's own. Added (Monomodule has none): **shift = 8x finer** drags
  and one step per wheel notch; the drag accumulates per event, so shift can change mid-drag without a jump. Typed:
  "+3" / "-19" for bipolar cells, clamped to the range; Escape drops it. The inline editor is the value box inverted (ink
  background, paper text, an 18 px bold system font: the LCD faces cannot be edited in). Values are raw (bipolar signed:
  PAN -1, EQG +20). A value-list pick and a typed value are one gesture each.
- **New cell kinds** (`spec::Display`): `Text` (the value as a word where the dial is: LFO TRACK, PARAM, UPDTE, CHAN,
  NOTES) and `Shape` (the six LFO shapes drawn in code after mk_skin.py's `draw_shape`; TRI SAW SQR RAMP EXP RAND in the
  value list). LFO TRACK shows the destination's number and its machine's short name ("01-BD" as in the reference; it
  follows that track's machine), PARAM the destination parameter's label (the SYN labels of the destination track's
  machine, "-" for an unused knob; the fixed names from AMD on). SPEED, DEPTH and SHMIX are ROUTE's LFOS, LFOD and LFOM
  (two cells on one parameter). A page's dynamic names are re-read by the editor's timer (`refreshText`).
- **Machine picker** (`MachinePicker.{h,cpp}`, Monomodule's overlay and unrolling): unrolls from under the header over the
  pages and the strip; one column per OS group in id order, ROM's 48 machines in three columns under one header (12
  columns, short names in bold, 16 rows of 10 LCD px); the current machine inverted; a footer names the machine under the
  mouse with its SYN labels, or why it is grey. **Greyed = not pickable** (INP, MID, CTR, RAM, ROM; drawn as a half-tone
  checkerboard, `LcdCanvas::shade`); a kit's ROM machine still shows as current, and the SYN page then has a SILENT badge
  (0007's PREVIEW badge and `kPreviewMachine` are gone). A pick is `setValueAsCompleteGesture` on the machine parameter,
  then `handleParameterChanges()`, so the SYN parameters, their names and the page follow at once (a host's change
  follows at the processor's 30 Hz tick).
- **GLOBAL tab:** MIXER (the 16 LEV faders across both columns in Monomodule's LEV-bar style: number, machine, dotted
  frame filled to the value, the raw value; each a `KnobCell`), GLOBAL (TEMPO as a dial over 30-300 with its BPM as text,
  a drag snapping to whole BPM and with shift to tenths, typed with decimals; CHAN OMNI/1-16; NOTES WHITE/CHROM; ACCNT;
  INIT KIT = `loadDefaultKit`; EXPORT = a save chooser in the kit folder writing `encodeKit(parametersToKit())`), KITS.
- **Kit browser** (`KitLibrary.{h,cpp}` model, `KitBrowser.{h,cpp}` page): the folder is `sharedKitFolder()`, the
  settings folder's `kits/` (so `MD_SETTINGS_DIR` moves it), created when an editor opens. Banks = the `.syx` files with
  at least one used kit, by file name; an OS file or a pattern dump in the folder is ignored. Rescan: the editor's timer
  compares names, sizes and modification times every 1.5 s (a directory listing) and re-reads the files when they differ,
  on the message thread (a 436 KB dump reads in milliseconds). The page: banks left, the browsed bank's used kits right in
  two columns of eight (the wheel scrolls a column at a time; "17-32/64" in the title bar when there are more), the loaded
  kit inverted (by bank and name, or by index when loaded from here), FOLDER reveals the folder, an inverted footer bar
  names the loaded kit / bank or, for 4 s, what a drop did. A click loads (`loadKit(kit, bank name)`). **Drop:** a `.syx`
  dropped anywhere on the editor is copied into the folder (`<name> 2.syx` on a name clash, nothing when the same
  contents are there already; refused with a message when it holds no kit), then the GLOBAL tab shows its bank. A dotted
  ink frame marks the glass while a file hovers.
- **Editor timer** 30 Hz (was 15): polls the 16 machine parameters (atomics: strip and mixer names, SYN relabel),
  `takeTriggeredTracks()` (a flash lasts 4 ticks; bits gathered before an editor opened are dropped), the LFO names, the
  kit name and bank (the kit lock, which the audio thread never takes), the kit folder every 45 ticks, the status twice
  a second.
- **Processor change (no audio or state change):** `statusText()` no longer takes `m_engineLock`. `processBlock`
  try-locks it and *skips the host block* when it cannot, so 0007's editor, polling the status at 15 Hz under that lock,
  could drop audio blocks. `m_status` and `m_engine` change on the message thread only; the fault string is read only once
  the audio thread has cleared `m_engineReady`; the conversion rate and latency for the status are published by
  `prepareClock` in two new atomics. `plugin-editor` shows it: with the lock put back, 2 of 4000 host blocks were skipped
  while the timer ran; without it none (three runs, ~108k timer ticks each).
- Host changes reach the knobs through JUCE's `ParameterAttachment` (synchronous on the message thread, an async update
  from other threads, as in every JUCE plugin with attachments).
- `md-plugin-check --check-editor` (ctest `plugin-editor`) drives the editor headlessly with synthetic mouse events on
  the cells (drag, shift drag, wheel, a click on the value, double-click), the picker, the strip and the kit page;
  `--snapshot` renders the states listed below.

## Verification (2026-10-12, macOS 26.6 arm64, `-DMD_COPY_PLUGINS=OFF`)
- Build: no warnings in the plugin code (`juce_recommended_warning_flags`).
- `build/src/cli/md-hash <OS 1.63>` -> `hash 04ef1db4fe767721`.
- `MD_OS_SYX=<OS> MD_KITS_SYX=<MD_presets_1_50.syx> ctest --test-dir build --output-on-failure`: 9/9 passed (romart,
  firmware, golden-hash, kits, resampler, plugin-os-load, plugin-render, plugin-state, plugin-editor); the regression
  hashes unchanged.
- `plugin-editor` (35 checks): no OS file (the SYN page's message, nothing to pick, the knobs still set parameters, the kit
  folder created beside the settings); track 3 binds all 32 cells; a host change moves a knob; drag 20 px up +16, shift
  40 px +4, 10 px right +8, far down stops at 0, one gesture per drag; wheel +5, shift +1; a click on the value -> typed
  99, 200 -> 127, EQG -10 -> raw 54, Escape drops; double-click -> the default; the picker refuses ROM/MID/INP/RAM, EFM-SD
  picked in one gesture relabels SYN (PTCH DEC NOIS NDEC MOD MFRQ MDEC HPF) at the machine's defaults, a host's E12-SD
  relabels it, a ROM machine shows SILENT; LFO TRACK 05-BD, PARAM RICH -> HAMR when track 5 becomes P-I BD, then VOL,
  UPDTE TRIG; the selected key pressed again plays the track and its key flashes; MIXER T6 100 -> 84; TEMPO typed 133.5,
  dragged to 149, with shift 150.9; CHAN OMNI/10, NOTES CHROM; the timer beside the audio thread skips no block; the OS
  file dropped is refused; the user's snapshot dropped is copied in and lists its 16 used kits (TRX .. FUGLESANG); a click
  on kit 4 loads FRANSISCO (E12BD EFMSD E12BC E12BC TRXXC EFMCP P-IRS TRXCB E12OH E12OH EFMHH E12CC EFMBD GNDSN GNDSN
  GNDSN), the strip and the header follow; dropped again it is not copied twice; EXPORT decodes to the same kit and the
  folder rescans (2 banks); an unchanged folder is not re-read; a rewritten file (same size, new mtime) is; a click on a
  bank browses it; every page renders.
- `md-plugin-check --bench 20` (load average ~4: another agent's md-kits at 100 %, Live open): engine alone 31.8 %;
  processor 31.7 / 32.1 / 32.3 % at 44.1 / 48 / 96 kHz; 99.9th percentile / max 643 / 691, 644 / 708, 372 / 592 us of
  1451 / 1333 / 667 us; **none over budget**. (A first run at load ~8, the other agent's md-tests at 100 %, had 1-4 of
  13-28k callbacks over and an 82 ms stall in the engine-alone reference, which reads no parameter: preemption. The bench
  has no editor in it; the editor-beside-audio check is in `plugin-editor`.)
- Snapshots (`--snapshot <scratch> --fw <OS> --kits <file>`), all looked at: track, picker, edit, global and kit-track at
  100 % and 200 %, global-empty, unconfigured, wrong-file, track-50. The track view follows the reference's layout (the
  kit-4 view shows exactly its LFO page: TRACK 01-BD, PARAM PTCH, the exponential shapes). Fixed after looking: the kit
  list's text sat a row low and its footer touched the page edge (now centred rows and an inverted footer bar); the inline
  editor's 12 px font was too small for its box (now 18 px bold); the stand-in face's two-digit track numbers overflowed
  their keys (the small face when the bold one does not fit). 200 % is crisp (6 px per LCD px); 50 % legible.
- Standalone with a scratch `MD_SETTINGS_DIR` (the OS configured, the user's kit file in its kit folder): ran 11 s at ~2 %
  CPU idle, RSS ~500 MB, quit on SIGTERM; the settings file unchanged. The window itself was not looked at (no screen
  capture in this session); the snapshots stand in.
- No PNG, JSON or other firmware-derived file added; the new code has no byte tables.

## For a human, by playing it
- Turning knobs in a host while it plays (the drag feel, shift, the wheel on a mouse and on a trackpad), the inline
  editor's keyboard focus in a DAW's plugin window, where the value-list popups open at other window scales.
- Automating a machine and its SYN parameters in a host with the editor open (knobs and labels following).
- Dropping `.syx` files from the Finder on the window; FOLDER opening the Finder; EXPORT's save dialog; INIT KIT.
- Whether greyed machines should stay unpickable (a kit can still hold them).
