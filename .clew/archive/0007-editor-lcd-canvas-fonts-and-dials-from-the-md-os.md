---
id: 7
status: done
tags:
- ui
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T16:14:53Z
---
## Why
The Monomodule-style UI: a pixel-exact LCD canvas drawn with the hardware's own fonts and dial art, read from the user's
OS file at run time. Two existing pieces do most of it:
- `resources/mpc-vst-machinedrum/tools/mdskin/mdartdump.cpp` knows **where MD OS 1.63 keeps the six bitmap fonts, the dial
  ring and dot** (addresses relative to `kBase = 0x200000`, the ColdFire section) and dumps them to JSON at build time. Port it
  to a runtime reader (`src/plugin/MdRomArt.{h,cpp}`) that fills the same `Art` struct Monomodule uses.
- `resources/monomodule/src/plugin/one/RomArt.{h,cpp}` (the `Art`/`Font`/`Bitmap` structs, the install-at-OS-load flow and the
  built-in stand-in face) and `Lcd.{h,cpp}` + `SpecData.{h,cpp}` (the 1-bit canvas, `drawKnobCell`, text, dotted rules,
  nearest-neighbour upscale by `kScale = 3`). Copy these into `src/plugin/` with attribution and strip the Monomachine-only
  icon families (`iconFmRatio`, `iconSidWave`, ...).
- `resources/mpc-vst-machinedrum/tools/mdskin/mk_skin.py` documents the MD page layout decisions already made for the MPC
  skin (per-track 2x2: SYN | AMP/EFX over ROUTE | LFO; a GLOBAL tab; picker columns = machine groups; the MD LCD colours
  `ink 5e0c0c / paper ff4836`). Reuse the decisions, not the PNG pipeline.
- Window scaling: `resources/monomodule/src/plugin/{ScaledEditor.h,WindowScale.h,SharedSettings.h}`.

The MD OS lacks the LCD's OFF/ON toggle icon (mdartdump draws a stand-in); draw ours in code.

## Acceptance criteria
- [x] With an OS loaded, the canvas renders the six fonts and the dial from the OS (compare against a photo / the MPC skin screenshot in the MD README)
- [x] Without an OS, or with another OS version (validate the image before trusting addresses), a built-in stand-in face is used
- [x] Nothing firmware-derived is committed (no font bitmaps, no art.json); `git grep` for OS byte tables stays clean
- [x] Editor resizable 50-200 % with a locked aspect ratio; the size is a shared setting
- [x] A `UiSnapshot`-style offline render of the canvas to PNG for review (Monomodule has `src/plugin/UiSnapshot.cpp`)

## Tasks
- [x] `src/plugin/MdRomArt.{h,cpp}` + `StandInFont.h`: runtime reader of OS 1.63's artwork into Monomodule's `Art` struct,
  image validation, stand-in face, drawn OFF/ON toggle
- [x] `src/plugin/one/Lcd.{h,cpp}`, `SpecData.{h,cpp}`: ported, Monomachine parts dropped
- [x] `src/plugin/Skin.h` (MD LCD colours by default), `WindowScale.h`, `ScaledEditor.h`; `SharedSettings.h` documents `uiScale`
- [x] `OneEditor`: the LCD (track box, machine bar, SYN page) in a chassis with the OS picker and status; corner grip
- [x] `md-plugin-check --snapshot`: base size with and without the OS, 50 % and 200 %, resize checks
- [x] `tests/test_romart.cpp` (`md-ui-tests`, ctest `romart`)

## Decisions taken while implementing (2026-10-12)
- **Art struct** (`md::uispec::Art`): `bold8 small4x5 tiny3x5 square5x5 digitsTop digitsBottom`, `dialRing` (11x13),
  `ringPlain` (11x11), `groupTie` (7x3) and `dialDot[128]` (7x7) are read from the OS; `iconToggle[2]` (17x13, mdartdump's
  ring-and-pointer drawing) is always drawn in code. Removed from Monomodule's struct: every list-icon family (`iconLfoDest`,
  `iconFmRatio`, `iconEnsPitch`, `iconFmDynFrq`, `iconSidWave`, `iconDproSync`, `iconDproWave`, `iconVoCons`, `iconDdrwWave`,
  `iconLfoPage`, `iconLfoWave`) and the machine-group logos (`groupLogo`, `groupLogoWords`: the MD OS has none, the machine
  bar sets the group in type). `litBounds` kept.
- **Validation before trusting addresses:** section 0 must be exactly OS 1.63's (404766 bytes, FNV-1a 64 `63e6be2538e4eb54`);
  any other image (another version, a patched OS, a non-OS file) keeps the stand-in. Then every descriptor is bounds- and
  shape-checked as in Monomodule (font height/advance, icon w/h/n/mask, glyph widths, >= 10 glyphs per font). A failed
  install never replaces artwork already in use (a wrong file after the OS file keeps the OS faces), as in Monomodule.
- `MdRomArt` is JUCE-free and reads the OS file itself (`loadMainOs` = section 0 of `parseContainer(parseSysex(readFile()))`;
  `ensureRomArt` caches by path, size and mtime), so the editor needs nothing from the processor for the artwork and the test
  links it without JUCE (`md-ui-tests`, its own executable because the artwork is process-wide state).
- The stand-in face is Monomodule's hand-drawn 3x5 caps face and generated dials, moved into `StandInFont.h` so the "nothing
  firmware-derived" audit has one obvious file of glyph data, all of it drawn for Monomodule.
- **Editor structure:** `MdOneProcessor::createEditor` still returns `new OneEditor(*this)`; `OneEditor` is now a
  `scale::ScaledEditor` that shows `OneView` (the content at its base size, 477x461) scaled, so `OneProcessor.cpp` (increment
  0003's file) is unchanged. `OneView` = the chassis (aluminium, bezel, LCD glass with a darkened edge, OS picker, status,
  the product name printed in the LCD bold face, corner grip) and on the glass `TrackBox` (TRACK over the number in the tall
  digit face; no leading zero, the tall face's 0 is slashed), `MachineBar` (inverted block: group, name, picker arrow) and
  one `KnobPage` ("SYN", 2x4 cells, Monomodule's geometry: `kCell` 32, title bar 10 rows, page 129x76 LCD px).
- **What the SYN page shows:** labels and defaults from the OS's machine table (`MachineRunner::machines()` of the loaded
  engine, through a new read-only accessor `MdOneProcessor::machineTable()`, one line in `OneProcessor.h`). The engine's
  tracks all hold GND-- (no labels) until the tracks get machine parameters (0004), so track 1 previews TRX-BD (the MD's BD
  track) at its OS defaults, with a PREVIEW badge in the title bar. Without an engine the page frame shows "No OS file ..."
  or "The OS file did not load ..." in the small face.
- **Skin:** Monomodule's `Skin.h` with the presets MACHINEDRUM (ink 5E0C0C on paper FF4836, the default), INVERTED, BLACK ON
  WHITE, CUSTOM; shared settings `skin`, `skinInk`, `skinPaper` (no UI to choose one yet).
- **Window scale:** Monomodule's `WindowScale.h` / `ScaledEditor.h`, same behaviour: 0.5-2.0 x the base size, aspect locked by
  the constrainer, shared setting `uiScale` written 400 ms after a resize settles (or when the editor closes); a new editor
  opens at it, or smaller where the screen is too small. The corner grip resizes through the constrainer (AUv2 hosts).
- **Snapshot:** `md-plugin-check --snapshot <dir>` writes unconfigured / wrong-file (stand-in) and loaded (OS faces) at the
  base size (3 screen px per LCD px), then `<last>-50.png` and `<last>-200.png`, and checks the constrainer (300 % held at
  200 %, 25 % at 50 %, a stretched size keeps the ratio) and that the last size became `uiScale`. PNGs are never committed.

## Verification (2026-10-12, macOS 26.6 arm64)
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMD_COPY_PLUGINS=OFF && cmake --build build`: builds; no
  warnings in the plugin code under `juce_recommended_warning_flags`.
- `MD_OS_SYX=<OS 1.63> ctest --test-dir build --output-on-failure`: 4/4 passed (romart, firmware, golden-hash,
  plugin-os-load). Without `MD_OS_SYX`: romart passes its OS-free case; golden-hash and plugin-os-load are skipped.
- `build/src/cli/md-hash <OS 1.63>` -> `hash 04ef1db4fe767721` (unchanged).
- `md-plugin-check --snapshot <scratch> --fw <OS>`: all checks OK. The PNGs were reviewed: with the OS the header, title,
  labels and values are the MD's own bold-8 and tiny-3x5 faces and the dials its 11x13 ring and pointer frames, matching the
  MPC skin screenshot (`resources/mpc-vst-machinedrum/docs/img/machinemodule.png`); crisp at 100 % and 200 % (3 and 6 px per
  LCD px); 50 % (1.5 px) is nearest-neighbour, legible but uneven, as in Monomodule. Without the OS: the stand-in face.
- Standalone with `MD_SETTINGS_DIR` naming the OS: ran 8 s (RSS ~506 MB, ~6 % CPU idle) and quit on SIGTERM. The window
  itself could not be looked at (no screen-recording / accessibility permission in this session); the snapshots stand in.
- `git grep --untracked` for hex byte tables and `art.json` under `src` and `tests`, and for untracked PNG/JSON: nothing.

## Notes for increment 0008 (pages, track select, machine picker, knobs)
- `KnobPage::setPage(params, values)` is the static stand-in for Monomodule's `KnobPage::bind(params8, paramId)`: add the
  `KnobCell`s (an invisible `juce::Slider` per cell, `onValueClick` on the value box) and the inline editor / value list from
  Monomodule's `OneEditor.cpp`; `drawKnobCell(..., highlightValue)` already takes the hover/drag flag.
- `spec::synParams(MachineInfo*, Param[8])` builds the SYN page from the OS table (labels are copied into `Param::label`, so
  a `Param` outlives the engine); `spec::kEffectsPage` / `kRoutingPage` hold the EFFECTS and ROUTING pages; the LFO page
  (TRACK PARAM SHP1 SHP2 UPDTE SPEED DEPTH SHMIX) needs new cell kinds (text, shape icon: mk_skin.py `cell_dynamic`).
- `OneView::kPreviewMachine`, `m_machineId` and `m_track` are the placeholders for the machine parameter (0004) and the
  track selector; drop the PREVIEW badge with them. `MachineBar` has `m_open` and the arrow for the picker; give it
  `onOpen`. `spec::machineGroup` / `machineShortName` split the OS names; picker columns = groups (mk_skin).
- Layout: `OneView::resized()` places everything from `kLcdContentW/H`; for the 2x2 pages (SYN | EFFECTS over ROUTING | LFO)
  grow them and the view's `setSize`: `ScaledHolder` picks up the new base size and keeps the scale.
- The artwork is process-wide: after `loadLcdArt` succeeds only the editor that asked repaints; other open editors repaint on
  their next change.
