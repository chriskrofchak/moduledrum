---
id: 5
status: done
tags:
- kits
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T17:48:24Z
---
## Why
Load real MD kits (user dumps, downloaded packs). Port the `$52` kit-dump parser from
`resources/mpc-vst-machinedrum/vst/engine.cpp` (the `Catalog`/`Kit` code around lines 200-330) into `src/core/md/library/`,
and give the plugin a kit browser.

## Test data
`~/Downloads/MD_presets_1_50.syx_.zip` -> `MD_presets_1_50.syx` (436 KB): a full MD sysex snapshot with **64 kit dumps**
(`F0 00 20 3C 02 00 52`, 1233 bytes each), 128 pattern dumps (`$67`), 32 song dumps (`$69`) and 8 global dumps (`$50`).
Use it (from Downloads or a copy in the user's data folder; never committed) to test the parser and the browser.
Elektron's C6 (`~/Downloads/Elektron_C6_MAC_AND_WIN_1.51`) and Transfer are also there for making dumps from hardware.
The sysex formats are documented in the MD manual's appendix (`~/Downloads/machinedrum_manual_OS1.63_1.pdf`); MCL
(https://github.com/jmamma/MCL) has a known-good C++ decoder to cross-check against.

**What the file really holds:** only slots 0-15 are kits (TRX, EFM, E12, P-I, FRANSISCO, BIPBOP, WIGGLY, BENTY, TOAST, BOOGIE,
SWARM, MAYHEM, HAUNTED, ALIENS, BRONCO, FUGLESANG). Slots 16-63 are *free*: the MD marks a cleared slot with `$7f` as the first
name byte and all 16 machines on GND-EMPTY (slots 16-40 still carry the stale rest of an old name behind the marker, 41-63
carry buffer garbage). So the file has 64 kit messages but 16 usable kits. The manual's sysex appendix lists the message ids
only, not the bytes of the dumps.

## Acceptance criteria
Split into the core (this increment's first part, done) and the plugin/editor (later, see "Remaining (editor side)").
- [x] Load a kit .syx (core): `md::library::applyKit` sets machines, all 24 params per track, levels and LFO settings on the 16
  tracks; the kit names are in `Kit::name` (shown by `md-kits list`). Routing: **a kit has none** (see decisions), so there is
  nothing to apply.
- [x] Load a kit .syx (editor): the file's kit names shown in the editor (0008: the GLOBAL tab's KITS page)
- [x] A file with several kits is a bank; bank/kit picker in the editor; drag-and-drop .syx onto the editor
  (core: `md::library::Bank` is the bank; editor: 0008's `KitLibrary` + `KitsPage`, a drop copies the file into the folder)
- [x] Kits re-read from the user's kit folder (`~/Library/Application Support/Moduledrum/Machinedrum/kits/`) when files
  change (0008: names, sizes and modification times compared every 1.5 s)
- [x] Master-effect fields in the kit decoded and kept (unused until increment 0009): `Kit::masterFx`
- [x] Patterns/songs/globals in a mixed dump are skipped without error (counted in `Bank`)
- [x] Unit test: parse the 64-kit file, check the count and a few known values; rendering a kit through md-hash's pattern gives a
  stable hash (`tests/test_kits.cpp`, below)

## Tasks (core, all done)
- [x] `src/core/md/library/MdSysex.{h,cpp}`: message splitting, header check, 7-bit packing, kit decode **and encode**, `Bank`
- [x] `src/core/md/library/KitApply.h`: `applyKit(HostModel<T>&, const Kit&)`
- [x] `src/core/util/Wav.{h,cpp}`: 24-bit WAV writer (ported from Monomodule, there was none here yet)
- [x] `src/cli/md_kits.cpp`: `md-kits list` / `md-kits render`
- [x] `tests/test_kits.cpp`, ctest `kits`
- [x] Editor side: see below (done in increment 0008)

## Kit dump layout (OS 1.63, version 4 / revision 1, 1233 bytes; the same text is in `MdSysex.h`)
```
0x000  F0 00 20 3C 02 00 52       header; the byte after "02 00" is the message type
0x007  version, revision          4, 1
0x009  kit slot 0-63              (included in the checksum)
0x00a  name, 16 bytes             NUL-terminated ASCII; stale buffer content after the NUL; first byte $7f = free slot
0x01a  16 x 24 track params       plain 7-bit values, HostModel raw order
0x19a  16 levels (LEV)            plain
0x1aa  16 machine words           7-bit packed, 64 -> 74 bytes; 32-bit big-endian, machine id = low byte
0x1f4  16 LFOs x 36 bytes         7-bit packed, 576 -> 659: dest track, dest param, shape 1, shape 2, update, 31 bytes of state
0x487  master effects, 32 bytes   plain: gate box reverb, rhythm echo, master EQ, dynamix; 8 values each
0x4a7  trig + mute groups         7-bit packed, 32 -> 37 bytes: 16 trig groups, 16 mute groups ($ff = none)
0x4cc  checksum (2x7 bit), length (2x7 bit), F7     checksum = sum(bytes 0x009..0x4cb) & $3fff; length = size - 10
```
7-bit packing: a header byte with the top bits of the next 7 data bytes (first byte in bit 6), then those 7 bytes.
Each 7-bit block starts a new group (the machine block ends in a short group, the LFO block starts fresh) -- this was the
trap: reading machines + LFOs + effects as one stream shifts everything by a byte (the MPC port avoids it by decoding
the blocks separately). The order follows MCL's `MDKit::fromSysex` (raw name, params, levels; 7-bit machine words; 7-bit
LFOs; 32 raw effect bytes; 7-bit group bytes).

Verified against the real file: all 232 messages (64 kits, 128 patterns, 32 songs, 8 globals) have a checksum and length of
the form above (the checksum starts at byte 9, the kit slot, which is why a plain sum from byte 10 matched only slot 0);
decode + encode reproduces all 64 kit messages byte for byte (test `kits_user_snapshot_reencodes_byte_for_byte`), so no byte of
a kit dump is unaccounted for; the machines of kits 0-15 name sensibly (kit 0 = the 16 TRX machines in family order, the EFM, E12
and P-I kits likewise; the others mix families).

## Decisions taken while implementing (2026-10-12)
- **Free slots are kits too.** `Bank::kits` holds every kit message (64 for the user's file) in file order; `Kit::isFree()` is
  the `$7f`/NUL first-name-byte marker the MPC port used to skip them, `Kit::name` is empty for them. The editor should hide or
  grey free slots. (The MPC port dropped them from its catalog.)
- **Names:** characters up to the first NUL or other non-printable byte, trailing spaces trimmed; `Kit::rawName` keeps all 16
  bytes (so a decoded kit re-encodes exactly).
- **Routing is not in a kit.** The MD keeps the output assignment per track (OUT A-F / main) in the *global* settings (`$50`; MCL
  reads it in `MDGlobal`, not `MDKit`). The first 16 decoded bytes of a global dump look like the 16 routes (`06` = main, with
  `$86` on tracks 5, 6, 12, 13 in the user's file: bit 7 unexplained). Not parsed (globals are skipped and counted); the
  engine has `setRouting` for when the plugin gets multiple outputs. `applyKit` leaves routing alone.
- **`applyKit` order:** `setMachine` (resets SYN1-8 to the machine's defaults), then all 24 `setParam`s (SYN from the kit wins),
  `setLevel`, `setLfo`. LFO values are clamped to what the OS knows (dest track 0-15, param 0-23, shapes 0-5, update 0-2) as the
  MPC port does; `Kit` itself keeps what the file says. It is a template over the voice engine (`HostModel<TVoices>`). Mutes,
  tempo and routing are not touched. As on the MD, the machine takes effect at the track's next trigger.
- **Master effects** (`Kit::masterFx`): four raw 8-value blocks in the file's order -- `reverb` (gate box: DVOL PRED DEC DAMP HP
  LP GATE LEV), `delay` (rhythm echo: TIME MOD MFRQ FB FILTF FILTW MONO LEV), `eq` (LF LG HF HG PF PG PQ GAIN), `dynamics`
  (dynamix: ATCK REL TRHD RTIO KNEE HP OUTG MIX). The block order is MCL's; the parameter order is the manual's page order, which
  the init kit's values fit for the first three (gate box DEC 64 / LP 127 / GATE 127 = off; echo FILTW 127; EQ gains 64 = 0 dB).
  The dynamix order is unconfirmed (its init values `7f 7f 7f 7f 7f 7f 00 00` look odd read that way). Increment 0009 should
  verify against the DSP1 code before relying on the names.
- **Trig and mute groups** are decoded (`Kit::trigGroup`, `Kit::muteGroup`: track number or `$ff` for none; the engine does not
  model them). Kits 0 and 2 have `muteGroup[8] = 9` (closed hat mutes open hat), which fits.
- **Strictness:** a kit message must be exactly 1233 bytes with the Machinedrum header, `$52`, F7 and no byte with bit 7 set, else
  it is counted in `Bank::rejected` with a reason in `Bank::errors` (never an exception). A wrong checksum/length is *not* a
  rejection: the kit decodes, `Kit::checksumOk` is false and `Bank::badChecksums` counts it. Version/revision are recorded, not
  checked (older OS versions may have other layouts; only the length guards that).
- `Bank` is what a file holds; `loadSyxFile` refuses files over 32 MB and throws `std::runtime_error` on unreadable ones (the
  editor should catch it per file).
- **Tests are in the existing md-tests binary**, ctest name `kits`: the synthetic cases (7-bit vectors, raw offsets of a built kit,
  encode/decode round trip, free slots, a mixed stream with a truncated message) always run; `MD_KITS_SYX` adds the 64-kit
  checks and the byte-exact re-encode; `MD_KITS_SYX` + `MD_OS_SYX` add the render. Without the env vars the file cases are skipped.
- **Recorded hash:** kit 0 of the user's file ("TRX") applied to a fresh engine, then `golden::Pattern`'s triggers (not its
  `setup()`, which would replace the kit's machines) for 12 s: **`f5c618b7df5550fa`**, peak 2894629 of 8388607 (-9.2 dBFS),
  stable across runs. `md-hash` still prints `04ef1db4fe767721`; no engine file was changed.
- **Finding for the audio path:** the same kit through `ParallelEngine` with 2 voice groups gives a different hash
  (`459526cbdcaa5166`; with 1 group it is identical). Per-track comparison: the first difference is block 301, track 12 (0-based,
  a TRX-MA), at the start of its sound; triggering that track alone matches. So the same-hash-for-any-group-count property holds
  for the golden pattern's machines but not for every kit -- probably DSP state shared by the voices of one instance (a noise
  source), so a split render is not the single-DSP (hardware) render for such machines. Not investigated further; a render with
  voice groups > 1 should not be compared bit for bit against kit renders from `md-kits` or this test.
- **`md-kits render`** warms the engine 0.2 s (the OS smooths levels and parameters), plays each non-GND track once for 0.6 s, then
  all of them together for 2 s, and writes a 24-bit 44.1 kHz stereo WAV of the main output (the engine's own 24-bit words).
  Kit 0 -> 11.8 s, peak 4015682 of 8388607 (-6.4 dBFS); every one of the 16 tracks is audible in its slot.
- `THIRD_PARTY_NOTICES.md` got a row for the kit decoder (ported from the MPC port) and `Wav.{h,cpp}` (Monomodule).

## Verification (2026-10-12, macOS arm64, Apple clang 21, CMake/Ninja, `-DMD_BUILD_PLUGIN=OFF`)
- `MD_OS_SYX=<OS 1.63> MD_KITS_SYX=<MD_presets_1_50.syx> ctest --test-dir build --output-on-failure`: `firmware`, `golden-hash`,
  `kits` pass (8 kits cases). Without the variables `kits` passes its 5 synthetic cases and skips the other 3.
- `build/src/cli/md-hash <OS>` -> `hash 04ef1db4fe767721`.
- `md-kits list <file> <OS>`: 64 kits (16 used), skipped 128 patterns, 32 songs, 8 globals.
- `md-kits render <OS> <file> <i> out.wav` for kits 0-15: no DSP fault, all 16 tracks audible, peaks -3.1 to -15.6 dBFS.

## Editor side (done 2026-10-12 in increment 0008; details in its markdown)
- [x] Kit/bank picker in the editor: the GLOBAL tab's KITS page, banks = the `.syx` files of the kit folder (`KitLibrary`), each
  bank's used kits only (free slots hidden), a click loads; the loaded kit / bank shown in the header and the page's footer;
  drag-and-drop of a `.syx` anywhere on the editor copies it into the kit folder. Verified by ctest `plugin-editor` with the
  user's file: 16 of 64 kits listed (TRX .. FUGLESANG), a click on kit 4 loads FRANSISCO's 16 machines.
- [x] The plugin loads the picked kit and keeps it across sessions: `MdOneProcessor::loadKit` (increment 0004: the kit's
  values through the host parameters, the kit dump in the state), not `applyKit` under the engine lock.
- [x] Watch the kit folder (`~/Library/Application Support/Moduledrum/Machinedrum/kits/`, created when an editor opens) and
  rebuild the list when files change: the editor's timer compares names, sizes and modification times every 1.5 s and
  re-reads on the message thread (no background thread: a 436 KB dump reads in milliseconds).
- Left open (optional, not needed for kits): parsing the global dump's output routing should the track outputs ever follow
  it (see decisions); the hardware cross-checks below (the dynamix order is increment 0009's to confirm).
- Hardware cross-checks this increment could not do: the dynamix block's parameter order; ROM/RAM machine ids in a kit dump (no
  kit in the test file uses ids >= 128, the MPC port's reading -- the OS id in the low byte -- is assumed).
