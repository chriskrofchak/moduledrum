---
id: 9
status: backlog
tags:
- engine
- stretch
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T15:04:48Z
---
## Why
The MPC port drops them for CPU. A Mac has headroom (~13 M DSP instructions/s extra per HANDOFF). Options: run DSP1's own program in a second emulated DSP56300, or translate to C++ as was done for the per-track chain.

## Tasks
- [ ] Read HANDOFF.md/PROTOCOL.md on DSP1's master section
- [ ] Prototype DSP1 emulation fed by the Mixer's send outputs; compare against gearmulator-md-mm
