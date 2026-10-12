---
id: 19
status: done
tags:
- plugin
- au
created_at: 2026-10-12T20:36:38Z
updated_at: 2026-10-12T22:29:00Z
---
## Why
`auval -oop -v aumu Mdo1 Mdrm` (out of process) passes everything except "Parameter values are different since last
set": changing `track<t>_machine` rewrites that track's SYN1-8 values (the machine's defaults), which the AU spec allows
only for parameters flagged as meta parameters (`kAudioUnitParameterFlag_IsMetaParameter`; in JUCE,
`AudioProcessorParameter::isMetaParameter()` returning true). In-process `auval` passes, so this only shows out of process
(Logic's AUHostingService runs plugins that way), found by increment 0010.

## Acceptance criteria
- [x] `isMetaParameter()` is true for the 16 machine parameters (and any other parameter whose change rewrites others)
- [x] `auval -oop -v aumu Mdo1 Mdrm` passes completely; in-process `auval` and `-strict` still pass
  (run on a scratch copy under another id, see Verification: auval only sees an installed component with that id)
- [x] `plugin-state` and `plugin-render` hashes unchanged

## Done (2026-10-12, with increment 0009's plugin side, branch `worktree-agent-a7625381f929937c1`)
- `MdOneProcessor::MachineParam::isMetaParameter()` returns true; JUCE's AU wrapper turns that into
  `kAudioUnitParameterFlag_IsGlobalMeta`. No other parameter rewrites others: a SYN, kit, LEV or LFO parameter sets only
  itself (the LFO PARAM's *names* follow the destination track's machine, its value does not move), the globals and
  `master_fx` set nothing else, and a master byte goes through the OS's smoothing without touching another parameter. A kit
  load or a restored state rewrites parameters, but neither is a parameter.
- `md-plugin-check --check-state` (ctest `plugin-state`): the 16 machine parameters, and no other one, are meta parameters.

## Verification
- The AU can only be validated under an id that is installed, and the installed `Moduledrum.component` (`Mdo1`) was not
  touched: as in increment 0010, a scratch copy of the tree (with increment 0009's plugin side, 533 parameters) was built
  with `PLUGIN_CODE Mdz2`, `PRODUCT_NAME "MdAuCheck"`, `BUNDLE_ID md.elektron-md.aucheck` in its own build directory,
  installed in `~/Library/Audio/Plug-Ins/Components`, validated and removed again (`auval -a` then lists `Mdo1` alone).
  It booted the engine from the user's shared OS setting (read only).
- `auval -v aumu Mdz2 Mdrm`: AU VALIDATION SUCCEEDED. `auval -oop -v aumu Mdz2 Mdrm`: **AU VALIDATION SUCCEEDED** (533
  parameters, exactly the 16 machine parameters flagged "Global Meta", the Master FX clumps). `auval -strict -v` and
  `auval -oop -strict -v`: AU VALIDATION SUCCEEDED.
- `plugin-render` `a28b9dc178776939` / `14291ff9302212d4` and `plugin-state` `da62047690565e38` unchanged (with master_fx
  off, increment 0009).
