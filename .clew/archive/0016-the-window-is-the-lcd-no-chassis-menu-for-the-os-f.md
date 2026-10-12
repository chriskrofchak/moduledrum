---
id: 16
status: done
created_at: 2026-10-12T19:27:33Z
updated_at: 2026-10-12T19:27:40Z
---
## Why

Side by side with Monomodule (2026-10-12), the user found Moduledrum's window the less pretty one: the LCD sat in a
brushed-aluminium chassis with a black bezel, a "Select OS File..." button and two status lines under it. Once the OS
file is chosen it is saved (shared settings), so the chassis served nothing afterwards. The user asked for the window
to be the LCD alone, as Monomodule's is.

## What changed

- `OneView` paints flat paper and nothing else; the base size is the LCD content plus its margin and a footer. No
  bezel, no aluminium, no button row (`kMargin`/`kBezel`/`kGlassEdge`/`kButtonRowH` gone).
- MENU (an `LcdButton`, 32x10 LCD px) at the header's right end, the product name and the kit box moved left of it:
  Select OS File..., Clear OS File Selection, Skin (the three presets of `Skin.h`; a custom pair from the settings file
  shows as a checked entry), Show Engine Status, Kit Folder...
- Show Engine Status: the two status lines (processor's `statusText`, the OS path) under the strip, in ink; the view
  grows by `kStatusH` (36 px) and the `ScaledEditor` follows at its scale (`ScaledHolder::childBoundsChanged`).
- Footer: the version (`MD_VERSION_STRING`, the project version from CMake, on both targets) at the left, the corner
  grip at the right, drawn in ink.
- A dropped `.syx` that holds no kit but parses as a Machinedrum OS (`md::fw::loadFirmware`) becomes the OS file.
- The SYN page's no-OS message points at MENU and at dropping the file; a failed load shows the processor's status
  there (it used to say "see the status below").
- `md-plugin-check`: snapshot `status`; `--check-editor` checks the status toggle's size change and the OS-file drop.

## Tasks

- [x] Remove the chassis, lay the LCD out as the window
- [x] MENU: OS file, clear, skin presets, engine status, kit folder
- [x] Footer with version and grip; status lines on request
- [x] OS file by drag and drop
- [x] Snapshots and editor checks; ctest 10/10 with `MD_OS_SYX` and `MD_KITS_SYX`

## Left for later

- The popup menu is JUCE's default look; Monomodule skins it (`OneLookAndFeel`). A custom skin dialog (`SkinDialog`).
- Another window changing the skin is not picked up until this one reopens (Monomodule polls the shared setting).
