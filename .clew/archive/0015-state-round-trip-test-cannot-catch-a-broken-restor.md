---
id: 15
status: done
tags:
- bug
- tests
created_at: 2026-10-12T19:11:00Z
updated_at: 2026-10-12T19:51:10Z
---
## Why

Found in the 2026-10-12 audit. In `src/plugin/Check.cpp`, the `render()` helper (around line 250) unconditionally calls
`setMidiChannel(o.midiChannel)`, `setNoteMap(o.noteMap)` and `setTempo(o.tempo)` *after* `o.setup(p)` runs. The state
round-trip test (around lines 768-790) restores via `restore.setup = [...] { p.setStateInformation(...) }` and then
compares parameters and bytes, but `render()` has already forced those three globals back to the values `edit` used.
So if `MdOneProcessor::setStateInformation` (`OneProcessor.cpp` ~876-879) stopped restoring them, all three checks
(`before == after`, `saved == again`, equal render hashes) would still pass.

Coverage gap: the schema-2 restore of the note map is not tested anywhere; channel and tempo are only tested on the
schema-1 path.

## Tasks

- [x] In `RenderOptions`, make the three globals optional (e.g. `std::optional`) or add a flag so the restore pass
      does not re-apply them; the restore render must take channel/note map/tempo from the restored state only.
- [x] Use non-default values for all three in the edited session (channel 3 is already used; also a non-default
      note map and tempo) so a missing restore changes the render hash.
- [x] Assert explicitly after restore: `midiChannel()`, `noteMap()` and tempo equal the edited values.
- [x] Temporarily comment out the note-map restore in `setStateInformation` and confirm the test fails, then restore it.
- [x] `md-plugin-check` full run still passes; `plugin-render` hash unchanged.

## Log

- Implemented by a Sonnet agent in a worktree (branch `worktree-agent-a01265fcd4037425c`, commit 7ca2c59, `src/plugin/Check.cpp`
  only), cherry-picked onto `main` by the coordinator. `RenderOptions::tempo/midiChannel/noteMap` are now `std::optional` with
  the old defaults; `render()` applies each only if set; the restore pass sets all three to `std::nullopt`. Optional rather
  than a bool flag: per-global control, every other caller unchanged.
- Unanticipated: the processor's default note map is WHITE KEYS, so the old edit session (`edit.noteMap = WhiteKeys`) would
  not have caught a missing note-map restore even without `render()`'s override. The edit session now uses Chromatic; the
  notes are no longer remapped to white keys (only their channel is set to 3). Channel 3 vs the default omni does not change
  the render hash (omni accepts channel 3), so the new getter assertion is what covers the channel; tempo 133.5 vs 120 does
  change it (no play head in that pass).
- New check in `restore.after`: `midiChannel()==3 && noteMap()==Chromatic && tempo()==133.5`.
- Negative tests (agent, and repeated by the coordinator on `main` for note map, channel and tempo each removed from
  `setStateInformation`): four FAIL lines, the new schema-2 assertion, "an edited session: every parameter restored", "its
  state saved again is the same bytes (20906 bytes)" and "and it renders the same (25a437e2359e7c32)". All reverted.
- ctest 11/11 pass; `plugin-render` hash `a28b9dc178776939` unchanged. The edited-session render hash is now
  `25a437e2359e7c32` (not a pinned constant).
- The agent's worktree sandbox could not write this file; the coordinator pasted its log.
