---
id: 12
status: done
tags:
- build
- ready-for-agent
created_at: 2026-10-12T16:55:47Z
updated_at: 2026-10-12T17:06:37Z
---
## Why
The user chose the name **Moduledrum** (2026-10-12): Machinedrum with Machine -> Module, as Monomachine -> Monomodule.
"Drummodule" was a placeholder from increment 0002. Do this after increment 0004 merges and before 0008 starts, so the
editor rebuild uses the final name.

## What changes
- `src/plugin/CMakeLists.txt`: PRODUCT_NAME "Moduledrum", COMPANY_NAME "Moduledrum", BUNDLE_ID `md.elektron-md.one` stays,
  PLUGIN_MANUFACTURER_CODE `Mdrm` (was `Drmd`), PLUGIN_CODE stays `Mdo1`; the AU identifier becomes `aumu Mdo1 Mdrm`.
- `src/plugin/SharedSettings.h`: settings folder `~/Library/Application Support/Moduledrum/Machinedrum/` (document that an
  old `Drummodule/` folder is not migrated; there are no users yet).
- Every string and comment: `git grep -i drummodule` (OneEditor's "DRUMMODULE" print, Check.cpp, README, THIRD_PARTY_NOTICES,
  CLAUDE.md, the increments). Target names (`MdOne`, `MdPluginCheck`) and the `md::plugin` namespace stay.
- Remove the old installed copies: `~/Library/Audio/Plug-Ins/Components/Drummodule.component` and
  `.../VST3/Drummodule.vst3` (and run `killall -9 AudioComponentRegistrar` so auval sees the new one).

## Acceptance criteria
- [x] `git grep -i drummodule` is empty outside `.clew/archive/`
- [x] Build, `ctest` with the OS and kits all pass, `md-hash` unchanged, `auval -v aumu Mdo1 Mdrm` succeeds on the installed build
- [x] `md-plugin-check --snapshot` shows the new name on the chassis

## Done 2026-10-12
Applied inline by the coordinator: 18 files, 43 substitutions (`Drummodule`/`DRUMMODULE`/`drummodule`, manufacturer code `Drmd` -> `Mdrm`; the
state tag is now `ModuledrumState`). Verified: 8/8 tests with the OS and kits, `md-hash` unchanged, `auval -v aumu Mdo1 Mdrm` succeeds on the
installed build, the snapshot shows MODULEDRUM on the chassis. The old installed `Drummodule.*` plugins were deleted; no settings folder
existed yet, so nothing was migrated. `CLAUDE.md` keeps one historical mention of the old name on purpose.
