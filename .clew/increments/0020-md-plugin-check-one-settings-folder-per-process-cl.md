---
id: 20
status: todo
tags:
- plugin
- tests
created_at: 2026-10-12T21:37:43Z
updated_at: 2026-10-12T21:37:43Z
---
## Why
Each `md-plugin-check --check-*` keeps its settings in a fixed temp folder named after the check (under
`~/Library/Caches/<executable>/md-plugin-check-render`, `-state`, ...) and deletes it at start and end. Two concurrent runs
(two worktrees running ctest, or a parallel stress loop) delete each other's OS-file setting; `render` then reports
"no engine: No Machinedrum OS file selected" and `checkRender` dereferences the empty result (SIGSEGV at address 0).
Found by increment 0010 while it looked like an x86_64 JIT crash (increment 0018). Until fixed: run one ctest at a time
per Mac, worktrees included (the README says so).

## Acceptance criteria
- [ ] The settings folder name includes the pid (or a `mkdtemp` folder) so concurrent checks never share it; removed at exit
- [ ] A check that gets no engine fails with a message and a non-zero exit, never a crash
- [ ] Eight parallel `--check-render` loops pass (the packaging agent's stress: 48 of 48)
- [ ] The README's "one ctest at a time" note is removed
