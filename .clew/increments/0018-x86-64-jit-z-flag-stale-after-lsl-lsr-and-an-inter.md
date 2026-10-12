---
id: 18
status: todo
tags:
- engine
- jit
- intel
created_at: 2026-10-12T20:41:00Z
updated_at: 2026-10-12T20:41:00Z
---
## Why
Increment 0010's universal build ran the x64 JIT back end on the voice program for the first time (under Rosetta on an
Apple Silicon Mac). The audio hash matched (`04ef1db4fe767721`, also MD_GROUPS=2/4) but lockstep found a flag bug, and
a crash was blamed on the x64 slice at first.

## Finding 1: a real x64 JIT bug - FIXED (2026-10-12, coordinator, after the packaging agent's diagnosis)
`MD_JIT_LOCKSTEP=1 arch -x86_64 md-hash <OS>` reported `registers differ after JIT block 136872 (from P:$1038c0 to
$1038ca): sr jit=80044 interp=80040`: Z stayed set on the x64 JIT inside a `DO #$22` loop that LSLs non-zero table words.
Cause: `alu_lsl` / `alu_lsr` in `ext/dsp56300/source/dsp56kEmu/jitops_alu_x64.cpp` opened `CcrBatchUpdate(N|C|V)`;
the batch clears its bits and makes `ccr_update()` OR-only, so a Z=1 from an earlier zero result was never cleared.
Fix: Z joins the batch - fix 4 in `ext/patches/0001-dsp56300-md.patch` (`MD patch` marker), tests
`jit_lsl_after_zero_clears_z` / `jit_lsr_after_zero_clears_z` in `tests/test_jit.cpp`.
Verified under Rosetta (`-DCMAKE_OSX_ARCHITECTURES=x86_64`): the two tests fail without the fix (5/7) and pass with it
(7/7); `MD_JIT_LOCKSTEP=1 arch -x86_64 md-hash` prints the hash with no lockstep report; arm64 unaffected (7/7, hash).

## Finding 2: the "intermittent x86_64 SIGSEGV" was not the JIT - RETRACTED
The plugin checks (`md-plugin-check --check-render` etc.) keep their settings in a fixed temp folder per check name and
delete it at start and end, so two concurrent runs delete each other's OS-file setting; `render` then has no engine and
`checkRender` dereferences the empty result. Native arm64 crashes the same way when run in parallel (21 of 24), and an
interpreter-only x86_64 build too; one run at a time passes everywhere. Rosetta also recovered by itself (the earlier
"wedged" note was wrong). The checker fix is its own increment (0020).

## Remaining
- [ ] Run the full x86_64 ctest on a real Intel Mac (so far: Rosetta, one test at a time, 10/10)
- [ ] Then flip `release/build_release.sh -a` default to `universal` and record it in README/DEVELOPMENT.md
- [ ] Report fix 4 upstream with the other three (dsp56300/dsp56300; Monomodule has the same JIT)
