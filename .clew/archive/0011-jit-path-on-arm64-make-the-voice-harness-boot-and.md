---
id: 11
status: done
tags:
- engine
- perf
created_at: 2026-10-12T15:25:17Z
updated_at: 2026-10-12T18:46:38Z
---
## Why
The interpreter is fast enough (~18 % of a core for 16 tracks, increment 0001), so this is an optimisation. But the
dsp56300 fork's aarch64 JIT would cut that several-fold, which matters for the master effects (increment 0009) and for
Intel Macs. The user: "Speed is everything."

## Result (2026-10-12): the JIT is the default, bit-exact with the interpreter, ~4x less CPU
Done by an Opus agent (cut off by a spend limit while writing its notes) and finished and verified by the coordinator.

### Root cause of the boot failure
The voice program keeps ordinary code in the interrupt-vector area: its init at P:$24 and the whole block loop at
$64-$e5. Without `JitConfig::interruptRegionIsCode` the JIT compiles everything below $100 as 2-word fast-interrupt
blocks; there `JitOps::pushPCSR` pushes the PC register rather than the return address and an unconditional branch's
block never sets PC before its op runs. Init's `jsr >$100024` at $3b pushed $3b, its `rts` came back to the `jsr`, and
init ran forever ("instruction budget exceeded at PC=$10008b"). `interruptRegionIsCode = true` (Monomodule's own
setting, for the same reason) fixes the boot. `VoiceEngine::configureJit()` documents the whole configuration.

### Three JIT bugs found by lockstep, fixed in `ext/patches/0001-dsp56300-md.patch`
Applied to `ext/dsp56300` in place at configure time by `cmake/dsp56300.cmake` (idempotent, versioned by sha256; the
submodule then shows as modified, which is expected). Each has a unit test in `tests/test_jit.cpp` (ctest `jit`, skipped
with `MD_USE_JIT=OFF`) that fails without the fix:
1. `jitblock.cpp`: a REP and its repeated instruction could be split across two blocks; the repeated instruction then
   ran once more. Now they stay in one block, or the block ends before the REP.
2. `jitdspregpoolregpair.cpp`: a 48-bit read of x/y cleared the written flags of x0/x1/y0/y1, dropping a partial write
   made earlier in the block. Changed the audio of 5 of the 64 preset kits.
3. `jitops_alu_aarch64.cpp` / `_x64.cpp`: ASL by zero set C for negative values on aarch64; multi-bit ASL never set L
   with V on either back end.
These are upstream dsp56300 bugs; worth reporting to dsp56300/dsp56300 and shnolk/monomodule.

### Harness changes (`src/core/md/VoiceEngine.{h,cpp}`)
- `configureJit()`: `interruptRegionIsCode`, `dynamicFastInterrupts`, `aguSupportBitreverse`, `linkJitBlocks = false`
  (linking measured no faster), and one-instruction JIT blocks at the two flag stubs (`kSkipNormal`, `kSkipSilent`) so
  `exec()` returns right after each flag's `movep`, as the interpreter does: per-voice instruction counts (used by the
  voice-group balancing) equal the interpreter's.
- `writeP()` goes through `memWriteP` (JIT and opcode-cache invalidation); a sticky JIT allocation failure becomes an
  engine fault instead of a hang.
- `MD_JIT_LOCKSTEP=1` (JIT builds): a shadow copy of each voice DSP runs on the interpreter, fed the same host writes;
  after every JIT block the registers (minus SR's E/U/N, see below) and every host word must match. The first
  difference is reported on stderr and becomes `faultReason()`; the render carries on with the JIT alone. ~2x the
  interpreter's time: for checking JIT changes, never for playing.
- Interpreter quirk, not fixed (it is the reference): `CLR` sets E/U/N but keeps the previous ALU op's lazy CCR update
  pending, so E/U/N read later are that op's. The JIT is right; the voice program never reads them in between.
- `md-voicediag` (src/cli): disassembles the voice program as the harness sets it up, or steps it from entry one line per
  `exec()`.

### Verification (coordinator, in the agent's worktree, OS 1.63, M-series)
- `md-hash`: `hash 04ef1db4fe767721` on the JIT, also `MD_GROUPS=2` and `4`, also with `MD_JIT_LOCKSTEP=1` (no
  difference reported). `MD_SWEEP=1`: all 135 machines' peaks identical to the interpreter's sweep.
- ctest in the JIT build: romart, firmware, golden-hash, kits (kit-0 hash `f5c618b7df5550fa`), jit, resampler,
  plugin-os-load, plugin-render (`a28b9dc178776939`) all pass. plugin-state / plugin-editor are checked on `main` after
  the merge (they post-date this branch).
- Speed: `md-hash` 0.60 s vs 2.17 s on the interpreter (12 s of audio; voice DSP 0.40 s vs 2.03 s).
  `md-plugin-check --bench 20`: engine alone 7.4 % of real time (interpreter ~31 %); processor 7.3 / 7.6 / 7.7 % at
  44.1 / 48 / 96 kHz, worst callback 404 / 393 / 329 us against budgets of 1451 / 1333 / 667 us, 0 over budget.

## Acceptance criteria
- [x] JIT build boots and `md-hash` prints `04ef1db4fe767721`, also with `MD_GROUPS=2/4` and in `MD_SWEEP=1`
- [x] Speed-up over the interpreter measured and recorded (~3.6x on md-hash, ~4.2x on the plugin bench)
- [x] `MD_USE_JIT` CMake option documented; default ON now that the plugin's regression render passes under both

## Follow-ups
- Intel x86_64: the x64 JIT has the same patch (fix 3's L flag, fix 1 and 2 are back-end independent) but was not run;
  check `md-hash` on an Intel Mac or under Rosetta before a universal build (increment 0010).
- Report the three JIT bugs upstream.
- Increment 0013 (low CPU) now starts from ~7 % instead of ~32 %.
