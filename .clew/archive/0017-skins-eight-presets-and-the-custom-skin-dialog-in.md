---
id: 17
status: done
created_at: 2026-10-12T19:34:02Z
updated_at: 2026-10-12T19:34:11Z
---
## Why

Following increment 0016 the user asked for the option to change skins, as Monomodule has (2026-10-12).

## What changed

- `Skin.h`: presets Machinedrum, Inverted, Black on white, White on black, Low contrast, Green LCD, Amber LCD,
  Blue LCD, Custom (Custom stays last; a settings file saved before this with "skin" = 3 now reads as White on black).
  `savedCustomInk()` / `savedCustomPaper()` read the custom pair.
- `SkinDialog.{h,cpp}` ported from Monomodule: two hex fields with a live preview (a dial and a machine block), APPLY
  saves to the shared settings, CANCEL / Escape restores. Over the whole view, the page dimmed under it.
- MENU > Skin lists every preset (the current one checked) and Custom...; the editor polls the shared skin every 2 s
  so a change in one window reaches the others.
- `md-plugin-check --snapshot`: `skin-<preset>` for each preset and `skin-dialog`.

## Tasks

- [x] Presets and the dialog
- [x] Menu wiring and the cross-window poll
- [x] Snapshots; ctest 10/10
