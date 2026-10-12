---
id: 19
status: todo
tags:
- plugin
- au
created_at: 2026-10-12T20:36:38Z
updated_at: 2026-10-12T20:36:38Z
---
## Why
`auval -oop -v aumu Mdo1 Mdrm` (out of process) passes everything except "Parameter values are different since last
set": changing `track<t>_machine` rewrites that track's SYN1-8 values (the machine's defaults), which the AU spec allows
only for parameters flagged as meta parameters (`kAudioUnitParameterFlag_IsMetaParameter`; in JUCE,
`AudioProcessorParameter::isMetaParameter()` returning true). In-process `auval` passes, so this only shows out of process
(Logic's AUHostingService runs plugins that way), found by increment 0010.

## Acceptance criteria
- [ ] `isMetaParameter()` is true for the 16 machine parameters (and any other parameter whose change rewrites others)
- [ ] `auval -oop -v aumu Mdo1 Mdrm` passes completely; in-process `auval` and `-strict` still pass
- [ ] `plugin-state` and `plugin-render` hashes unchanged
