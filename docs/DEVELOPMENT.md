# Development notes

What anyone picking up Moduledrum needs to know that the code does not say by itself: where it comes from, what has
been established and measured, the decisions that were made and why, and how to check that a change keeps the sound
bit for bit. The finished work items in [`.clew/archive/`](../.clew/archive/) hold the full detail of each step
(decisions, measurements, verification output); this file is the summary.

## Where the project comes from

Moduledrum was built in one day (2026-10-12) on top of two existing projects, both AGPL-3.0:

- **[shnolk/monomodule](https://github.com/shnolk/monomodule)**, a chip-level Monomachine emulation. It is the model
  for everything plugin-side: the JUCE shell, the OS-file picker and shared settings, the pixel-exact LCD editor drawn
  with the fonts and dials read from the user's own OS file, the resampler (the engine runs at the hardware's 44.1 kHz),
  the resizable window, and the knob interaction. `src/plugin` is derived from its `src/plugin/one`; `src/core/dsp`
  is its resampler.
- **[sd88me/mpc-vst-machinedrum](https://github.com/sd88me/mpc-vst-machinedrum)** ("Machinemodule for MPC OS"), the
  Machinedrum sound engine as a VST2 for Akai MPC/Force hardware. `src/core/md/` is its `engine/` and `tools/mdfw`,
  vendored at commit `6ea7a4a` (the only change: include paths). Its `HANDOFF.md` is the full log of how the engine
  was reverse-engineered and verified against a full-system emulation (gearmulator-md-mm); read it before touching the
  engine. The kit-dump decoder, the parameter layout and the LCD artwork addresses come from it too.

Everything MPC-specific was left behind on purpose: the render-ahead thread and ring (8.7 ms of latency, there to ride
out MPC OS stalls), `/proc/stat` core picking and `sched_setaffinity`, the voice budget and silence release (CPU
measures for a Gen 1 Force), the "tap" plugins (MPC allows one stereo output per plugin; a DAW gets 19 buses), the
static recompiler (the Force has no JIT; a Mac has one).

The emulators are git submodules under `ext/`: `ext/dsp56300` (sd88me's fork of
[dsp56300](https://github.com/dsp56300/dsp56300), which already contains Monomodule's emulator patch) and
`ext/mc68k` (joelanders' fork of gearmulator's Musashi wrapper, for the OS's own machine routines).

## Files that are never in the repository

- **The Machinedrum OS** (`Elektron_SPS1-1UW_OS1.63.syx`, free from Elektron's site; sha256 of the .syx
  `a58cd61f2efacfb07add0c643162fb4365c30e73831f2021c17b4aa3b42cabd5`). It is the sound engine, the LCD artwork and the
  machine table. Tests and tools take it from `MD_OS_SYX` (environment variable or CMake option).
- **Kit dumps** (`.syx`). Elektron's `MD_presets_1_50.syx` (64 kit messages, of which slots 0-15 are real kits: TRX,
  EFM, E12, P-I, FRANSISCO, BIPBOP, WIGGLY, BENTY, TOAST, BOOGIE, SWARM, MAYHEM, HAUNTED, ALIENS, BRONCO, FUGLESANG;
  plus 128 patterns, 32 songs, 8 globals) is the test file, via `MD_KITS_SYX`.
- **A flash image** of a Machinedrum UW (8 MB, from a real unit). Not available so far; it is what the ROM sample
  machines and the factory kits need (see "Open work").
- Anything rendered from them: `md-plugin-check --snapshot` PNGs made with an OS loaded, `art.json`, WAVs. Hashes,
  names and numbers in tests are fine. The three screenshots in `docs/` were published by the project owner.

## How to check that a change keeps the sound

Every engine or plugin change must keep these; they are what "bit-exact" means here.

| Check | Expected | Where |
|---|---|---|
| `build/src/cli/md-hash <OS>` (also `MD_GROUPS=2`, `4`) | `hash 04ef1db4fe767721` | ctest `golden-hash`, `src/core/golden/GoldenPattern.h` |
| Kit 0 (TRX) of the preset file, `applyKit` on a fresh engine | `f5c618b7df5550fa` | ctest `kits`, `tests/test_kits.cpp` |
| Kit 0 through the processor (after its 16 warm-up blocks), and after a state save/restore | `da62047690565e38` | ctest `plugin-state`, `src/plugin/Check.cpp` |
| The plugin's regression render, chromatic note map | `a28b9dc178776939` | ctest `plugin-render` |
| The same render, the hardware's white-key note map | `14291ff9302212d4` | ctest `plugin-render` |
| JIT = interpreter | all of the above identical with `-DMD_USE_JIT=OFF`; `MD_SWEEP=1 md-hash` identical per machine | ctest `jit`, `MD_JIT_LOCKSTEP=1` |

```bash
MD_OS_SYX=/path/to/Elektron_SPS1-1UW_OS1.63.syx MD_KITS_SYX=/path/to/MD_presets_1_50.syx \
  ctest --test-dir build --output-on-failure
```

Useful tools (all in `build/src/cli` or `build/src/plugin/MdPluginCheck_artefacts/Release/MdPluginCheck`):

- `md-hash <OS> [ROM_SAMPLES.bin]`: the golden pattern's hash. `MD_SWEEP=1` plays every machine and prints its peak
  (the release gate: none of the playable ones silent); `MD_TIMING=1` prints the render's stages; `MD_JIT_LOCKSTEP=1`
  (JIT builds) runs an interpreter shadow of each voice DSP and stops at the first register that differs.
- `md-machines <OS>`: the OS machine table (135 machines: id, name, SYN labels).
- `md-kits list|render`: the kits of a `.syx`, and one kit rendered to a WAV (each track solo, then all).
- `md-voicediag`: the voice DSP program as the harness sets it up, disassembled or stepped.
- `md-plugin-check --check-os-load | --check-render | --check-state | --check-editor | --snapshot <dir> | --bench 20`:
  the plugin headless. `--bench` is the CPU figure quoted below (all 16 tracks retriggered 8x per beat at 120 BPM,
  64-sample blocks) and checks that no callback goes over budget.

## Established facts and measurements (Apple Silicon, macOS 26, Apple clang 21)

- **The emulated DSP runs on dsp56300's JIT, bit-exact with its interpreter.** `MD_USE_JIT` defaults to ON; an
  existing build directory keeps its cached value, so pass `-DMD_USE_JIT=ON` once. OFF defines
  `DSP56K_NO_JIT_RUNTIME` on every target (the constant `g_useJIT` is in the emulator's headers, so all targets must
  agree) and is the reference the JIT is checked against.
- **CPU:** `md-hash`'s 12 s of 16-track audio renders in 0.6 s on the JIT (2.2 s on the interpreter). The plugin bench:
  **7.2 / 7.5 / 7.7 % of one core at 44.1 / 48 / 96 kHz** with all 16 tracks hammered, worst callback ~260 µs against
  a 1451 µs budget (interpreter: ~33 %). An idle instance still runs the whole DSP every block (open work 0013).
- **Memory:** about 420-500 MB per instance (the emulator's memory and the interpreter's opcode cache). The MPC port
  trimmed this ("about 225 MB less RAM", its HANDOFF); not done here yet.
- **Latency:** 0 samples at 44.1 kHz, 40 at 48 kHz, 79 at 96 kHz (the resampler), reported to the host.
- **`ParallelVoiceEngine` (voices split over several DSP instances) is not bit-exact for real kits.** Kit 0 renders
  differently with 2 groups from block 301 (a TRX-MA: voices in one DSP instance share state, probably a noise source).
  The golden pattern happens not to show it. The plugin uses the single `VoiceEngine`; do not add group splitting
  without a kit-based hash test.
- **Interpreter quirk (not fixed; it is the reference):** `CLR` sets E/U/N but leaves the previous ALU op's lazy CCR
  update pending. The JIT is right; the voice program never reads those bits in between, so the audio is the same.
- **The machine sweep:** every GND, TRX, EFM, E12 and P-I machine makes sound. Silent by design: GND-- (empty), INP
  (audio input), MID/CTR (MIDI and control machines), RAM (sampling), ROM (no sample data without a flash image).

## The JIT story (increment 0011), for whoever touches the emulator

The JIT did not boot the voice harness at first (`voice DSP init did not complete: instruction budget exceeded at
PC=$10008b`). The voice program keeps ordinary code in the interrupt-vector area (init at P:$24, the whole block loop
at $64-$e5); without `JitConfig::interruptRegionIsCode` the JIT compiles everything below $100 as 2-word
fast-interrupt blocks, where a `jsr` pushes the wrong return address. That option (Monomodule's setting, for the same
reason) fixed the boot. `VoiceEngine::configureJit()` documents the configuration, including one-instruction JIT
blocks at the two harness flag stubs so that the per-voice instruction counts equal the interpreter's.

Running the JIT and the interpreter in lockstep then found **three bugs in dsp56300's JIT**, fixed in
`ext/patches/0001-dsp56300-md.patch` (applied to `ext/dsp56300` at configure time by `cmake/dsp56300.cmake`, which is
why that submodule always shows as modified; `MD patch` markers in the source; one failing-without-the-fix test each in
`tests/test_jit.cpp`):

1. `jitblock.cpp`: a REP and its repeated instruction could be split across two blocks, and the repeated instruction
   then ran once more.
2. `jitdspregpoolregpair.cpp`: a 48-bit read of x/y cleared the written flags of x0/x1/y0/y1, dropping a partial write
   made earlier in the same block (this changed the audio of 5 of the 16 preset kits).
3. `jitops_alu_aarch64.cpp` / `_x64.cpp`: ASL by zero set C for negative values on aarch64; multi-bit ASL never set L
   together with V on either back end.

These are upstream bugs (dsp56300/dsp56300) and affect Monomodule too; they have not been reported upstream yet.

### The x64 back end (increment 0010: first runs, under Rosetta 2)

`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds the plugins, the app and the tools; dsp56300 and asmjit pick the
JIT back end by compiler macro, so the x86_64 slice runs the x64 JIT, with the same patch. There is no Intel Mac at
hand; the slice ran under Rosetta on Apple Silicon (`arch -x86_64`). An arm64 CMake's `ctest` cannot be started with
`arch`, so the tests were started with `-DCMAKE_CROSSCOMPILING_EMULATOR="arch;-x86_64"`, which makes CMake prefix every
test command. OS 1.63:

- `md-hash`: `hash 04ef1db4fe767721`, also with `MD_GROUPS=2` and `4`.
- ctest: romart, firmware, golden-hash, kits, jit, resampler, plugin-os-load and plugin-editor pass.
- **Finding 1 (lockstep).** `MD_JIT_LOCKSTEP=1 md-hash` reports a first difference that the aarch64 JIT does not have:
  after JIT block 136872 (P:$1038c0 to $1038ca, instruction 5596681) `sr jit=80044 interp=80040`, i.e. Z is set on the
  x64 JIT. The block is a `DO #$22` loop in one machine's set-up code: per iteration two table words are loaded and
  each is shifted left by 12 with LSL. All other registers agree and the last shifted values are not zero, so Z should
  be clear (the interpreter and the aarch64 JIT say so). Cause, from reading the source (not yet confirmed by a run:
  Rosetta stopped working, see below): `alu_lsl` and `alu_lsr` in `jitops_alu_x64.cpp` open a
  `CcrBatchUpdate(N | C | V)`, which clears those bits up front and switches `ccr_update()` to OR-only; Z is not in
  the mask, yet both ops set it with `ccr_update_ifZero(CCRB_Z)`, so an earlier Z = 1 (a zero table word in the loop)
  is never cleared by a later non-zero result. The fix would be `CCR_Z` in both masks (lines 144 and 168), with a case
  in `tests/test_jit.cpp` (LSL of zero, then of a non-zero value, compared with the interpreter; the aarch64 versions
  have no batch). The golden hash is unaffected (the voice program does not read Z there), but lockstep stops at the
  first difference, so nothing after it has been compared.
- **Finding 2 (crashes).** `plugin-render` and `plugin-state` (`MdPluginCheck`) died with SIGSEGV, a null dereference
  in JIT-generated code on the main thread, in the one full ctest run made while the Mac was heavily loaded. They passed
  in every serial run after (4 of render, 15 of state); with 8 copies running at once, `--check-render` crashed in 21 of
  24 runs and `--check-state` in 0 of 24. On arm64 nothing crashes, also not with `MallocScribble`. Whether this is a
  bug in the x64 JIT or Rosetta's handling of self-modifying code (asmjit does not use `MAP_JIT` in a non-hardened
  x86_64 process, so the JIT memory is plain RWX, which Rosetta has to track) is not settled. An interpreter-only
  x86_64 build (`-DCMAKE_OSX_ARCHITECTURES=x86_64 -DMD_USE_JIT=OFF`) under the same parallel load separates the two,
  and an Intel Mac settles it.
- **Caution.** Do not attach lldb to the x86_64 slice: the session hung (0 % CPU, debugserver under Rosetta), and after
  it was killed this Mac's Rosetta translator stopped starting binaries it had not seen before (a freshly compiled
  hello world too: processes stuck in the kernel at exec, state U, which `kill -9` does not end) until `oahd` is
  restarted (`sudo killall oahd`) or the Mac reboots. Whether the lldb session caused it is not known.

So the x86_64 slice is experimental: the release script builds arm64 unless told `-a universal`, and the README says so.

## Decisions (made, not to be re-litigated without a reason)

- **Render synchronously in `processBlock`** (Monomodule's pattern), not on a render-ahead thread. The engine is fast
  enough and the latency is lower. Engine swaps (OS load) happen under a lock that `processBlock` only tries.
- **Engine rate 44.1 kHz**, the hardware's; other host rates go through Monomodule's band-limited resampler.
- **Buses:** Main (stereo) + Track 1-16 (mono, off by default) + Reverb send + Delay send (stereo). A track on its own
  bus leaves the dry main mix but still feeds the sends; never use `HostModel::setRouting` (the hardware's outputs A-F
  land in `Mixer::Output::frame`, which no bus plays) - use the processor's `setTrackOutput`.
- **MIDI note map:** the hardware maps tracks 1-16 to the white keys from C2 (36 38 40 41 43 45 47 48 50 52 53 55 57
  59 60 62; MD manual appendix B). That is the default; `note_map` = chromatic gives 36-51 and 0-15 together (the MPC
  port's conventions). `params::trackForNote` / `noteForTrack` in `src/plugin/one/OneParams.h` are the one place.
- **Host parameters:** 500, raw 0-127 integers with the hardware's names, ids = the MPC port's keys
  (`track0_machine`, `track0_syn1` ... `track0_level`, `track0_lfo_shp1` ..., `tempo`, `midi_channel`, `accent`,
  `note_map`), groups T1-T16 + Global, append-only once released. SYN names follow the machine. A machine change gives
  the engine the machine's SYN defaults at once; the SYN parameters follow on the message thread (30 Hz timer) and the
  host is told names changed. Restore order: machine before SYN.
- **State:** schema 2 = all parameters + the current kit as a kit dump (`encodeKit(parametersToKit())`), no OS path.
  The loaded `md::library::Kit` is the canonical in-memory form.
- **Kits:** `src/core/md/library` decodes and re-encodes `$52` kit messages byte for byte (all 64 of the preset file
  round-trip). Routing is not in a kit (it is in the `$50` global dump); free slots are kits whose first name byte is
  `$7f`. Master-effect blocks are decoded and kept raw (their parameter order is unverified until the master effects
  exist). Kits live in `~/Library/Application Support/Moduledrum/Machinedrum/kits/`, one bank per file.
- **Editor:** everything inside the LCD is drawn on a 1-bit canvas with the OS file's own fonts and dials (nothing
  firmware-derived is committed; a built-in stand-in face is used without an OS). Layout: SYN | AMP/EFX over ROUTE |
  LFO per track, a 16-track strip, a GLOBAL tab (mixer, settings, kit browser). The artwork reader trusts the OS 1.63
  addresses only after checking the ColdFire section's size and hash.
- **The processor must never take the engine lock from the message thread for display** (the old status line did, and
  the audio thread dropped blocks); the editor reads atomics.
- **Packaging:** releases are arm64 until the x64 JIT is verified (the universal build exists and is opt-in). The build
  signs the bundles itself (hardened runtime; the JIT entitlements only stick to the standalone app, plugins run on their
  host's), the installer installs system-wide (AU, VST3, app), and the `.zip` is made after notarization and stapling.
- **Voice budget, silence release and voice-group splitting stay off** (not hardware behaviour, or not bit-exact).

## Repository history and local files

The development history (about 25 commits, one per work item, with the agent reports) was squashed into one root
commit when the repository was published; the project owner keeps the full history on a local branch `dev-history`.
The reference checkouts (`resources/monomodule`, `resources/mpc-vst-machinedrum`) were submodules then and are now
local, git-ignored folders; clone them yourself to read them. `CLAUDE.md` and `.claude/` are git-ignored local files
of the owner's coding sessions.

## Workflow

Work is planned and logged with [clew](https://github.com/npatten/clew): `.clew/increments/` holds the open items
(`clew list`), `.clew/archive/` the finished ones with their decisions, measurements and verification output.
Decisions are written into the increment body before the work starts, so that whoever implements it (person or coding
agent) does not reopen them; the verification (hashes, ctest, bench) is written into it when it closes. Several
increments were implemented by Claude coding agents in isolated git worktrees and merged after independent
verification of their claims.

## Open work (see `.clew/increments/`)

- **0013 Low CPU.** Targets: an idle instance under 0.5 % of a core, a typical beat under 5 %. The big lever is an
  exact whole-engine idle skip (the engine already skips settled tracks' effects and idle voices in the DSP loop; when
  everything has settled the block is zeros), then the resampler cost, the editor repaint rate, and memory.
- **0010 Packaging** (in progress; clew shows what is left). Exists now: the build signs the AU, VST3 and standalone
  with the hardened runtime and the JIT entitlements JUCE writes (`cmake/codesign.cmake`, `MD_CODESIGN_IDENTITY`, ad-hoc
  by default), then copies them; `release/build_release.sh [-v] [-s] [-n] [-a arm64|universal]` makes a clean Release
  build, runs the tests that need no OS file, and writes `dist/Moduledrum-<v>-macos-<arch>.pkg` (AU and VST3 to
  /Library/Audio/Plug-Ins, the app to /Applications) and a `.zip` of the three bundles; `-n` notarizes and staples
  (needs the owner's Developer ID certificates and a `notarytool` keychain profile, untested here);
  `.github/workflows/build.yml` builds and tests on an Apple Silicon runner and uploads the bundles; the README has
  "Installing a release". `auval -v aumu Mdo1 Mdrm` passes on the signed build. Left: the two x64 findings above (the
  default release is arm64 until they are explained), loading the packaged build in a DAW, and the owner's certificates.
- **0009 Master effects.** The Machinedrum's master section (rhythm echo, gate box/reverb, master EQ, dynamix) runs on
  the mixer DSP (DSP1) and is not emulated; the reverb and delay sends come out as their own buses instead. Options:
  run DSP1's own program in a second emulated DSP, or translate it to C++ as the per-track chain was. The MPC port's
  HANDOFF and `docs/PROTOCOL.md` describe DSP1.
- **0006 ROM machines and factory kits** need a flash image from a real Machinedrum UW; the MPC port's `tools/mdkits`
  shows how to extract the sample memory and the 16 factory kits by booting the emulated machine.
- Not modelled at all: the sequencer (patterns, songs, parameter locks, trigger groups), audio input (INP machines),
  MIDI machines, sampling (RAM machines).
