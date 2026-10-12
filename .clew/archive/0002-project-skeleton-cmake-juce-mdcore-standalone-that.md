---
id: 2
status: done
tags:
- build
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T15:49:45Z
---
## Why
A buildable home for the plugin, modelled on Monomodule's CMake (JUCE 8.0.9 fetched at configure time). Everything after
this increment is porting work on top of it.

## Decisions (made 2026-10-12, do not re-litigate)
- **Vendor the engine.** Copy `resources/mpc-vst-machinedrum/engine/*` and `tools/mdfw/Firmware.{h,cpp}` into `src/core/md/`
  (keep the `md::engine` / `md::fw` namespaces and file names so diffs against upstream stay readable). Record the upstream
  commit (`6ea7a4a`) in `THIRD_PARTY_NOTICES.md`. Reason: we will modify it (desktop defaults, later the master effects), and
  it is ~3k lines.
- **Emulators as submodules under `ext/`:** `ext/dsp56300` = https://github.com/sd88me/dsp56300.git @ `26558e5401fa134a013eca8dbb8c5ecde837ec55`
  (init its `source/asmjit` submodule too); `ext/mc68k` = https://github.com/joelanders/mc68k-md-mm.git @ `ace95b3d0a5a332db147244762dda65f9a010b9f`.
  Do not pull in gearmulator-md-mm. Build only the static libs, as `resources/mpc-vst-machinedrum/cmake/dsp56300.cmake` does
  (asmjit, dsp56kBase, dsp56kEmu, mc68k; no vtuneSdk on macOS).
- **Interpreter by default.** Define `DSP56K_NO_JIT_RUNTIME` on the emulator targets *and* on `mdcore` and everything that
  includes `dsp.h` (`g_useJIT` is an inline constant). A CMake option `MD_USE_JIT` (default OFF) drops the define; see
  increment 0011 for why the JIT is off.
- `resources/` are git submodules for reference only; nothing in the build depends on them.
- Layout mirrors Monomodule: `src/core` (engine + firmware + tests' golden data), `src/plugin` (JUCE), `src/cli` (`md-hash`,
  `md-machines`), `tests/`. Plugin naming: product "Machinemodule" is taken by the MPC port; use a distinct name, e.g.
  **"Drummodule"** (processor class `MdOneProcessor`), bundle id `md.elektron-md.one`, 4-char codes to be chosen.

## Acceptance criteria
- [x] `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` builds AU, VST3 and Standalone on macOS arm64
- [x] `build/src/cli/md-hash <OS.syx>` prints `hash 04ef1db4fe767721` (the baseline from increment 0001) and `ctest` has a test
  that checks it when `MD_OS_SYX` is set (skipped otherwise), as Monomodule's tests do with `MNM_OS_SYX`
- [x] Standalone shows a "Select OS File..." button, stores the path in a shared settings file
  (`~/Library/Application Support/<Company>/Machinedrum/`), parses the OS (`md::fw::parseContainer(parseSysex(readFile()))`),
  boots the engine and renders silence; a wrong file reports an error and keeps running
- [x] `THIRD_PARTY_NOTICES.md` and `LICENSE` (AGPL-3.0) present

## Tasks
- [x] `ext/` submodules, `cmake/dsp56300.cmake`, `cmake/mc68k.cmake`, `cmake/juce.cmake` (FetchContent JUCE 8.0.9, GIT_SHALLOW)
- [x] `src/core/md/` vendored engine + `CMakeLists.txt` -> `mdcore` static lib
- [x] `src/cli/md_hash.cpp` (port of `tools/mdhash/mdhash.cpp`), `src/cli/md_machines.cpp` (port of `tools/mdmachine`)
- [x] `tests/`: golden-hash test; Monomodule's `tests/mini_test.h` is a fine single-header harness to copy
- [x] `src/plugin/`: `juce_add_plugin` (AU VST3 Standalone, IS_SYNTH, NEEDS_MIDI_INPUT, hardened-runtime JIT entitlements as
  Monomodule sets them), minimal processor + editor with the OS picker
- [x] README with the build steps and where to get OS 1.63

## Decisions taken while implementing (2026-10-12)
- **Names.** Product and company "Drummodule" (Monomodule uses its author's handle as company; this project has no brand yet,
  so the product name stands in. Changing it later moves the settings folder and the AU manufacturer name). CMake target
  `MdOne`, namespace `md::plugin::one`, classes `MdOneProcessor` / `OneEditor` (files `src/plugin/one/OneProcessor.*`,
  `OneEditor.*`, `OneFactory.cpp`, as Monomodule). Bundle id `md.elektron-md.one`, `PLUGIN_MANUFACTURER_CODE Drmd`,
  `PLUGIN_CODE Mdo1` (AU `aumu Mdo1 Drmd`), VST3 categories `Instrument Drum`.
- **Shared settings:** `~/Library/Application Support/Drummodule/Machinedrum/settings.xml`, root tag `MachinedrumSettings`,
  attribute `osPath`; `MD_SETTINGS_DIR` overrides the folder (as Monomodule's `MNM_SETTINGS_DIR`). A failed load never
  overwrites it; session restore never writes it.
- **`MD_OS_SYX`** is both a CMake cache path (compiled into the tests and CLI tools as their default) and an environment
  variable that overrides it at run time (Monomodule splits these into `MNM_OS_SYX` / `MNM_OS`; one name is simpler).
- **Vendored engine:** byte-identical to upstream `6ea7a4a` except the two `#include "../tools/mdfw/Firmware.h"` lines
  (`VoiceEngine.h`, `TrackFx.h`) -> `"Firmware.h"`. `Engine.cpp` (upstream's empty stub) is copied but not built, as upstream.
- **Golden pattern shared:** md-hash's pattern + FNV hash moved into `src/core/golden/GoldenPattern.h` (`md::golden::Pattern`,
  `render()`, `kHashOs163`); `md-hash` and the test both use it, so they cannot drift. md-hash keeps all its env modes
  (`MD_GROUPS`, `MD_SWEEP`, `MD_BUDGET`, `MD_RELEASE`, `MD_RAW`, `MD_T1`, ROM samples arg).
- **`DSP56K_NO_JIT_RUNTIME`** is a PUBLIC definition on `dsp56kBase` (so dsp56kEmu, mdcore and every consumer get it) and,
  for readers, on `mdcore`. `MD_USE_JIT=ON` drops both.
- **LTO on mdcore + dsp56kEmu + dsp56kBase + 68kEmu** (the MPC port sets IPO globally): md-hash 2.13 s user vs 2.40 s
  without, equal to the 0001 reference build (2.11 s). Hash unchanged. asmjit left out (its CMake ignores the property).
- **Warnings:** the emulators are built with `-w`; their include dirs and the vendored `src/core/md` are SYSTEM includes for
  consumers, so the plugin's `juce_recommended_warning_flags` (`-Wsign-conversion`) do not fire on upstream code. Our own
  plugin code builds warning-free under those flags. mc68k's own unit tests are kept out (`BUILD_TESTING OFF` around its
  `add_subdirectory`; it includes CTest). Dropped `JUCE_DISPLAY_SPLASH_SCREEN=0` (JUCE 8 warns it is ignored).
- **Processor skeleton:** `loadEngine()` (message thread) = `fw::loadFirmware` (validates: 3+ sections, checksums) +
  `parseContainer(parseSysex(readFile()))` for section 0, `md::engine::Engine` construction (boots the voice DSP), then 16
  warm-up blocks that must be all-zero; any exception -> "OS file load failed: ...", plugin keeps running without an engine.
  `processBlock` clears the output and, under `ScopedTryLock`, renders the engine in 32-frame blocks at 44.1 kHz in step with
  the host clock (fractional accumulator), discarding the output. Stereo output bus only, no input.
- **Tests (ctest):** `firmware` (wrong-file decoding without an OS; OS 1.63 sections + 135 machines with it), `golden-hash`
  (1 and 2 voice groups), `plugin-os-load` (`md-plugin-check --check-os-load`: unconfigured start, wrong/missing file,
  OS load, settings written, silence and engine block count at 44.1/48 kHz, a second instance picks up the shared path).
  `mini_test` returns 77 when every selected case skipped, and the tests use `SKIP_RETURN_CODE 77`, so a run without the OS
  shows "Skipped" rather than "Passed".
- **`md-plugin-check --snapshot <dir>`** renders the editor offscreen to PNG (unconfigured / wrong-file / loaded). Added
  because this session had no screen-recording or accessibility permission to look at the window; 0007 can reuse it for
  its UiSnapshot-style review.

## Verification (2026-10-12, macOS 26.6 arm64, Apple clang 21, CMake 4.4.4, Ninja 1.13.2)
- Fresh `rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`: 49 s wall (JUCE
  download included); AU, VST3, Standalone, md-hash, md-machines, md-tests, md-plugin-check built. Copy-after-build put
  `Drummodule.component` / `Drummodule.vst3` in `~/Library/Audio/Plug-Ins/`.
- `build/src/cli/md-hash <OS 1.63>` -> `hash 04ef1db4fe767721`; same with `MD_GROUPS=2` and `MD_GROUPS=4`.
  `md-machines` lists 135 machines.
- `MD_OS_SYX=<OS> ctest --test-dir build --output-on-failure`: 3/3 passed (4.5 s). Without `MD_OS_SYX`: `golden-hash` and
  `plugin-os-load` Skipped, `firmware` passes its OS-free case.
- `auval -v aumu Mdo1 Drmd`: AU VALIDATION SUCCEEDED, unconfigured and with the OS loaded (`MD_SETTINGS_DIR`).
- Standalone run (with `MD_SETTINGS_DIR` in the scratchpad): window 640x240 came up unconfigured; the OS was then picked
  with "Select OS File..." (the native panel) and the settings file got the chosen path; relaunch boots the engine from the
  shared setting (RSS ~495 MB); a settings file pointing at a junk `.syx` reports the error and the app keeps running.
- Boot + warm-up of the engine: ~34 ms.

## Notes for later increments
- **Memory:** one engine is ~420 MB RSS (md-hash peak, identical to the upstream build): mostly the DSP's 8M-word X/Y space and
  the interpreter's per-address opcode cache for 2M words of P. The standalone sits at ~495 MB. Matters for many plugin
  instances (0003/0005); `VoiceEngine.cpp`'s `kSizeXY` / `kSizeP` are the knobs, but any change must keep the golden hash.
- **0003:** `MdOneProcessor::processBlock` already keeps the engine in step with the host (`m_engineDue`, `engineBlocks()`)
  and swaps engines under `m_engineLock` with `ScopedTryLock` on the audio thread; replace the discard with the note map,
  bus layout and Monomodule's resampler. `kWarmUpBlocks` (16) runs in `loadEngine()`. `md-plugin-check` is the place for
  the offline regression render ("next to md-hash's").
- **0007:** `OneEditor` is a placeholder (title, text, picker button, status, path). `md-plugin-check --snapshot` renders it.
- **0010:** the hardened-runtime entitlements file is generated (`MdOne_artefacts/JuceLibraryCode/*.entitlements`, JIT +
  unsigned memory + library validation) but Ninja builds are only ad-hoc linker-signed; packaging must `codesign
  --options runtime --entitlements` them. The interpreter needs no JIT entitlement.
- The linker warns `reducing alignment of section __DATA,__common from 0x8000 to 0x4000` for every engine binary; the upstream
  build does the same, harmless.
- The emulators log to stdout/stderr (`MemoryBuffer@...`, `writeCRA@...`, mc68k `write16@...`); md-hash's `hash` line is the
  only line of its own.
- `THIRD_PARTY_NOTICES.md`: the mc68k wrapper is GPL-3.0 (its `LICENSE.md`), Musashi inside it MIT, SoftFloat 2b its own
  licence.
