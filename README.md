# Moduledrum

**Moduledrum** is a free, open-source, chip-level emulation of the Elektron Machinedrum SPS-1 UW as AU, VST3 and
standalone plugins for macOS. Like [Monomodule](https://github.com/shnolk/monomodule) does for the Monomachine, it
doesn't approximate the drum machine with new DSP code. It runs the Machinedrum's own voice DSP program in an emulated
Motorola DSP56300 and the OS's own machine code in an emulated ColdFire, on the engine of sd88me's
[Machinemodule for MPC OS](https://github.com/sd88me/mpc-vst-machinedrum).

<p align="center">
  <img src="docs/moduledrum.png" width="760" alt="The Moduledrum editor: the Machinedrum LCD with track 5's SYN, AMP/EFX, ROUTE and LFO pages, and the strip of 16 tracks">
</p>

> **Moduledrum is not affiliated with, endorsed by or sponsored by Elektron.** "Elektron" and "Machinedrum" are
> trademarks of Elektron Music Machines MAQ AB, used here only to describe what Moduledrum emulates. Moduledrum
> contains no Elektron software. To make sound it needs the Machinedrum OS file, which you download from Elektron
> yourself (see [Getting the OS file](#getting-the-os-file)).

**Status:** early development. It plays, it saves, and the editor is complete, but the master effects and the ROM
machines are still missing (see [Known limitations](#known-limitations)) and there is no packaged release yet.

## What's included

| | Type | What it does |
|---|---|---|
| **Moduledrum** | Instrument (AU, VST3, standalone) | All 16 Machinedrum tracks in one instance, with a main mix, 16 track outputs and the reverb and delay sends as separate outputs. MIDI notes trigger tracks 1-16 as on the Machinedrum (the white keys from C2: 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62), or chromatically (36-51 and 0-15) with the NOTE MAP parameter. The editor recreates the Machinedrum's LCD pages. |
| **Command-line tools** | `md-hash`, `md-machines`, `md-kits`, `md-plugin-check` | An audio hash of a fixed 16-track pattern to compare builds, the OS's machine table, the kits of a `.syx` dump (list, render one to WAV), and headless checks and snapshots of the plugin. |

Every knob of the 16 tracks (machine, the SYN, EFFECTS and ROUTING pages, LEV, the LFO page) is a host parameter with
the hardware's raw values and names, and the whole sound is saved with the project. Kits import from and export to
Machinedrum kit dumps (`.syx`) byte for byte.

The window can be resized from 50% to 200% with its proportions kept: drag the grip in the bottom-right corner. Every
Moduledrum window shares one size setting.

## The editor

The LCD is drawn with the OS file's own fonts and dials, in the layout of Machinemodule's MPC skin.

<p align="center">
  <img src="docs/global.png" width="49%" alt="The GLOBAL tab: the mixer with the 16 tracks' levels, tempo, MIDI channel, note map, accent and the kit browser">
  <img src="docs/machine-picker.png" width="49%" alt="The machine picker: the OS's machines in columns by group (GND, TRX, EFM, E12, P-I, INP, MID, CTR, ROM, RAM)">
</p>

- **Header:** the selected track's machine (click it for the machine picker), the loaded kit and its bank (click it for
  the kit browser).
- **Pages:** SYN | AMP/EFX over ROUTE | LFO for the selected track. SYN's knobs are named after the machine. Every cell
  is a knob on its host parameter: drag up or right to turn it up (shift: finer), use the mouse wheel (shift: one step),
  click the value to type it (or, for a named value such as LFO TRACK or UPDTE, to pick it from a list), double-click
  the dial for its default. Values are the raw ones the hardware shows (bipolar ones signed: PAN -1, EQG +20).
- **Strip** (bottom): GLOBAL and tracks 1-16 with their machines. Click to select; click the selected track again to
  play it. Tracks flash when they play.
- **Machine picker:** the OS's machines by group. Greyed machines make no sound in this engine (INP, MID, CTR, RAM,
  and ROM, which needs samples from a real unit); a kit that uses one shows it, marked SILENT.
- **GLOBAL:** MIXER (the 16 tracks' LEV), TEMPO (used when the host gives none), CHAN (MIDI channel), NOTES (note map),
  ACCNT (accent), INIT KIT, EXPORT (the current kit as a Machinedrum kit dump `.syx`), and KITS, the kit browser.
- **Kits:** every `.syx` in `~/Library/Application Support/Moduledrum/Machinedrum/kits/` is a bank of kits (free slots
  are hidden); click a bank, then a kit to load it. Drop a `.syx` (a kit dump from C6, Transfer or an MD backup) anywhere
  on the editor to copy it there. FOLDER opens the folder; the browser notices files added or changed there.

## Getting the OS file

Moduledrum needs **Machinedrum OS 1.63**, a free download from Elektron's website (Machinedrum support / downloads):

1. Download `Elektron_SPS1-1UW_OS1.63.zip` from Elektron.
2. Unzip it. Inside is `Elektron_SPS1-1UW_OS1.63.syx`.
3. Open Moduledrum, click **Select OS File...** and choose that `.syx` file.

You only have to do this once: every Moduledrum instance shares the setting. The file stays where you put it.
Moduledrum reads it each time it starts and never copies it or redistributes it.

## Installing a release

There are no pre-built downloads yet (a universal macOS build and an installer are planned). Build it from source
below; the build installs the AU and VST3 plugins for you.

## Building from source

### Requirements

- macOS 12 or newer, with Xcode or the Xcode command line tools (`xcode-select --install`)
- CMake 3.22 or newer, Git and, optionally, [Ninja](https://ninja-build.org)
- An internet connection for the first configure: CMake downloads [JUCE](https://github.com/juce-framework/JUCE) 8.0.9.
  The two emulators are git submodules under `ext/`.

### macOS

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

What you get:

| | |
|---|---|
| `build/src/plugin/MdOne_artefacts/Release/Standalone/Moduledrum.app` | standalone app |
| `build/src/plugin/MdOne_artefacts/Release/AU/Moduledrum.component`, `.../VST3/Moduledrum.vst3` | plugins, also copied to `~/Library/Audio/Plug-Ins/` |
| `build/src/cli/md-hash`, `md-machines`, `md-kits` | the command-line tools |
| `build/src/plugin/MdPluginCheck_artefacts/Release/MdPluginCheck` | `md-plugin-check`, the plugin's headless checks |

### Build options

| Option | Default | Meaning |
|---|---|---|
| `MD_BUILD_PLUGIN` | `ON` | Build the plugin. `OFF` builds only the engine, the tests and the command-line tools. |
| `MD_BUILD_TESTS` | `ON` | Build the unit tests. |
| `MD_COPY_PLUGINS` | `ON` | Copy the plugins to `~/Library/Audio/Plug-Ins/` after each build. |
| `MD_OS_SYX` | *(empty)* | Path to your OS file. Used by the tests and the command-line tools. |
| `MD_USE_JIT` | `OFF` | Run the emulated DSP on dsp56300's JIT instead of its interpreter. Experimental: it does not boot yet. |

### Tests

```bash
MD_OS_SYX=/path/to/Elektron_SPS1-1UW_OS1.63.syx ctest --test-dir build --output-on-failure
```

Without the OS file, the tests that run the engine are reported as skipped and the rest still run.
`MD_KITS_SYX=/path/to/a kit dump .syx` adds the kit cases: kit files decoded and re-encoded byte for byte, a kit through
the plugin's parameters and saved state, and the editor's kit browser. `plugin-editor` drives the editor headlessly;
`md-plugin-check --snapshot <dir>` renders its pages to PNG for review (they show artwork read from your OS file, so
don't commit them).

The golden test checks that `md-hash`'s pattern still renders bit for bit what it did: `build/src/cli/md-hash <OS.syx>`
prints `hash 04ef1db4fe767721` with OS 1.63, and `MD_GROUPS=2` must print the same.

## How it works

- **OS file.** `src/core/md/Firmware` unpacks the user's OS file: the sysex transport, the flash container and the
  compressed sections. From these come the ColdFire OS image, the voice DSP's program and the tables the effects use.
- **Voices.** `VoiceEngine` runs the Machinedrum's voice DSP (DSP2) program in the dsp56300 emulator. A small harness
  replaces the hardware's link DMA and frame sync (the Monomodule approach): every 32-sample block renders all 16
  voices and reads them out over the host port.
- **Machine code.** `MachineRunner` runs the OS's own per-machine coefficient functions, ColdFire code from the OS
  image, in the mc68k emulator: the 8 SYN values go in, and the voice DSP's slot words come out.
- **Host model.** `HostModel` does the main processor's job around them, as the OS's tick routine does: triggers,
  machine assignment, parameter smoothing and the LFOs (the OS's own routines again).
- **Effects and mix.** `TrackFx` and `Mixer` are the mixer DSP's (DSP1) per-track chain (AMD, EQ, filter, SRR,
  distortion) and its mix (pan law, VOL, the reverb and delay sends, the individual outputs), translated to native C++
  and bit-exact with the DSP code. Their tables are read from the OS file.
- **Plugin.** `src/plugin` holds the plugin. The engine runs at 44.1 kHz inside `processBlock`; at other host rates
  Monomodule's band-limited resampler converts it. `src/core/md/library` decodes and re-encodes Machinedrum kit dumps.
  The editor draws the LCD with the fonts and dials from your OS file. Without an OS file it falls back to a built-in
  stand-in face.

The engine in `src/core/md` is sd88me's, vendored from Machinemodule for MPC OS
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) has the details).

## Known limitations

- **No master effects yet.** The Machinedrum's master section (rhythm echo, gate/reverb, EQ, dynamix) runs on the
  mixer DSP and is not emulated. The reverb and delay sends come out as their own outputs, ready for your own
  effects.
- **ROM machines and the factory kits** need the sample flash of a real unit, which is not part of the OS file. INP,
  MID, CTR and RAM machines are silent too.
- **No sequencer.** Moduledrum plays the 16 tracks from MIDI notes. Patterns, songs, parameter locks and trigger groups
  are not modelled.
- **CPU.** The emulated DSP runs on dsp56300's interpreter (the JIT does not boot the voice harness yet). On Apple
  Silicon, 16 tracks cost about 18% of one core on `md-hash`'s sparse pattern, and about 32% with every track
  retriggered 8 times per beat.
- **macOS only** for now, and no packaged release.

## Settings and data

Moduledrum keeps its settings and kit folder in `~/Library/Application Support/Moduledrum/Machinedrum/`
(`settings.xml` and `kits/`).

## Development

Work is planned and logged with [clew](https://github.com/npatten/clew). `.clew/increments/` holds the open work in
priority order (`.clew/path.md`), and `.clew/archive/` the finished increments with their decisions, measurements and
regression hashes. With clew installed, `clew list` shows what's next.

## Licence

Moduledrum is free software: you can redistribute it and/or modify it under the terms of the
**GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)). Anyone who distributes Moduledrum, modified or
not, must pass on these same rights and make the corresponding source code available.

It is built on [JUCE](https://github.com/juce-framework/JUCE) (AGPLv3), [dsp56300](https://github.com/dsp56300/dsp56300)
(GPLv3) and mc68k (GPLv3), among others. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every dependency, its
licence and the code adapted from Monomodule and Machinemodule for MPC OS.

## Credits

Moduledrum is made by [chriskrofchak](https://github.com/chriskrofchak).

It stands on the work of:

- **Elektron**, for the Machinedrum. Its OS and its sound belong to them, and Moduledrum ships none of it.
- **sd88me's [Machinemodule for MPC OS](https://github.com/sd88me/mpc-vst-machinedrum)**, the Machinedrum engine at the
  heart of Moduledrum: the host model, the machine runner, the voice engine, the track effects and mix bit-exact with
  the hardware's DSP code, the parameter layout, the kit-dump parsing, where the OS file keeps its LCD artwork, and the
  skin layout the editor follows. Moduledrum is, in large part, that work brought to the desktop.
- **shnolk's [Monomodule](https://github.com/shnolk/monomodule)**, the model this project follows: the approach (the
  machine's own DSP in an emulator, a host model for the main processor) and the plugin shell, the LCD editor drawn
  with the OS file's own fonts, the resampler, the shared settings and the OS-file picker.
- **[dsp56300](https://github.com/dsp56300/dsp56300)**, the DSP56300 emulator that runs the voice DSP, built from
  sd88me's fork [sd88me/dsp56300](https://github.com/sd88me/dsp56300), which includes Monomodule's dsp56300 patch.
- **[gearmulator](https://github.com/dsp56300/gearmulator)**'s mc68k, the 68k-family emulator that runs the OS's machine
  code, in the fork [joelanders/mc68k-md-mm](https://github.com/joelanders/mc68k-md-mm) by Joe Landers, whose
  full-system Machinedrum emulation [gearmulator-md-mm](https://github.com/joelanders/gearmulator-md-mm) is the
  reference the engine was checked against.
- **[Musashi](https://github.com/kstenerud/Musashi)** by Karl Stenerud, the CPU core inside mc68k.
- **[JUCE](https://github.com/juce-framework/JUCE)**, the plugin and UI framework.
- **[AsmJit](https://github.com/asmjit/asmjit)**, the JIT back end of dsp56300.
- **[MCL](https://github.com/jmamma/MCL)** by jmamma, whose Machinedrum kit layout the kit codec was checked against.
- **[elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool)** by mischa85, credited by Monomodule for
  its work on the Elektron OS file format. Moduledrum's OS-file decoder descends from Monomodule's.
- **[clew](https://github.com/npatten/clew)** by npatten, a fast, local, command-line issue tracker made for working
  with coding agents. Every increment of Moduledrum, from the first check that the engine builds on a Mac to the
  editor's last page, was planned, worked through and closed in it, and `.clew/` keeps the whole story, dead ends
  included. It kept a project with a lot of moving parts small, steady and tested at every step. Highly recommended.
- Built with [Claude Code](https://claude.com/claude-code).

Bug reports and pull requests are welcome.
