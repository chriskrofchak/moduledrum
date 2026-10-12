---
id: 20
status: done
tags:
- plugin
- tests
created_at: 2026-10-12T21:37:43Z
updated_at: 2026-10-12T21:51:03Z
---
## Why
Each `md-plugin-check --check-*` keeps its settings in a fixed temp folder named after the check (under
`~/Library/Caches/<executable>/md-plugin-check-render`, `-state`, ...) and deletes it at start and end. Two concurrent runs
(two worktrees running ctest, or a parallel stress loop) delete each other's OS-file setting; `render` then reports
"no engine: No Machinedrum OS file selected" and `checkRender` dereferences the empty result (SIGSEGV at address 0).
Found by increment 0010 while it looked like an x86_64 JIT crash (increment 0018). Until fixed: run one ctest at a time
per Mac, worktrees included (the README said so).

## Acceptance criteria
- [x] The settings folder name includes the pid (or a `mkdtemp` folder) so concurrent checks never share it; removed at exit
- [x] A check that gets no engine fails with a message and a non-zero exit, never a crash
- [x] Eight parallel `--check-render` loops pass (the packaging agent's stress: 48 of 48)
- [x] The README's "one ctest at a time" note is removed

## Log
- `src/plugin/Check.cpp`: `ScopedSettingsDir(name)` (RAII) replaces the three lines at the top of every check
  (`--check-os-load`, `-render`, `-state`, `--bench`, `--check-rt`, `--snapshot`, `--check-editor`): `mkdtemp` of
  `md-plugin-check-<name>-XXXXXX` under JUCE's temp directory (`~/Library/Caches/<executable>` on macOS), `MD_SETTINGS_DIR`
  set to it, folder deleted by the destructor on every way out of the check (return, a failed check, an abort). The
  executable's own folder in the cache root is only the root: no run touches another run's folder.
- A caller's `MD_SETTINGS_DIR` is still not honoured by the checks: they always replaced it (a `setenv(..., 1)` in each),
  which is what keeps the user's real settings safe, and deleting a caller's folder at exit would not. Kept as it was.
- `--check-os-load` also had a shared file beside the folder (`md-plugin-check-not-an-os.syx` in the cache root, a
  *sibling* of the settings folder): two concurrent runs could delete it under each other. It is now a file in the run's
  own folder.
- No engine: `render()` (the helper every render-based check reads its result from) calls `abortCheck()` when the
  processor has no engine, and when the all-buses layout is refused (the other way it returned an empty `Render`).
  `abortCheck()` prints the `[FAIL]` line and throws `CheckAbort`; `main` is a function-try-block that catches it, prints
  `md-plugin-check: the check was aborted` and returns 1. One place, so no check function changed; the unwinding
  runs the folder's destructor. Tried with `--fw /etc/hosts`: `--check-render` prints `[FAIL] render: no engine: OS file
  load failed: ...` and exits 1; `--check-state` and `--check-editor` also fail cleanly (exit 1). The caller's
  `MD_SETTINGS_DIR` folder stays untouched, the cache root is empty afterwards.
- The `settingsDir.deleteRecursively();` lines that end the checks are now redundant (the guard does it); they were left
  so as not to touch lines next to the other branch's edits to the check bodies. Folders of a check killed by a signal
  (a ctest timeout) stay in `~/Library/Caches/<executable>/`, a few kilobytes each.
- `tests/stress_checks.sh [bin] [jobs] [runs]` is the stress: `jobs` parallel loops of `runs` runs of `--check-render`,
  then of `--check-state`, from one binary. Before the change (same build): `--check-render` 15 of 24 (SIGSEGV,
  exit 139), `--check-state` 24 of 24 that time. After: `--check-render` 24 of 24, `--check-state` 24 of 24 (44 s each
  run of the script, 8 cores busy); the cache root is empty afterwards. Also three `ctest -R plugin -j5` at once (15
  plugin checks at a time): all pass.
- ctest (OS 1.63 and the preset kits) 15 of 15 pass; `md-hash` `hash 04ef1db4fe767721`.
- README: the "run one ctest at a time" note is gone; `docs/DEVELOPMENT.md`: the 0010 paragraph no longer lists the shared
  temp folders as left, and "Finding 2" says it is fixed.
