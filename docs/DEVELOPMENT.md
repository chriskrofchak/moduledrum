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
| Kit 0 through the processor (after its 16 warm-up blocks), and after a state save/restore; `master_fx` off | `da62047690565e38` | ctest `plugin-state`, `src/plugin/Check.cpp` |
| The plugin's regression render, chromatic note map; `master_fx` off (the dry main bus) | `a28b9dc178776939` | ctest `plugin-render` |
| The same render, the hardware's white-key note map; `master_fx` off | `14291ff9302212d4` | ctest `plugin-render` |
| The plugin's regression render with `master_fx` on (the main bus is the master output, latency removed): chromatic / white keys | `ece6201f17cf1b81` / `7fd32e31f89ea574` | ctest `plugin-master` |
| Kit 0 through the processor with `master_fx` on = the engine's master output (`applyKit`, the kit's master bytes, `primeMaster` after 16 warm-up blocks; the sends 18 frames behind it) | `288d02a442ac6b0b` | ctest `plugin-master` |
| JIT = interpreter | all of the above identical with `-DMD_USE_JIT=OFF` (the master-on plugin hashes too); `MD_SWEEP=1 md-hash` identical per machine | ctest `jit`, `MD_JIT_LOCKSTEP=1` |
| The master effects' output of the golden pattern (`MD_MASTER=dry\|wet`, fixed synthetic master bytes in `GoldenPattern.h`) | dry-ish `fe784d9f02fa30b2`, wet `9b8a53ca6fae15fe`; the cleared kit's bytes (`MD_MASTER=1`) `3c37e206683bf50e`; with the master bytes of preset kits 0 (TRX), 1 (EFM), 9 (BOOGIE), 13 (ALIENS): `5f4b45659f477ab7`, `1222389a11b38fe6`, `2abcba9c5e9bf547`, `b892b2c5f5a1e43b`. The dry hash above is unchanged next to them, and all of them are identical on the JIT and the interpreter | ctest `golden-master`, `md-hash` with `MD_MASTER=` |
| Master section against its references | JIT = interpreter on 3200 random blocks (0 of 16.7 M words differ); master-only = DSP1's whole main loop (`tests/Dsp1Reference`) on 3 kits x 4000 blocks (0 of 2.9 M); the OS's 38 parameter words (test vector, the 16 preset kits' checksum `3f50575c608c9c35`); a live block takes < 25 us and allocates nothing; a booted DSP stays under 200 MB | ctest `master`, `master-host`, `master-ram` |

```bash
MD_OS_SYX=/path/to/Elektron_SPS1-1UW_OS1.63.syx MD_KITS_SYX=/path/to/MD_presets_1_50.syx \
  ctest --test-dir build --output-on-failure
```

Useful tools (all in `build/src/cli` or `build/src/plugin/MdPluginCheck_artefacts/Release/MdPluginCheck`):

- `md-hash <OS> [ROM_SAMPLES.bin]`: the golden pattern's hash. `MD_SWEEP=1` plays every machine and prints its peak
  (the release gate: none of the playable ones silent); `MD_TIMING=1` prints the render's stages; `MD_JIT_LOCKSTEP=1`
  (JIT builds) runs an interpreter shadow of each voice DSP and stops at the first register that differs.
  `MD_MASTER=1|dry|wet|kit:N` runs the master effects too (a second emulated DSP behind the mixer; N = a kit of
  `$MD_KITS_SYX`) and prints `master hash` next to the dry hash, which does not move.
- `md-machines <OS>`: the OS machine table (135 machines: id, name, SYN labels).
- `md-kits list|render`: the kits of a `.syx`, and one kit rendered to a WAV (each track solo, then all); with
  `--master`, `--ab`, `--pattern`, `--fx` it renders the master effects too (see "Listening pack" below).
- `md-voicediag`: the voice DSP program as the harness sets it up, disassembled or stepped.
- `md-plugin-check --check-os-load | --check-render | --check-state | --check-master | --check-editor | --check-rt |
  --snapshot <dir> | --bench 20`: the plugin headless. `--bench` is the CPU figure quoted below (all 16 tracks
  retriggered 8x per beat at 120 BPM, 64-sample blocks) and checks that no callback goes over budget. `--check-rt` (ctest
  `plugin-rt`) counts every heap allocation in `processBlock` (macOS `malloc_logger` plus a replaced `operator new`) and
  fails on any outside dsp56300's JIT compiles; `MD_RT_ALLOC_TRACE=1` prints the call stacks. `--check-master` (ctest
  `plugin-master`) is the master effects in the plugin (increment 0009).

**Listening pack.** The master section cannot be compared with hardware by automation, so it is judged by ear: A/B renders
of the owner's own OS and kits, made locally and never committed. `md-kits render --pattern beat <kit> <out.wav>` writes
the dry mix of a built-in beat (eight bars at 120 BPM: kick on 1 and 3, snare on 2 and 4, hats in eighths, two fills, a
crash and a kick at the end, then a silent tail); `--master` writes the master output of the same pattern (the kit's 32
master bytes applied and primed) and `--ab` writes every bar twice, dry and then master, to compare back to back. In a
pattern render the dry mix is delayed by the master section's own latency (`master::kDryLatency`), so the dry and master
files line up sample for sample. `--pattern echo-glide|reverb-tail|eq-gain` are the demonstrations (echo TIME and tempo
changed mid-way, one hit into the gated reverb, EQ GAIN changed mid-way), `--fx INDEX=VALUE` overrides a master byte, and
each render prints the peak, RMS and tail length of dry and master. The pack is those calls over the 16 preset kits; the
options are documented in the header of `src/cli/md_kits.cpp`.

## Established facts and measurements (Apple Silicon, macOS 26, Apple clang 21)

- **The emulated DSP runs on dsp56300's JIT, bit-exact with its interpreter.** `MD_USE_JIT` defaults to ON; an
  existing build directory keeps its cached value, so pass `-DMD_USE_JIT=ON` once. OFF defines
  `DSP56K_NO_JIT_RUNTIME` on every target (the constant `g_useJIT` is in the emulator's headers, so all targets must
  agree) and is the reference the JIT is checked against.
- **CPU:** `md-hash`'s 12 s of 16-track audio renders in 0.6 s on the JIT (2.2 s on the interpreter). The plugin bench
  (`md-plugin-check --bench 20`, increment 0009), as it ships with the master effects on: **8.8-9.0 / 9.1 / 9.2-9.4 % of
  one core at 44.1 / 48 / 96 kHz** with all 16 tracks hammered (three runs), 99.9th percentile callback ~280 µs against a
  1451 µs budget, none over it in two of the three runs (the third, with another process spinning a core, had 1 of 13091
  at 1.6 ms: preemption; its 99.9 % was 280 µs); **7.2 % with the master off** (before the master: 7.2 / 7.5 / 7.7 %;
  interpreter: ~33 %). The engine alone with the master: 8.8-9.0 %. An idle instance still runs the whole DSP (both
  DSPs with the master on) every block (open work 0013).
- **Memory:** a booted emulated DSP takes about 90 MB (JIT; ~110 MB on the interpreter, whose opcode cache is 100 MB of
  it), measured as `task_info` `phys_footprint`: a plain engine 87 MB, with the master effects (a second DSP) 303 MB. It was
  358 MB per DSP (the 420-500 MB per instance quoted before; the MPC port trimmed some of it) until two defaults of the
  emulator were removed (increment 0009): `MmuHelper::mapRegion` `mlock()`ed every alias of the DSP's address space, 192 MB of
  wired pages for ~40 MB of backing store (`ext/patches/0002-dsp56300-md-memory.patch`, applied at configure time by
  `src/core/CMakeLists.txt` beside 0001), and the ESSI ports' audio ring buffers, 80 MB per DSP that nothing here uses
  (`DSP56300_AUDIO_RINGBUFFER_SIZE=64`, the emulator's own switch, defined PUBLIC on `dsp56kEmu`). ctest `master-ram`
  guards it. What is left is mostly the JIT's block chains: one per DSP mode (the combination of addressing modes the
  program has set in the AGU registers), each an array of 8 bytes per P address up to the highest P word written, 10.6 MB for
  the master program, which goes through 17 modes in one pass (~130 MB of the engine-with-master's 303). The dispatch
  (`DSP::execJit`) indexes that array unchecked, so shrinking it needs a bounds check in the JIT's hot path: not done.
- **Master effects, engine side (increment 0009):** the rhythm echo, gate box reverb, master EQ and dynamix are the mixer
  DSP's (DSP1) own program, master section only (P:$342-$9a7, 1255 instructions, 10 295 per 32-sample block), unchanged,
  on a second emulated DSP (`MasterFx`) behind the native `TrackFx` and `Mixer`. A harness parks DSP1's boot loop behind
  a stub and starts each block at $342; no word of the master code is patched. The 32 kit bytes (`HostModel::setMasterFx`) go
  through the OS's own smoothing and parameter code (the tail of its tick routine, $20b4a2-$20ba4c, one of four groups
  per tick, run by `MachineRunner::callUntil`) into the 38 words at Y:$150-$18c, so the hardware's cadence and quirks
  stay (a smoothed value ends 3 below a target it approaches from below; a TIME or tempo change glides the echo).
  `EngineT` owns the optional `MasterFx` (on by default when the OS has the section; `EngineOptions{false}` to leave it
  out), its output is `Engine::Output::master`, and the dry outputs, their hashes and the plugin's are untouched: it is an
  extra stage after the mixer. Checked: JIT = interpreter, and master-only = DSP1's whole main loop, word for word (table
  above). Cost: **11.8 us a block** on the JIT (`md-hash`: 590 -> 800 ms per 12 s of audio, 4.9 -> 6.7 % of real time;
  74-81 us on the interpreter build), ~90 MB, 55 ms to build (6 ms boot, then a warm-up that compiles every JIT block (672 entry points) over
  synthetic signals and puts DSP memory back, so that a live block compiles and allocates nothing). The pass is not
  level-neutral against the dry bus (dry goes through EQ, dynamix and GAIN too; the cleared kit is about -2.6 dB) and the
  dry path comes out `master::kDryLatency` = 18 frames late.
- **Master effects, plugin side (increment 0009):** the 32 bytes are host parameters (`fx_rev_dvol` .. `fx_dyn_mix`) fed to
  `HostModel::setMasterFx` at each block (an edit glides, as on the MD); a kit load, a restored state and a new engine
  set a flag that makes the next block call `EngineT::primeMaster()` after the parameters and the tempo (allocation-free,
  `plugin-rt`). With `master_fx` on the main bus is `Output::master` and every other stream (sends, track buses) goes
  through an 18-frame delay line (fed in either mode, so switching finds it full); the converter then reads the streams
  at `(m - L) x 44100 / rate + 18` with `L = ceil((35 + 1 + 18) x rate / 44100)`, so the output is exactly L host samples
  late and the reported latency a whole number: 18 / 59 / 118 samples at 44.1 / 48 / 96 kHz (0 / 40 / 79 with the master
  off). Checked (ctest `plugin-master`): a GND-IM impulse lands on the same engine frame on the main bus, a track bus and
  both sends; with the master on the track and send buses are the master-off render's sample for sample; 48 and 96 kHz
  are the 44.1 kHz master render through the converter (max difference 0); the master DSP compiles no JIT block on a
  live callback (672 before and after a 4.5 s render; the first live callback after an OS load takes ~250 µs). Off: the
  master pass is bypassed (`masterBypass`), the old path exactly.
- **Latency:** 0 samples at 44.1 kHz, 40 at 48 kHz, 79 at 96 kHz (the resampler), reported to the host; with the master
  effects on (the default) 18 / 59 / 118 (their dry path's 18 engine frames and the resampler, as one delay).
- **No heap allocation on the audio thread** (increment 0014, ctest `plugin-rt`), JUCE's MIDI handling included, except
  dsp56300's JIT compiling a voice-program block the first time the program reaches it (~500 blocks, ~5900 mallocs,
  in the first dozen callbacks after an OS load; a new machine or a rare path compiles a few more later). Before 0014
  the engine glue made ~2,750 allocations a second (`MachineRunner::call` copied its arguments into a vector).
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
4. `jitops_alu_x64.cpp` (x86-64 only): LSL/LSR did not clear a stale Z (their CCR batch covered N, C and V but not Z).
   Found by lockstep under Rosetta (increment 0018); tests `jit_lsl_after_zero_clears_z` / `jit_lsr_after_zero_clears_z`.

These are upstream bugs (dsp56300/dsp56300) and affect Monomodule too; they have not been reported upstream yet.

### The x64 back end (increment 0010: first runs, under Rosetta 2)

`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds the plugins, the app and the tools; dsp56300 and asmjit pick the
JIT back end by compiler macro, so the x86_64 slice runs the x64 JIT, with the same patch. There is no Intel Mac at
hand; the slice ran under Rosetta on Apple Silicon (`arch -x86_64`). An arm64 CMake's `ctest` cannot be started with
`arch`, so the tests were started with `-DCMAKE_CROSSCOMPILING_EMULATOR="arch;-x86_64"`, which makes CMake prefix every
test command. OS 1.63:

- `md-hash`: `hash 04ef1db4fe767721`, also with `MD_GROUPS=2` and `4`.
- ctest, one run at a time: all 10 pass on the x86_64 slice (romart, firmware, golden-hash, kits, jit, resampler,
  plugin-os-load, plugin-render, plugin-state, plugin-editor).
- **Finding 1 (lockstep): a real x64 JIT bug - now fix 4 of the patch, with its tests (increment 0018).**
  `MD_JIT_LOCKSTEP=1 md-hash` reports a first difference that the aarch64 JIT does not have: after JIT block 136872
  (P:$1038c0 to $1038ca, instruction 5596681) `sr jit=80044 interp=80040`, i.e. Z is set on the x64 JIT. The block is a
  `DO #$22` loop in one machine's set-up code: per iteration two table words are loaded and each is shifted left by 12
  with LSL; all other registers agree and the last shifted values are not zero, so Z should be clear. Cause: `alu_lsl`
  and `alu_lsr` in `jitops_alu_x64.cpp` (lines 144 and 168 of the patched file) open a `CcrBatchUpdate(N | C | V)`, which
  clears those bits up front and switches `ccr_update()` to OR-only, but both set Z with `ccr_update_ifZero(CCRB_Z)`:
  a Z = 1 from an earlier zero word is never cleared by a later non-zero result. With `CCR_Z` added to both masks
  the x86_64 lockstep runs through the whole golden pattern without a difference (hash unchanged), and
  `MD_SWEEP=1 MD_JIT_LOCKSTEP=1` over all 135 machines prints exactly the arm64 sweep with no lockstep report. To do:
  put the two-line change into `ext/patches/0001-dsp56300-md.patch` and add a case to `tests/test_jit.cpp` (LSL and LSR
  of zero, then of a non-zero value, JIT against interpreter; it fails on x64 without the fix, passes on arm64
  either way), then run it under Rosetta. The golden hash was never affected (the voice program does not read Z there).
- **Finding 2 (crashes) was not the JIT, nor x64, nor Rosetta: the plugin checks cannot run twice at once.** The first
  full Rosetta ctest run, made while other sessions were using the Mac, died with SIGSEGV in plugin-render and
  plugin-state, and 8 parallel copies crashed `--check-render` in 21 of 24 runs. The same 8 parallel loops crash the
  native arm64 build (`--check-render` 21 of 24, `--check-state` failing 3 times) and an interpreter-only x86_64 build
  alike. Cause: every check keeps its settings in a fixed folder (`md-plugin-check-render`, `-state`, ... under JUCE's
  temp directory, `~/Library/Caches/<executable name>` on macOS) and deletes it at the start and the end, so two runs
  delete each other's OS-file setting; `render` then reports "no engine: No Machinedrum OS file selected" and
  `checkRender` goes on to read the first channel of an empty result (null dereference; lldb on the native build
  stopped in `checkRender`). Eight parallel loops with each its own copy of the executable (so its own folder): 48 of 48
  pass. A user's real settings are never touched (`MD_SETTINGS_DIR`). Fixed in increment 0020: every check run keeps
  its settings in a folder of its own (`mkdtemp` in that temp directory, removed when the check returns), and a check
  without an engine fails with a message and exit code 1 instead of reading an empty render. The same 8 parallel
  loops now pass 24 of 24 for `--check-render` and for `--check-state` (`tests/stress_checks.sh`).
- **Caution.** Attaching lldb to the x86_64 slice hung (0 % CPU, debugserver under Rosetta); after it was killed
  Rosetta stopped starting binaries it had not seen before (a freshly compiled hello world too: processes stuck in
  the kernel at exec, state U, which `kill -9` does not end). It cleared by itself after about 40 minutes; whether the
  lldb session caused it is not known. Native lldb on arm64 is fine.

So the repo's x86_64 slice is still opt-in (the release script builds arm64 unless told `-a universal`, the README says
so): the Z fix is in the patch and verified under Rosetta (the tests fail without it, the lockstep render is clean), but
there has been no Intel hardware. Once a real Intel Mac has run the full ctest, the universal build can become the default.

## Linux and Windows (increment 0021, 2026-10-12)

Monomodule ships Linux and Windows with the same emulator and the same JUCE shell, so Moduledrum follows its recipe.
Target architectures: arm64 (Apple Silicon, Linux aarch64, Windows on ARM as an experiment) and x86-64 (Intel Mac,
Linux, Windows 10 1803+). 32-bit ARM would be interpreter-only (dsp56300 has no A32 JIT; the MPC port used a static
recompiler we left behind) and anything else has no JIT back end at all: out of scope.

- **Nothing in the engine is platform-specific.** The only platform code was in the development tools: the allocation
  counter (`malloc_logger`, Apple only, still so) and `md-plugin-check`'s settings folder (`mkdtemp`/`setenv`, now a
  JUCE random folder and `_putenv_s` on Windows; `AllocCounter.cpp` uses `std::thread::id` and `_aligned_malloc`).
- **MSVC** (as Monomodule): `_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS`, `/utf-8 /bigobj`, a static C runtime
  (`CMAKE_MSVC_RUNTIME_LIBRARY`, policy CMP0091 defaulted NEW for dsp56300's sub-projects), and
  `/w /permissive- /constexpr:steps10000000` on the emulator libraries where the others get `-w`. `MD_OS_SYX` is passed
  through `file(TO_CMAKE_PATH)` so a Windows path's backslashes do not become escape sequences in the string literal.
- **The emulator on each platform:** memory is mapped through `MmuHelper` (`shm_open` + `mmap` on Linux and macOS,
  `VirtualAlloc2` with a legacy fallback on Windows, so Windows 10 1803+); the JIT's executable memory is asmjit's
  (needs the JIT entitlements under macOS's hardened runtime, nothing elsewhere). On Linux `/dev/shm` must hold the
  DSPs' memory: Docker needs `--shm-size=1g`. The x86-64 builds run the x64 back end with fix 4 of the patch; the
  aarch64 builds the back end every hash here was established with.
- **MSVC found a second one, in the emulator fork:** `dsp.h`'s arm32 static-recompiler helpers (`callInstruction`,
  `getRecompInfo`, the MPC port's work, unused here) rebuild a member pointer by copying an Itanium `{ptr, adjustment}`
  pair into it and asserted that layout; MSVC's member pointer to a class without bases is one pointer, so every file
  including `dsp.h` failed (`error C2338`). `ext/patches/0003-dsp56300-md-msvc.patch` copies `sizeof(member pointer)`
  bytes instead, right on both ABIs; `src/core/CMakeLists.txt` applies it at configure time as it applies 0002.
- **The `windows-11-arm` runner image moved to Visual Studio 2026** in September 2026 (actions/runner-images#14602), so
  that job uses CMake's "Visual Studio 18 2026" generator; `windows-2022` keeps Visual Studio 2022.
- **CI** (`.github/workflows/build.yml`): macos-14 universal, ubuntu-24.04, ubuntu-24.04-arm, windows-2022 (x64),
  windows-11-arm (`continue-on-error`). Each runs ctest (the OS-file tests skip) and uploads
  `Moduledrum-<version>-<platform>-<arch>.zip` made by `cmake/stage_release.cmake`: `VST3/`, `Standalone/`, `AU/` on
  macOS, `Tools/md-hash`, licences, `BETA-TESTING.md`. A `v*` tag attaches them to a draft release.
- **The beta check** (`docs/BETA-TESTING.md`): a tester runs `md-hash` with their own OS file and reports whether it
  prints `04ef1db4fe767721`; `MD_JIT_LOCKSTEP=1` gives the first diverging instruction otherwise. The first Windows or
  Linux x64 report with the right hash is what closes 0018's "real x86-64 hardware" task.
- **The Linux build found one real bug:** `cmake/mc68k.cmake` put the mc68k checkout itself on the include path, and
  its `endian.h` shadowed glibc's `<endian.h>`: JUCE's C sources (SheenBidi) failed to compile, and every C++ file that
  got the directory silently read the wrong header too. Now only the checkout's parent is on the path
  (`mc68k/mc68k.h`, `mc68k/Musashi/m68k.h`), and 68kEmu is a private dependency of mdcore (only `MachineRunner.cpp`
  includes it).
- **Verified:** macOS arm64 builds, 16/16 tests with the OS and kit files, `md-hash` `04ef1db4fe767721`, the staging
  zip complete; Linux aarch64 in an Ubuntu 24.04 container (GCC 13.3, Docker on this Mac) the same suite; on CI the
  macOS universal, Linux x64 and Linux arm64 jobs pass. Windows compiles only on CI; the two fixes above came out of its
  first run, and the next push shows whether MSVC has more to say.

## Decisions (made, not to be re-litigated without a reason)

- **Render synchronously in `processBlock`** (Monomodule's pattern), not on a render-ahead thread. The engine is fast
  enough and the latency is lower. Engine swaps (OS load) happen under a lock that `processBlock` only tries.
- **Engine rate 44.1 kHz**, the hardware's; other host rates go through Monomodule's band-limited resampler.
- **Buses:** Main (stereo) + Track 1-16 (mono, off by default) + Reverb send + Delay send (stereo). A track on its own
  bus leaves the dry main mix but still feeds the sends; never use `HostModel::setRouting` (the hardware's outputs A-F
  land in `Mixer::Output::frame`, which no bus plays) - use the processor's `setTrackOutput`. With `master_fx` on (the
  default) Main is the master section's output and the track and send buses (the dry sends, before the master) are
  delayed 18 engine frames to stay in step with it; the latency includes them. Off: the dry mix, no delay.
- **MIDI note map:** the hardware maps tracks 1-16 to the white keys from C2 (36 38 40 41 43 45 47 48 50 52 53 55 57
  59 60 62; MD manual appendix B). That is the default; `note_map` = chromatic gives 36-51 and 0-15 together (the MPC
  port's conventions). `params::trackForNote` / `noteForTrack` in `src/plugin/one/OneParams.h` are the one place.
- **Host parameters:** 533, raw 0-127 integers with the hardware's names, ids = the MPC port's keys
  (`track0_machine`, `track0_syn1` ... `track0_level`, `track0_lfo_shp1` ..., `tempo`, `midi_channel`, `accent`,
  `note_map`), then ours for the master effects (`master_fx`, `fx_rev_dvol` .. `fx_rev_lev`, `fx_echo_time` ..
  `fx_echo_lev`, `fx_eq_lf` .. `fx_eq_gain`, `fx_dyn_atck` .. `fx_dyn_mix`, in the kit's order), groups T1-T16 + Global
  + Master FX (sub-groups REV, ECHO, EQ, DYN), append-only once released. SYN names follow the machine. A machine change
  gives the engine the machine's SYN defaults at once; the SYN parameters follow on the message thread (30 Hz timer) and
  the host is told names changed. Restore order: machine before SYN. The machine parameters are meta parameters (AU
  `IsGlobalMeta`, increment 0019: `auval -oop` requires it of a parameter that rewrites others); no other one is.
- **State:** schema 3 = all parameters + the current kit as a kit dump (`encodeKit(parametersToKit())`), no OS path.
  The loaded `md::library::Kit` is the canonical in-memory form. A schema 2 state (no master parameters) restores the
  init kit's master bytes and `master_fx` on: the session was made without the master, so it gets the plain settings
  and not those of the kit dump it carries (decision 7 of increment 0009).
- **Kits:** `src/core/md/library` decodes and re-encodes `$52` kit messages byte for byte (all 64 of the preset file
  round-trip). Routing is not in a kit (it is in the `$50` global dump); free slots are kits whose first name byte is
  `$7f`. The master-effect blocks are four blocks of 8 bytes in the OS's own order (reverb, echo, EQ, dynamix; the names in
  `MdSysex.h` are confirmed by what the OS does with each byte, increment 0009); the engine runs them
  (`Kit::masterFx.bytes()` into `HostModel::setMasterFx`; the library's `applyKit` leaves them out so that the engine's
  dry tests stay as they are, the plugin's `loadKit` sets them through its parameters and primes the master). Kits live
  in `~/Library/Application Support/Moduledrum/Machinedrum/kits/`, one bank per file.
- **Editor:** everything inside the LCD is drawn on a 1-bit canvas with the OS file's own fonts and dials (nothing
  firmware-derived is committed; a built-in stand-in face is used without an OS). Layout: SYN | AMP/EFX over ROUTE |
  LFO per track, a 16-track strip, a GLOBAL tab (mixer, settings with the MASTER on/off switch, kit browser), a MASTER
  tab (ECHO | REV over EQ | DYN: the hardware's page order, while the parameters keep the kit's storage order). The
  artwork reader trusts the OS 1.63 addresses only after checking the ColdFire section's size and hash.
- **The processor must never take the engine lock from the message thread for display** (the old status line did, and
  the audio thread dropped blocks); the editor reads atomics.
- **Packaging:** releases are arm64 until the x64 JIT has run on real Intel hardware (the universal build exists and is opt-in; its JIT is fixed and lockstep-clean under Rosetta). The build
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
  "Installing a release". `auval -v aumu Mdo1 Mdrm` passes on the signed build. Left: an Intel-hardware run before
  the default release becomes universal, loading the packaged build in a DAW, and the owner's certificates.
  `auval -oop` passes too (increment 0019, the machine parameters' meta flag; checked on a scratch copy under another id).
- **After 0009 (master effects, done and archived)**, optional: the idle skip of the master pass (it runs every block:
  echo and reverb feedback, a free-running modulation phase) belongs with 0013; a C++ translation (B, with
  `tests/Dsp1Reference` as the oracle) only if RAM or CPU ever ask for it; the control machines (CTR-RE/GB/EQ/DX, which
  drive the master parameters from a track) are not modelled; the x64 JIT has never run DSP1's program.
- **0021 Linux and Windows** (in progress): the code, CI matrix, staging script and tester guide are in; left is the
  first CI run on a push, a Windows 10 x86-64 tester's `md-hash` report, and then flipping `build_release.sh -a` to
  universal (0018).
- **0006 ROM machines and factory kits** need a flash image from a real Machinedrum UW; the MPC port's `tools/mdkits`
  shows how to extract the sample memory and the 16 factory kits by booting the emulated machine.
- Not modelled at all: the sequencer (patterns, songs, parameter locks, trigger groups), audio input (INP machines),
  MIDI machines, sampling (RAM machines).
