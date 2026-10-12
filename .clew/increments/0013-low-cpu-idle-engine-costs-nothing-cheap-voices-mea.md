---
id: 13
status: backlog
tags:
- perf
- engine
created_at: 2026-10-12T16:59:14Z
updated_at: 2026-10-12T16:59:14Z
---
## Why
The user wants Moduledrum to be light on CPU (2026-10-12): many instances in a project, laptops on battery. Today
(interpreter) 16 tracks cost ~18 % of a core on a sparse pattern and ~32 % with all tracks constantly retriggered; an
idle instance still runs the whole DSP every block.

## Targets (measure with `md-plugin-check --bench`, add an idle bench)
- Idle instance (no notes for > 1 s, all tails silent): < 0.5 % of a core.
- Typical beat (4-6 tracks active): < 5 % with the JIT (increment 0011), < 15 % on the interpreter.
- No callback over budget at 64-sample blocks at 44.1/48/96 kHz (keep the bench's 99.9th percentile check).

## Leads
- **JIT** (increment 0011): the biggest single win; the MPC port's static recompiler was 2.1-3.8x over the interpreter.
- **Whole-engine idle skip (exact):** the engine already skips settled silent tracks' effects (`EngineT::fxTrack`,
  `skipSettled`) and idle voices in the DSP loop (`kSkipStub`). When every track is idle and every track-effect state has
  settled, the whole block is zeros: skip the DSP and the mixer entirely, keep the tick counter / LFO phases advancing
  cheaply (or document that LFO phase restarts on the next note; TRIG-type LFOs restart anyway). Must keep the regression
  hashes (`04ef1db4fe767721`, `a28b9dc178776939`): an exactness test that compares skip on/off over a pattern with gaps.
- **The master effects** (increment 0009, step 7b moved here): with `master_fx` on (the default) a second emulated DSP
  runs DSP1's master pass every block, ~12 us (bench: 9.0 % with it, 7.2 % with it off at 44.1 kHz), whether anything
  plays or not. An idle skip of it is not exact-trivial: it has feedback (echo, reverb) and free-running state (the
  echo's write phase Y:$159 advances every block, the modulation phase), so it needs an analysis of which state words are
  time-shift invariant once the rings have decayed to zero, and a test against the unskipped pass (`tests/Dsp1Reference`,
  ctest `master`). `masterBypass` already skips it while `master_fx` is off. Its memory: ~215 MB per engine (mostly the
  JIT's block chains, archive 0009 section 10).
- **Silence release** exists in `HostModel::setSilenceRelease` (MPC port, off by default, not bit-exact: a freed voice
  restarts fresh). Consider it as a user option ("eco mode"), never the default.
- **Resampler cost** at 48/96 kHz: measure; Monomodule's converter is band-limited and may dominate when the engine is idle.
- **Editor repaint**: the LCD canvas should repaint only on value changes / at a low rate, not per audio block.
- **Memory**: ~420-500 MB per instance (the emulator's P/X/Y memory and interpreter opcode cache). The MPC port trimmed
  P memory ("about 225 MB less RAM"); see HANDOFF.md "Memory". Share read-only firmware tables between instances.

## Acceptance criteria
- [ ] Idle bench added and the idle target met
- [ ] Whole-engine idle skip proven exact by test
- [ ] Typical-beat figures recorded in CLAUDE.md for interpreter and JIT
