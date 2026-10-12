---
id: 10
status: in_progress
tags:
- build
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T19:32:48Z
---
## Why
Moduledrum had no way to be handed to anyone: Ninja builds were ad-hoc signed by the linker with no entitlements (JUCE
writes the hardened-runtime entitlements file but only Xcode applies it), so a build signed with a real identity would
stop at the first JIT allocation; the binaries were arm64 only; there was no installer and no CI.

## Plan (2026-10-12)
1. **Universal binary.** `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` builds plugins, standalone and CLI tools. Verify the
   x86_64 slice under Rosetta (`md-hash` = `04ef1db4fe767721`, ctest incl. `jit`). A different hash or a failing test is a
   finding, not something to paper over; the x86_64 slice then stays out of the default until fixed.
2. **Signing.** `MD_CODESIGN_IDENTITY` (default `-`, ad-hoc): after each plugin/app build `codesign --force --options
   runtime --timestamp=none --entitlements <JUCE's file> -s ...` on the AU, VST3 and standalone. `auval` must still pass.
3. **`release/build_release.sh [-v version] [-s identity] [-n]`**: clean Release configure, tests that need no OS file,
   signing, component `.pkg`s + distribution, `.zip`, in `dist/`. `-n`: notarytool + stapler (`MD_NOTARY_PROFILE`).
   Nothing firmware-derived goes in.
4. **CI**: `.github/workflows/build.yml` on macos-14, ctest with the OS-dependent tests skipping, bundles uploaded.
5. **Docs**: README (Installing a release, Building options), docs/DEVELOPMENT.md.

## Result (2026-10-12): everything is in place; the x86_64 slice is not trusted yet, so it is not the default
Done by a Sonnet agent in an isolated worktree. Commits: signing, release script, CI, docs (see `git log`).

### What exists
- `cmake/codesign.cmake`: `md_codesign_bundle(<target>)` adds the codesign command as a POST_BUILD step of MdOne_AU,
  MdOne_VST3 and MdOne_Standalone; cache variables `MD_CODESIGN_IDENTITY` (`-`) and `MD_CODESIGN_TIMESTAMP` (`none`; empty
  = Apple's server, which notarization needs). `src/plugin/CMakeLists.txt` passes `COPY_PLUGIN_AFTER_BUILD FALSE` to
  `juce_add_plugin` and calls `juce_enable_copy_plugin_step(MdOne)` after the signing, because JUCE's own copy step is
  added at the end of `juce_add_plugin` and would copy the bundles before they are signed (checked: with
  `MD_COPY_PLUGINS=ON` the copies in the plugin folders verify and carry the runtime flag).
- Top-level `MD_VERSION` (default 0.1.0; `-DMD_VERSION=` overrides it before `project()`): the bundles' version.
- `release/build_release.sh [-v version] [-s identity] [-n] [-a arm64|universal]`, `release/distribution.xml`,
  `release/resources/{welcome,conclusion}.html`. Build directory `build-release/`, output `dist/` (git-ignored).
  Environment: `MD_INSTALLER_IDENTITY` (Developer ID Installer certificate that signs the .pkg), `MD_NOTARY_PROFILE`
  (notarytool keychain profile, made with `xcrun notarytool store-credentials`), `MD_CMAKE_ARGS` (extra configure args,
  e.g. `-DFETCHCONTENT_SOURCE_DIR_JUCE=...` to build offline), `MD_KEEP_BUILD=1` (reuse build-release/, for working on the
  script).
- `.github/workflows/build.yml` (macos-14; checkout with submodules, ninja, configure with `MD_BUILD_PLUGIN=ON`, build,
  ctest, signature check, ditto zips, upload-artifact). Action versions (`checkout@v7`, `upload-artifact@v7`) are those
  of Monomodule's workflow; they could not be checked from here. YAML parses (ruby); the `run:` steps were executed
  locally with `bash -eo pipefail`.
- README: Installing a release, Signing, Universal binary and Intel, Making a release, options table. DEVELOPMENT.md: the
  x64 section under the JIT story and the 0010 paragraph.

### Decisions
- **x86_64 is not in the default.** The release script builds arm64 unless `-a universal`; CI is arm64; the README
  calls the universal build experimental. Reason: the findings below.
- **Entitlements and plugin bundles.** `codesign` stores entitlements in executables only (checked with a dylib, a
  bundle and an executable). The standalone app shows the three; the AU and VST3 bundles accept `--entitlements` and
  drop it, so they carry the hardened-runtime flag only. A plugin runs in its host's process, so the host's entitlements
  decide whether the JIT may run there. We still pass the flag to all three (harmless, and notarization wants
  `runtime` + a timestamp on every bundle).
- **Proof that the entitlements matter:** `md-hash` signed with `--options runtime` and no entitlements stops with
  `JIT failed: 2 - InvalidArgument`; with the entitlements file it prints `hash 04ef1db4fe767721`; unhardened it passes
  (tested on copies of the arm64 tool).
- **Installer:** three component packages (`md.elektron-md.one.au` -> /Library/Audio/Plug-Ins/Components, `.vst3` ->
  /Library/Audio/Plug-Ins/VST3, `.standalone` -> /Applications), `BundleIsRelocatable` and `BundleIsVersionChecked` off
  (relocatable bundles are updated wherever the installer finds them, e.g. in a build folder), `BundleOverwriteAction
  upgrade`, `--ownership recommended`, system domain only, `hostArchitectures="arm64,x86_64"`, macOS 12+. All three
  bundles share the bundle id `md.elektron-md.one` (JUCE's BUNDLE_ID), which the installer tolerates here because none
  is relocatable.
- **Notarization order:** installer is signed with `MD_INSTALLER_IDENTITY`, submitted, stapled; the bundles are
  stapled; the `.zip` is made last so that it holds stapled bundles. Untested (no certificates here); `notarytool`
  1.1.3 exists on this Mac.
- Rosetta test runs use `-DCMAKE_CROSSCOMPILING_EMULATOR="arch;-x86_64"` (CMake prefixes every test command with it,
  also when not cross compiling): an arm64 `ctest` cannot be started with `arch -x86_64` ("Bad CPU type").

### Verification (OS 1.63, kits = MD_presets_1_50.syx, Apple Silicon, Apple clang 21)
- Universal configure/build: first try, no changes needed in `cmake/` or `ext/` (no arch-specific flags there; asmjit and
  dsp56300 choose the JIT back end by compiler macro). `lipo -archs` of md-hash, md-machines, md-kits, md-voicediag,
  md-tests, md-ui-tests, md-dsp-tests, MdPluginCheck, and the standalone, AU and VST3 executables: `x86_64 arm64`.
- arm64 build: all 10 ctest tests pass with `MD_OS_SYX` + `MD_KITS_SYX`; without them 5 report skipped and the rest
  pass (what CI sees). `md-hash` = `hash 04ef1db4fe767721`. The arm64 slice of the universal build: same.
- Signing: `codesign --verify --strict --deep` valid for the AU, VST3 and app (arm64 and universal builds);
  `codesign -d --entitlements -` on the app lists allow-jit, allow-unsigned-executable-memory, disable-library-validation;
  AU/VST3 flags `0x10002(adhoc,runtime)`.
- `auval -v aumu Mdo1 Mdrm` against the signed hardened build: **AU VALIDATION SUCCEEDED** (also `-strict`). The build
  could not be put in front of auval under its real ID without touching the installed `Moduledrum.component` (the
  registrar ignores a second component with the same type/subtype/manufacturer, whatever its version or bundle id), so
  a copy built from the same sources with another plugin code (Mdz1/Mdzz, product name MdAuCheck) was installed in
  `~/Library/Audio/Plug-Ins/Components` for the run and removed after. `auval -oop` of that copy opens it and passes
  everything except the "Parameter values are different since last set" meta-parameter check (a machine change rewrites
  the SYN values; unrelated to packaging; worth a Meta Param Flag later). The previously installed build cannot be opened
  out of process at all (`OpenAComponent: result: -1`).
- CI: the workflow's `run:` steps (configure, build, test, verify the signatures, zip) were run from a clean clone of the
  branch (submodules fetched from GitHub, JUCE downloaded by FetchContent, no OS file): all exit 0, ctest 10/10 with 5
  skipped. `brew install ninja` was not run (Ninja is installed); GitHub's runner and its Xcode were not available.
- Release script, run twice without `-n`:
  `dist/Moduledrum-0.1.0-macos-arm64.pkg` 7.6M, `.zip` 7.6M; `dist/Moduledrum-0.1.0-macos-universal.pkg` 16M, `.zip`
  16M; each with a `.sha256`. The `-n` path was exercised with stand-ins for `xcrun` (notarytool, stapler), `productbuild
  --sign` and `codesign` (no certificates here): control flow only, nothing was sent to Apple. `pkgutil --expand` shows the three component packages with install locations, root:wheel
  ownership in the BOMs, no signature on the .pkg (no installer identity here). Nothing was installed.

### Findings: the x86_64 slice under Rosetta 2
1. `arch -x86_64 md-hash <OS>` = `hash 04ef1db4fe767721` (also `MD_GROUPS=2`, `4`). ctest on the x86_64 slice: romart,
   firmware, golden-hash, kits, jit, resampler, plugin-os-load, plugin-editor pass.
2. **Lockstep (`MD_JIT_LOCKSTEP=1 md-hash`) fails on x86_64:** `registers differ after JIT block 136872 (from P:$1038c0
   to $1038ca, instruction 5596681): sr jit=80044 interp=80040`. Z is set on the x64 JIT after a `DO #$22` loop of
   LSL #12 on two accumulators (loaded from a table); the accumulators agree and are not zero, so the interpreter and the
   aarch64 JIT (no difference over the whole run) are right. Cause, from reading the source (not confirmed by a run,
   Rosetta being wedged, point 4): `alu_lsl`/`alu_lsr` in `ext/dsp56300/source/dsp56kEmu/jitops_alu_x64.cpp` (lines 144
   and 168) open `CcrBatchUpdate(N | C | V)`, which clears those bits and makes `ccr_update()` OR-only, but then set Z
   with `ccr_update_ifZero(CCRB_Z)`: a Z = 1 from a zero table word earlier in the loop is never cleared again. Fix:
   add `CCR_Z` to both masks (in `ext/patches/0001-dsp56300-md.patch`, with a test in `tests/test_jit.cpp`: LSL of zero
   then of a non-zero value, JIT against interpreter; that test would fail on x64 now). Both files are outside this
   increment's territory, so it is not done. No audio effect in the golden pattern.
3. **Intermittent SIGSEGV (null dereference in JIT code, main thread) in plugin-render and plugin-state** in the one full
   ctest run made under heavy machine load; 4/4 and 15/15 later serial runs passed; with 8 copies in parallel
   `--check-render` crashed in 21 of 24 runs, `--check-state` in 0 of 24. arm64 never crashes (also with MallocScribble).
   Crash reports: faulting thread main, rip in translated JIT memory, KERN_INVALID_ADDRESS at 0x0 / 0x8. Not known
   whether it is the x64 JIT or Rosetta's self-modifying-code tracking of asmjit's plain RWX JIT memory (asmjit uses
   MAP_JIT only in hardened processes). The interpreter-only x86_64 build under the same load (build-x64-nojit config:
   `-DCMAKE_OSX_ARCHITECTURES=x86_64 -DMD_USE_JIT=OFF`) and an Intel Mac are the discriminating tests; the first could
   not be run (next point).
4. **Rosetta wedged on this Mac during the work.** An lldb session on the x86_64 slice hung (0 % CPU) and was killed;
   afterwards every binary Rosetta had not seen before hangs at exec in state U (a freshly compiled hello world too),
   and `kill -9` does not end those processes (about 12 of mine are stuck: build-x64-nojit tests, a debug md-hash,
   build-universal md-hash). `oahd` needs a restart (root: `sudo killall oahd`) or a reboot. Whether lldb caused it is not
   known (an x64-JIT md-hash from another session's scratch build, started about 14:00, was stuck too).

## Remaining
- [ ] Fix the x64 lockstep finding (CCR_Z in the LSL/LSR batch masks, see above) and check it with `MD_JIT_LOCKSTEP=1` under
      Rosetta/Intel; explain the Rosetta crashes (finding 3), compare with the interpreter-only x86_64
      build, ideally on an Intel Mac; then make universal the release default
- [ ] Owner: Developer ID Application and Installer certificates, a `notarytool store-credentials` profile; run
      `release/build_release.sh -s ... -n` once (the notarization path is untested)
- [ ] Owner: load the packaged build in a DAW (Logic/Ableton/Reaper) and check that it survives project save/reload;
      note that the AU/VST3 get no entitlements of their own, so a hardened host must allow JIT itself
- [ ] First run of the CI workflow on GitHub (action versions, Xcode 15 clang on macos-14)
