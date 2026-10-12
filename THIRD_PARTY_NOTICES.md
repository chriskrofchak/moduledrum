# Third-party notices

Moduledrum is licensed under the GNU Affero General Public License v3.0 (see `LICENSE`). It is built from, or contains
code adapted from, the projects below. Each keeps its own copyright and licence. The emulators are git submodules under
`ext/`; JUCE is downloaded at configure time.

## Code copied or adapted into this repository

| Where | From | Licence |
|---|---|---|
| `src/core/dsp/Resampler.{h,cpp}` (the host-rate converter), `tests/test_resampler.cpp`, and the clock and conversion logic of `src/plugin/one/OneProcessor.cpp` (`m_engineAt`, chunked rendering) | [shnolk/monomodule](https://github.com/shnolk/monomodule) (Monomodule), `src/core/dsp/Resampler.{h,cpp}`, `tests/test_resampler.cpp`, `src/plugin/one/OneProcessor.cpp`, © Shnolk | GNU AGPL v3 |
| `src/core/md/` (the Machinedrum engine: `HostModel`, `MachineRunner`, `VoiceEngine`, `ParallelVoiceEngine`, `TrackFx`, `Mixer`, `MdEngine.h`, `Dsp56.h`, `Engine.cpp`, `Firmware.{h,cpp}`) | [sd88me/mpc-vst-machinedrum](https://github.com/sd88me/mpc-vst-machinedrum) (Machinemodule for MPC OS), `engine/*` and `tools/mdfw/Firmware.{h,cpp}` at commit `6ea7a4a` | GNU AGPL v3 |
| `src/cli/md_hash.cpp`, `src/cli/md_machines.cpp`, `src/core/golden/GoldenPattern.h` | the same repository, `tools/mdhash/mdhash.cpp` and `tools/mdmachine/mdmachine.cpp` (ported; the md-hash pattern moved into `GoldenPattern.h`) | GNU AGPL v3 |
| `src/core/md/library/` (the Machinedrum kit dump decoder: 7-bit unpacking, name, machines, parameters, LFOs; extended to the whole message, with an encoder), `src/cli/md_kits.cpp` | the same repository, `vst/engine.cpp` (`decode7`, `parseKit`); the dump layout cross-checked against [jmamma/MCL](https://github.com/jmamma/MCL)'s `MDKit` (read, not copied) | GNU AGPL v3 |
| The host parameter set and ids of `src/plugin/one/OneParams.h` (`track<n>_machine`, `_syn1`.., `_lfo_shp1`.., `tempo`) and the machine-change semantics of `OneProcessor.cpp` (SYN1-8 reset to the machine's defaults unless set since, pushed back to the host) | the same repository, `vst/gen_params.py`, `vst/params.json` and `vst/engine.cpp` | GNU AGPL v3 |
| Where Machinedrum OS 1.63 keeps its LCD artwork (the addresses and descriptor layouts in `src/plugin/MdRomArt.cpp`), the drawn OFF/ON toggle icon, the MD LCD colours and the editor's layout decisions | the same repository, `tools/mdskin/mdartdump.cpp` and `tools/mdskin/mk_skin.py` | GNU AGPL v3 |
| `src/core/util/Wav.{h,cpp}`, `src/plugin/SharedSettings.h`, the firmware handling in `src/plugin/one/OneProcessor.cpp` and the OS picker in `OneEditor.cpp`, the `--check-*` tool shape of `src/plugin/Check.cpp`, `tests/mini_test.{h,cpp}`, the CMake layout | [shnolk/monomodule](https://github.com/shnolk/monomodule) (Monomodule), © Shnolk | GNU AGPL v3 |
| The SYN parameters named after the machine and defaulting to its defaults (`SynParam` in `src/plugin/one/OneProcessor.cpp`), the state as a versioned XML | the same repository, `src/plugin/one/OneParams.h` and `OneProcessor.cpp` | GNU AGPL v3 |
| The LCD editor: `src/plugin/MdRomArt.{h,cpp}` (the artwork reader), `StandInFont.h` (the built-in stand-in face, Monomodule's drawing), `Skin.h`, `WindowScale.h`, `ScaledEditor.h`, `src/plugin/one/Lcd.{h,cpp}`, `SpecData.{h,cpp}` and the shape of `OneEditor.{h,cpp}` | the same repository, `src/plugin/one/{RomArt,Lcd,SpecData,OneEditor}` and `src/plugin/{Skin,WindowScale,ScaledEditor}.h` | GNU AGPL v3 |
| The editor's pages and controls in `src/plugin/one/OneEditor.{h,cpp}` (`KnobCell`'s invisible sliders with drag, wheel, typed value and value list, `KnobPage`, the LEV bar of `MixerPage`, `MachineBar`, `LcdButton`, the track keys of `TrackStrip`), `src/plugin/one/MachinePicker.{h,cpp}` (the overlay, its unrolling and keys) and the list style of `src/plugin/one/KitBrowser.{h,cpp}` | [shnolk/monomodule](https://github.com/shnolk/monomodule), `src/plugin/one/{OneEditor,MachinePicker,LibraryUi}.{h,cpp}`, © Shnolk | GNU AGPL v3 |
| The editor's layout (the machine block and product name in the header, the 2x2 SYN, AMP/EFX, ROUTE, LFO pages, the GLOBAL tab with the 16 LEV faders and the bank / kit browser), the LFO page's cells (TRACK and PARAM as text, the six LFO shapes drawn in `Lcd.cpp`'s `drawLfoShape`, UPDTE as text), the machine picker's columns by group, and the kit folder rescanned on change (`src/plugin/KitLibrary.{h,cpp}`) | [sd88me/mpc-vst-machinedrum](https://github.com/sd88me/mpc-vst-machinedrum), `tools/mdskin/mk_skin.py` and `vst/engine.cpp` | GNU AGPL v3 |

The vendored engine is unchanged from upstream except for its include paths (`VoiceEngine.h` and `TrackFx.h` include
`Firmware.h` from the same folder). Upstream itself credits Monomodule: `VoiceEngine`'s harness is adapted from
Monomodule's `src/core/dsp/DspEngine.cpp`, and `Firmware.{h,cpp}` from Monomodule's `src/core/firmware`.

## Libraries compiled into Moduledrum

| Project | Used for | Licence |
|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) 8.0.9, © Raw Material Software Limited | plugin formats, UI, audio and MIDI I/O | GNU AGPL v3 (JUCE is dual-licensed: AGPLv3 or the commercial JUCE licence; Moduledrum uses it under the AGPLv3) |
| [dsp56300](https://github.com/dsp56300/dsp56300), © the dsp56300 authors; sd88me's fork [sd88me/dsp56300](https://github.com/sd88me/dsp56300) at `26558e5401fa134a013eca8dbb8c5ecde837ec55` (`ext/dsp56300`), with Moduledrum's patch `ext/patches/0001-dsp56300-md.patch` | Motorola DSP56300 emulator that runs the Machinedrum's voice DSP program | GNU GPL v3 |
| [AsmJit](https://github.com/asmjit/asmjit), © the AsmJit authors (dsp56300's `source/asmjit` submodule) | JIT back end of dsp56300 (the emulated DSP runs on the JIT unless built with `MD_USE_JIT=OFF`) | zlib |
| mc68k, [gearmulator](https://github.com/dsp56300/gearmulator)'s 68k emulator wrapper, © the dsp56300/gearmulator authors; joelanders' fork [joelanders/mc68k-md-mm](https://github.com/joelanders/mc68k-md-mm) at `ace95b3d0a5a332db147244762dda65f9a010b9f` (`ext/mc68k`) | runs the Machinedrum OS's own machine functions (ColdFire code) | GNU GPL v3 (its `LICENSE.md`) |
| [Musashi](https://github.com/kstenerud/Musashi), © Karl Stenerud (inside mc68k, `Musashi/`) | the 68000-family CPU core | MIT |
| SoftFloat Release 2b, by John R. Hauser (inside Musashi, `Musashi/softfloat/`) | floating-point emulation of Musashi's FPU | SoftFloat-2 licence (free use and derivative works with notice; see the file headers) |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk), © Steinberg Media Technologies GmbH (bundled with JUCE) | VST3 plugin format | GNU GPL v3 (dual-licensed: Steinberg VST3 licence or GPLv3; used under the GPLv3) |
| [AudioUnitSDK](https://github.com/apple/AudioUnitSDK), © Apple Inc. (bundled with JUCE, macOS only) | Audio Unit plugin format | Apache 2.0 |
| Intel ITT / JIT profiling API (part of dsp56300; Windows and Linux builds only), © Intel Corporation | profiler hooks of the dsp56300 JIT | BSD 3-Clause (dual-licensed: GPL-2.0-only or BSD-3-Clause; used under the BSD licence) |
| Other JUCE dependencies (zlib, libpng, jpeglib, FLAC, Ogg Vorbis, HarfBuzz, SheenBidi and others) | as listed in JUCE's `LICENSE.md` | as listed there |

The GNU GPL v3 and the GNU AGPL v3 may be combined (section 13 of each), and the combined work is distributed under
the AGPL v3.

The sd88me dsp56300 fork already contains Monomodule's dsp56300 patch (saturation mode, MPYRI/PFLUSH, JIT fixes; marked
`MNM patch` in its source). Moduledrum adds three JIT fixes of its own (`ext/patches/0001-dsp56300-md.patch`, marked
`MD patch`, applied to the checkout at configure time; GPLv3 like the code they change).

## Elektron

Moduledrum is an independent project. It is not affiliated with, endorsed by or sponsored by Elektron Music Machines
MAQ AB. "Elektron" and "Machinedrum" are trademarks of Elektron Music Machines MAQ AB, used here only to identify the
hardware that Moduledrum emulates. Moduledrum does not include any part of Elektron's software. It needs the
Machinedrum OS file, which each user downloads from Elektron and selects when Moduledrum first runs. The editor's LCD
fonts and dials are likewise read from that file at run time and never stored; without it a built-in stand-in face is
drawn. The screenshots in `docs/` show the editor as it draws itself with that artwork.
