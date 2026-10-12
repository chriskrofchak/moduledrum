---
id: 11
status: backlog
tags:
- engine
- perf
created_at: 2026-10-12T15:25:17Z
updated_at: 2026-10-12T15:25:17Z
---
## Why
The interpreter is fast enough (~18 % of a core for 16 tracks, increment 0001), so this is an optimisation. But the
dsp56300 fork's aarch64 JIT would cut that several-fold, which matters for the master effects (increment 0009) and for
Intel Macs.

## What is known (2026-10-12)
- JIT build of `md-hash` (`resources/mpc-vst-machinedrum`, scratch build without `DSP56K_NO_JIT_RUNTIME`) throws
  `voice DSP init did not complete: instruction budget exceeded at PC=$10008b with 0/1 words` after 200 M instructions:
  the DSP never sends the first HI08 flag word. PC is inside the silent-voice render ($10008f) whose timing loop
  `installHarness()` NOPs out at $100093-$100094; the harness also patches `jmp >stub` over program words and uses
  `jclr` self-loops on HI08 status.
- The MPC port's x86 reference builds always forced the JIT off (HANDOFF.md ~line 246), so the harness has probably never
  run under the JIT at all; gearmulator-md-mm runs the same DSP program under the JIT fine, but with the real
  host-interface protocol, not this harness.
- The fork's JIT unit tests pass on this Mac. The JIT config used (`dynamicFastInterrupts`, `aguSupportBitreverse`,
  `linkJitBlocks = false`) is gearmulator-md-mm's.
- Interpreter reference hash: `04ef1db4fe767721` (default md-hash pattern, no ROM samples).

## Leads
- Peripheral polling inside a JIT block: `jclr #0,x:<<$ffffc3,self` and `movep ... x:<<$ffffc7` (HI08). Check how the JIT
  treats a block that loops on a peripheral read; the interpreter returns to `exec()` every instruction, the JIT may not,
  so `runUntilTx`'s `hi.txData().size()` check never runs. `MDV_DEBUG=1` prints PC every 5 M instructions.
- `VoiceEngine::writeP` writes P memory with `m_mem->set` directly (no JIT invalidation) - used for ROM samples; not in
  this failure but must become `memWriteP` for the JIT.
- The DO-loop body at $100093 patched to NOPs: does the JIT's loop handling cope with a loop whose last-address
  instruction is a NOP?
- Compare with how Monomodule's `DspEngine` (`resources/monomodule/src/core/dsp/DspEngine.cpp`) drives its DSP under the
  JIT on macOS: it works, so its harness style is a template.

## Acceptance criteria
- [ ] JIT build boots and `md-hash` prints `04ef1db4fe767721`, also with `MD_GROUPS=2/4` and in `MD_SWEEP=1`
- [ ] Speed-up over the interpreter measured and recorded
- [ ] `MD_USE_JIT` CMake option documented; default stays OFF until the plugin's own regression test passes under both
