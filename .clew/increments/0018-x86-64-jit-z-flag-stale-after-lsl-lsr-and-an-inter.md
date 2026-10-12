---
id: 18
status: todo
tags:
- engine
- jit
- intel
created_at: 2026-10-12T20:36:38Z
updated_at: 2026-10-12T20:36:38Z
---
## Why
Increment 0010's universal build ran the x64 JIT back end on the voice program for the first time (under Rosetta on an
Apple Silicon Mac). The audio hash matches (`04ef1db4fe767721`, also MD_GROUPS=2/4) but two things are wrong, so the
release default stays arm64-only until they are settled.

## Finding 1: a real x64 JIT bug (confirmed by lockstep, not yet fixed)
`MD_JIT_LOCKSTEP=1 arch -x86_64 md-hash <OS>` reports `registers differ after JIT block 136872 (from P:$1038c0 to
$1038ca): sr jit=80044 interp=80040`: Z stays set on the x64 JIT inside a `DO #$22` loop that LSLs table words by 12
(the values are not zero). The aarch64 JIT has no difference over the whole run.
Probable cause (from reading, not yet proven by a run): `alu_lsl` / `alu_lsr` in
`ext/dsp56300/source/dsp56kEmu/jitops_alu_x64.cpp` (~lines 144 and 168) open `CcrBatchUpdate(N|C|V)`; the batch clears
those bits and makes `ccr_update()` OR-only, but Z is not in the mask, so an earlier Z=1 is never cleared.
Fix: add `CCR_Z` to both masks in `ext/patches/0001-dsp56300-md.patch` (fix 4, `MD patch` marker) and a
`tests/test_jit.cpp` case (LSL of zero, then of a non-zero value; JIT vs interpreter). The voice program never reads
Z between those instructions here (the hash matched), but the bug is real and upstream.

## Finding 2: intermittent SIGSEGV in the x86_64 slice under load (unsettled)
Null dereference in JIT code on the main thread in `plugin-render` and `plugin-state`: serial runs pass (4/4, 15/15),
eight parallel copies crash `--check-render` in 21 of 24 runs (`--check-state` 0 of 24); arm64 never crashes (also with
`MallocScribble`). Not known whether it is the x64 JIT or Rosetta's handling of asmjit's RWX JIT memory. The
discriminating test: an interpreter-only x86_64 build (`-DCMAKE_OSX_ARCHITECTURES=x86_64 -DMD_USE_JIT=OFF`) under the
same load; and ideally a real Intel Mac.

## Rosetta on this Mac got wedged during 0010
An lldb session attached to the x86_64 slice hung; afterwards every new x86_64 binary hangs at exec (state U) and
`kill -9` does not end them. Needs `sudo killall oahd` or a reboot before any further x86_64 work. Do not attach lldb to
the x86_64 slice. Run x86_64 tests with `-DCMAKE_CROSSCOMPILING_EMULATOR="arch;-x86_64"` (documented in the README).

## Acceptance criteria
- [ ] Lockstep clean on x86_64 over the whole golden render; the new `jit` test case fails without the fix
- [ ] The crash is explained: reproduced on an interpreter-only x86_64 build (-> Rosetta/asmjit memory) or not (-> JIT), and fixed or documented
- [ ] All hashes identical on x86_64 (md-hash, sweep, kits, plugin-render/state), then flip `release/build_release.sh -a` default to universal
