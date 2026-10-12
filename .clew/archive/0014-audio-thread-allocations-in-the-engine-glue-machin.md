---
id: 14
status: done
tags:
- bug
- p1
created_at: 2026-10-12T19:11:00Z
updated_at: 2026-10-12T19:42:52Z
---
## Why

The 2026-10-12 audit (two independent subagents, both confirmed against the code) found two heap allocations on the
plugin's audio thread, below the processor boundary. `OneProcessor.h`'s comment that nothing on the render path locks
or allocates is false below the engine.

1. `src/core/md/MachineRunner.cpp:141` — `MachineRunner::call` does `std::vector<uint32_t> args(_args);` on every
   call. Reached from `processBlock → renderEngineBlock → Engine::render → HostModel::renderBlock → tick()` (every 11th
   32-frame block, ~125 Hz): 16 `compute()` calls + 4 direct calls (`kSmooth`, `kLfoOsc`, `kLfoApply`, `kLevelSmooth`)
   per tick, two more per trigger (`kLfoWave`, `kLfoApplyOne`). About 2,000 malloc/free pairs a second on the RT thread.
2. `src/core/md/HostModel.cpp:121` — `m_activeOrder.push_back(_track)` in `HostModel::trigger`; the vector is never
   reserved, so the first ~5 distinct-track triggers after each OS load reallocate. `erase` keeps capacity, so it is
   steady-state afterwards.

Also noted, not in scope here: `m_keyboard.processNextMidiBuffer` (`OneProcessor.cpp:736`) takes the
`MidiKeyboardState` CriticalSection every block (standard JUCE idiom; leave unless it shows up in the bench).

## Tasks

- [x] `MachineRunner::call`: push the arguments straight from the `initializer_list` in reverse (`std::rbegin/rend`
      work on an initializer_list since C++14) or via a fixed `std::array`; no heap.
- [x] `HostModel` ctor: `m_activeOrder.reserve(kTracks)`; add a comment that `trigger` must not grow it past 16.
- [x] Regression guard: a malloc hook (macOS `malloc_zone_t` hooks or a `malloc_default_zone()->malloc` swap, or
      simply an `operator new` override counting calls while a flag is set) around `processBlock` in
      `md-plugin-check --bench` and/or a `plugin-rt` ctest case. Fail if any allocation happens after the first second
      (the JIT compiles new blocks on first use; a newly picked machine can allocate once, so warm the machines used
      by the test sequence first, or only count after warm-up).
- [x] Fix `OneProcessor.h`'s "nothing on that path locks or allocates" comment to say what is actually true (the
      keyboard-state lock remains).
- [x] Verify: `md-hash` default pattern still `04ef1db4fe767721`; ctest `plugin-render` hash `a28b9dc178776939`
      unchanged; `test_kits` kit-0 hash `f5c618b7df5550fa` with `MD_KITS_SYX`; `MD_JIT_LOCKSTEP=1` run clean.

## Not in scope

The dsp56300 JIT compiling blocks on the audio thread on first use (inherent to `ext/`, bench outliers only).

## Log

2026-10-12 (subagent, worktree branch `worktree-agent-ad8005011154e7bd9`, fast-forwarded from 7f49d24 to main f3b0dec
first: the worktree had been created before the JIT commits).

- **Fixes.** `MachineRunner::call` pushes the arguments straight from the `initializer_list` (`std::rbegin/rend`, no
  vector). `HostModel` ctor reserves `m_activeOrder` for `kTracks`; the member and the `push_back` say why it never
  grows past that (each track at most once: erased before it is pushed).
- **Guard: `md-plugin-check --check-rt`, ctest `plugin-rt`** (new `src/plugin/AllocCounter.{h,cpp}`, linked into
  md-plugin-check only). Two hooks: a replaced global `operator new`/`delete` (portable) and macOS libmalloc's
  `malloc_logger` (exported by libsystem_malloc; with it set, every malloc/calloc/realloc/aligned alloc in every zone
  calls it, fast paths included: checked with a scratch program at -O0 that all 9 test allocations showed, with and
  without `MallocNanoZone`). `malloc_logger` is what catches JUCE: `HeapBlock` uses `std::malloc`, invisible to an
  `operator new` override. Only the thread that called `begin()` counts; the hooks never allocate, lock or touch TLS.
- **The JIT, told apart by call stack, not by callback.** First design excluded whole callbacks in which the JIT
  compiled (a counter in VoiceEngine's `getBlockConfig` lambda). It hid the `m_activeOrder` growth: the first triggers
  after an OS load land in the same callbacks as the first JIT compiles. Final design: the malloc hook keeps each
  allocation's stack (`backtrace()`, a frame-pointer walk, into a preallocated 16384 x 32 table) and `end()` sorts them
  with `dladdr`: `dsp56k::JitBlockChain::create` or `dsp56k::funcCreate` on the stack = a JIT compile, anything else
  fails the check, from the very first callback (no warm-up needed). Core is untouched by the guard. A stripped binary
  would count everything as ours (fails, never passes falsely). Off macOS the check skips (77).
- **Sequence:** 4 settings (44.1 kHz/64, 48 kHz/480 all buses on and 6 tracks on own buses, 96 kHz/256, 44.1 kHz/1024 all
  buses), each 8 s = the same 4 s sequence twice: all 16 tracks on every 16th (accented downbeats, note-offs too),
  editor keyboard note on/off, automation (filter, LFO speed), tempo change with the play head dropping out and coming
  back, machine changes on two tracks and back.
- **Before (main):** per setting ~11,050 allocations outside the JIT in the first 4 s (incl. the 5 `m_activeOrder`
  growths at 0.0000 s) and ~11,000 in the second 4 s, i.e. **~2,750 a second** (the audit's 2,000 estimate left out the
  per-trigger calls: 125 Hz x 20 + 16 tracks x 8/s x 2). With only the `MachineRunner` fix: exactly 5 (the trace shows
  `vector<int>::emplace_back` in `HostModel::trigger`). **After: 0** in every setting, both passes. JUCE's part of the
  path (MidiBuffer iteration, `MidiKeyboardState::processNextMidiBuffer` injecting editor notes, play head, buses)
  allocated nothing.
- **The JIT's share** (first pass): ~509 blocks compiled in 8-14 callbacks, 5,922 mallocs (4,323 via operator new). The
  44.1 kHz/1024 setting compiles 2 more blocks (14 mallocs) in the second pass: rare paths keep showing up later, so a
  callback-count warm-up would have been brittle.
- **Bench** now reports allocations after 1 s (outside / inside JIT compiles): 0 / 0 at all three rates.
- **Comment** in `OneProcessor.h`: nothing on the processBlock path allocates (except the JIT's compiles), it never
  waits on a lock (engine lock is try-locked), the one lock it takes is `MidiKeyboardState`'s.
- Docs: `docs/DEVELOPMENT.md` (tool list, a measurement line), `README.md` (tests paragraph).
- Noticed, not changed: `HostModel::trigger`'s `ParallelVoiceEngine` branch allocates `std::vector<int> cost(groups)` per
  trigger of a non-sounding track (voice groups > 1 only; the plugin uses the single `VoiceEngine`). The pre-existing
  `ld: warning: reducing alignment of section __DATA,__common from 0x8000 to 0x4000` shows on the Standalone link too.
- **Verified** (Release, JIT ON): `md-hash` `hash 04ef1db4fe767721` (also `MD_GROUPS=2`, and `MD_JIT_LOCKSTEP=1`: same
  hash, no lockstep/fault message, 2.75 s); `plugin-render` `a28b9dc178776939` and white-key `14291ff9302212d4` (also
  under `MD_JIT_LOCKSTEP=1`); kits `kit 0 golden-trigger hash f5c618b7df5550fa` (also under lockstep); ctest with
  `MD_OS_SYX` and `MD_KITS_SYX`: `100% tests passed out of 11` (10 + `plugin-rt`). Bench: 6.7 / 7.0 / 7.1 % at
  44.1 / 48 / 96 kHz, 0 callbacks over budget, 0 allocations.
