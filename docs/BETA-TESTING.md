# Beta testing Moduledrum

Thank you for trying a beta. This page says what is in the download, where to put it, the one check that tells us
whether the emulation is exact on your machine, and what to report.

## What you need

- **The download for your machine** (the zip from the project's Releases page or from a CI run):

  | File | For |
  |---|---|
  | `Moduledrum-<version>-macos-universal.zip` | macOS 12 or later, Apple Silicon and Intel |
  | `Moduledrum-<version>-windows-x64.zip` | Windows 10 (1803 or later) and 11, 64-bit Intel or AMD |
  | `Moduledrum-<version>-windows-arm64.zip` | Windows 11 on ARM (experimental) |
  | `Moduledrum-<version>-linux-x64.zip` | Linux, 64-bit Intel or AMD |
  | `Moduledrum-<version>-linux-arm64.zip` | Linux, 64-bit ARM (Raspberry Pi 5, Asahi, ARM VMs) |

- **The Machinedrum OS file.** Moduledrum is a chip-level emulation: it runs the real Machinedrum SPS-1 UW OS 1.63,
  which is Elektron's and is not in the download. Get `Elektron_SPS1-1UW_OS1.63.zip` from Elektron's support site
  (the Machinedrum downloads, free) and unzip it. The file you need is `Elektron_SPS1-1UW_OS1.63.syx`. Keep it
  somewhere permanent: Moduledrum reads it from where you leave it.
- **A DAW that loads VST3 plugins** (on macOS also Audio Units), or just the standalone app.

## Installing

Unzip the download. Inside:

| Folder | What | Where to put it |
|---|---|---|
| `VST3/Moduledrum.vst3` | the VST3 plugin | macOS: `~/Library/Audio/Plug-Ins/VST3/`. Windows: `C:\Program Files\Common Files\VST3\`. Linux: `~/.vst3/` |
| `AU/Moduledrum.component` | the Audio Unit (macOS only) | `~/Library/Audio/Plug-Ins/Components/` |
| `Standalone/` | the standalone app | anywhere |
| `Tools/md-hash` | the exactness check (below) | anywhere |

Then start your DAW and let it rescan plugins (Logic scans Audio Units when it starts). The first time Moduledrum
opens it asks for the OS file: choose the `.syx` you unzipped. You do this once; every instance shares the setting.

**macOS:** a beta is not notarized, so the first launch is blocked by Gatekeeper. Right-click the app and choose
*Open*, or run this on the unzipped folder:

```bash
xattr -dr com.apple.quarantine Moduledrum-<version>-macos-universal
```

**Windows:** the beta is not code-signed, so SmartScreen warns when you start the standalone or `md-hash`. Choose
*More info* and *Run anyway*. The VST3 inside a DAW raises no warning.

**Linux:** the emulator keeps the DSPs' memory in `/dev/shm`. A normal desktop has plenty. In a Docker container,
start it with `--shm-size=1g`.

## The one check that matters: `md-hash`

Moduledrum runs the Machinedrum's DSP program on an emulator that compiles DSP code to your CPU's instructions as it
runs (a JIT). That compiler has a back end for Apple Silicon and ARM, which we have verified exhaustively, and one
for Intel and AMD (x86-64), which has so far only run inside Rosetta on a Mac. **If you are on an Intel or AMD
machine, this check is the most useful thing you can do.**

`md-hash` renders 12 seconds of a fixed pattern through every track and effect and prints a hash of the audio. The
right answer, the same on every platform, is:

```
hash 04ef1db4fe767721
```

Run it with your OS file. macOS or Linux, in a terminal, from the unzipped folder:

```bash
Tools/md-hash /path/to/Elektron_SPS1-1UW_OS1.63.syx
```

Windows, in a Command Prompt or PowerShell, from the unzipped folder:

```
Tools\md-hash.exe C:\path\to\Elektron_SPS1-1UW_OS1.63.syx
```

It takes a second or two. If the hash matches, the emulation on your machine is bit for bit what it is on ours.

If it does not match, please run it once more with the lockstep check on, which runs the JIT and the reference
interpreter side by side and reports the first instruction where they disagree, and send us everything it prints:

```bash
MD_JIT_LOCKSTEP=1 Tools/md-hash /path/to/Elektron_SPS1-1UW_OS1.63.syx
```

```
set MD_JIT_LOCKSTEP=1
Tools\md-hash.exe C:\path\to\Elektron_SPS1-1UW_OS1.63.syx
```

(Lockstep is slow, a minute or so, and prints a lot. That is the point.)

## Then try it as an instrument

- Open the standalone, choose the OS file, play the on-screen keyboard or a MIDI keyboard. Tracks 1 to 16 are on the
  white keys from C2 (as on the hardware); the GLOBAL page's NOTES setting has a chromatic map too.
- Load the VST3 (or AU) in your DAW on an instrument track. Play notes. Try the 19 outputs if your DAW shows them
  (main, one per track, the reverb and delay sends).
- Switch machines, turn knobs, load a kit `.syx` if you have one, save and reopen the project.
- Watch the CPU meter. On an Apple M-series core, 16 busy tracks cost under 10% of one core.

## What to report

Open an issue (or message the author) with:

1. Your OS and version, CPU model, and whether it is Intel/AMD or ARM.
2. The DAW and its version, and the plugin format you used (VST3, AU, standalone).
3. The output of `md-hash` (and of the lockstep run, if the hash was wrong).
4. What worked, what did not, and the CPU cost you saw. Crashes: what you did just before, and any crash log.
5. Any difference you hear from a real Machinedrum, if you have one to compare with.

Nothing you send contains the OS file or anything derived from it: the hash is a checksum of the rendered audio.
