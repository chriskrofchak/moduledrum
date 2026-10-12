---
id: 1
status: done
tags:
- engine
- p0
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T15:25:17Z
---
## Why
Everything else builds on the engine. On Apple Silicon the dsp56300 fork uses its aarch64 JIT, a path the MPC port never validated. Prove it is bit-exact before building on it.

## Acceptance criteria
- [x] Interpreter build (`-DCMAKE_CXX_FLAGS=-DDSP56K_NO_JIT_RUNTIME`) of `md-hash` renders the fixed 16-track pattern
- [x] Same hash with `MD_GROUPS=1/2/4`
- [x] `MD_SWEEP=1`: every offered machine makes sound
- [x] Real-time factor measured for a 16-track kit
- [ ] ~~JIT build hash equals the interpreter hash~~ -> moved to increment 0011 (the JIT does not boot the harness)

## Findings (2026-10-12, macOS 26.6, M-series arm64, Apple clang 21, CMake 4.x/Ninja via Homebrew)

- OS file: `Elektron_SPS1-1UW_OS1.63.syx`, sha256 `a58cd61f2efacfb07add0c643162fb4365c30e73831f2021c17b4aa3b42cabd5` (from Elektron's zip; kept in
  `~/Downloads`, never in the repo). `mdmachine` lists its 135 machines.
- **Interpreter baseline hash: `04ef1db4fe767721`** (md-hash default pattern, no ROM samples). Identical for MD_GROUPS=1, 2, 4.
- **Speed (interpreter, one core): 12 s of 16-track audio in 2.17 s = ~18 % of real time.** The MPC port's x86 reference was
  29 % with the static recompiler. The desktop plugin can ship on the interpreter; the JIT is an optimisation, not a prerequisite.
- Sweep: every GND/TRX/EFM/E12/P-I machine peaks > 0. Silent as expected: GND-- (empty), INP* (needs audio input), MID*/CTR*
  (no audio), RAM* (sampling), ROM01-48 (no sample data without a flash image).
- dsp56300 fork facts: sd88me/dsp56300 @ 26558e54 is upstream `main` tip (edbeabc1) + 68 commits, and it **already contains
  Monomodule's patch** (`MNM patch` markers in 17 files: saturation mode, MPYRI/PFLUSH, aarch64 JIT fixes). No patch merging needed.
- The fork's own `dsp56kTestRunner`: JIT unit tests pass on arm64 macOS. The interpreter unit tests segfault in
  `DSP::execOp` (null handler) because the test runner never calls `setInterpreterEnabled(true)`, the fork's opt-in opcode cache.
  `VoiceEngine` does call it, so this is a test-runner quirk, not an engine problem.
- **JIT build fails to boot**: `voice DSP init did not complete: instruction budget exceeded at PC=$10008b with 0/1 words`
  after 200 M instructions. The DSP spins near the silent-voice render stub ($10008f) that `installHarness()` patches; the first
  HI08 flag word never arrives. HANDOFF.md shows the x86 reference builds always ran with the JIT forced off, so the
  harness has likely never run under any JIT. See increment 0011.
- `g_jitSupported` is `(HAVE_X86_64 || HAVE_ARM64) && !DSP56K_NO_JIT_RUNTIME` (`dspconfig.h`); our CMake must define
  `DSP56K_NO_JIT_RUNTIME` on the emulator and on every consumer of its headers (the constant is inline in `dsp.h`).
- Scratch builds used: `build-md` (JIT) and `build-md-interp` in the session scratchpad; nothing in the repo yet.

## Tasks
- [x] Build mdcore/md-hash/mdmachine natively (done during research, no source changes)
- [x] Get Machinedrum OS 1.63 `.syx` from the user
- [x] Interpreter build in a second build dir, compare hashes
- [x] Record the baseline hash for the project's own tests (see above)
