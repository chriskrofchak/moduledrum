---
id: 6
status: backlog
tags:
- kits
- needs-info
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T15:04:48Z
---
## Why
ROM sample machines and the 16 factory kits need an 8 MB MD UW flash image (only from a real unit). The MPC build extracts them by booting gearmulator-md-mm in Docker (`tools/mdkits`, `tools/mdtrace`).

## Tasks
- [ ] Ask the user whether they have a flash image
- [ ] Try running the extraction natively on macOS instead of Docker
- [ ] Load ROM_SAMPLES.bin at runtime from the user's data folder (never shipped)
