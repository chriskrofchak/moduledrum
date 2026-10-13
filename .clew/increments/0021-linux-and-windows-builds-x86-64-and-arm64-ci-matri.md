---
id: 21
status: in_progress
created_at: 2026-10-13T01:00:50Z
updated_at: 2026-10-13T01:00:50Z
---
## Why
Moduledrum only ships for macOS on Apple Silicon. Beta testers on Windows 10 x86-64 and Linux want to try it, and the
x86-64 JIT back end has only ever run under Rosetta: a real x86-64 machine has to confirm the golden hash before the
Intel slice becomes the default. Monomodule (the model for the plugin shell) already builds and ships Linux and Windows
with the same emulator and the same JUCE setup, so the recipe is known.

## Scope (decided 2026-10-12)
The top two rows of the architecture grid: arm64 (Apple Silicon, Linux aarch64, Windows on ARM as experimental) and
x86-64 (Intel Mac, Linux, Windows 10+). Not in scope: 32-bit ARM (interpreter only), anything else, AAX, CLAP, LV2,
a Windows installer (a zip is enough for the beta; Inno Setup later), Authenticode signing.

## What the research found (full write-up: the shareable page from this session, and docs/BETA-TESTING.md)
- Linux: no engine code is Apple-only. dsp56300 keeps DSP memory in /dev/shm (shm_open); Docker needs --shm-size.
  The CMake already links vtuneSdk off-Apple and copies to ~/.vst3. JUCE needs the usual X11/ALSA/freetype/GTK dev packages.
- Windows: VST3 + Standalone only. Needs MSVC defines (_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS), /utf-8 /bigobj,
  a static CRT, and /w /permissive- /constexpr:steps10000000 on the emulator libraries instead of -w (cmake/dsp56300.cmake,
  cmake/mc68k.cmake). The check tool used mkdtemp/setenv (Check.cpp) and pthreads/posix_memalign (AllocCounter.cpp).
  dsp56300's mmuhelper uses VirtualAlloc2 (Windows 10 1803+) with a fallback. asmjit's JIT needs no entitlement on Windows.
- The x64 JIT fix (Z after LSL/LSR) is already fix 4 of ext/patches/0001-dsp56300-md.patch with its tests (0018).
  Nothing more is known to be needed in the JIT; what is missing is a run on real x86-64 hardware.
- A tester's one-command check of bit-exactness: md-hash with their own OS 1.63 file must print 04ef1db4fe767721;
  MD_JIT_LOCKSTEP=1 pins down any difference. The md-hash CLI therefore goes into every beta zip.

## Tasks
- [x] MSVC build settings at the top level and on the emulator libraries (as Monomodule)
- [x] MD_OS_SYX as a forward-slash path in the compile definition (a Windows path's backslashes are escapes in C++)
- [x] Check.cpp: a portable temp settings folder and environment variable (no mkdtemp/setenv)
- [x] AllocCounter.cpp: no pthreads, aligned allocation on Windows (_aligned_malloc/_aligned_free)
- [x] cmake/stage_release.cmake: Moduledrum-<version>-<platform>-<arch>.zip (VST3, Standalone, AU on macOS, md-hash, docs)
- [x] .github/workflows/build.yml: macos-14 (universal), ubuntu-24.04, ubuntu-24.04-arm, windows-2022, windows-11-arm
      (experimental, continue-on-error); ctest everywhere (the OS-file tests skip); zips as artifacts; draft release on a v* tag
- [x] docs/BETA-TESTING.md (what to download, where to put it, the md-hash check, what to report), README and DEVELOPMENT.md
- [x] Verify: the arm64 macOS build and ctest still pass locally (16/16 with the OS and kit files, md-hash 04ef1db4fe767721,
      the staging zip complete)
- [x] Verify: Linux aarch64 in an Ubuntu 24.04 Docker container (GCC 13.3), 15/16 with the OS and kit files (plugin-rt
      skips: its malloc hook is macOS-only), md-hash 04ef1db4fe767721, the linux-arm64 zip staged. Finding: ext/mc68k on the include path shadowed glibc's <endian.h> (JUCE's C sources failed); fixed in cmake/mc68k.cmake,
      src/core/CMakeLists.txt (68kEmu private) and MachineRunner.cpp (mc68k/Musashi/m68k.h)
- [ ] Verify on CI: Linux x64 and arm64 and Windows x64 build and pass the OS-free tests (needs a push)
- [ ] Beta: a Windows 10 x86-64 tester reports md-hash = 04ef1db4fe767721 and the VST3 loads in their DAW
- [ ] Then: an Intel Mac or Linux x64 report closes 0018's "real hardware" task; flip build_release.sh -a to universal

## Done so far (2026-10-12, evening)
Code: `CMakeLists.txt` (MSVC block, MD_OS_SYX path), `cmake/dsp56300.cmake` and `cmake/mc68k.cmake` (MSVC warning flags),
`src/plugin/Check.cpp` (portable settings folder and env), `src/plugin/AllocCounter.cpp` (std::thread::id, _aligned_malloc).
New: `cmake/stage_release.cmake`, `docs/BETA-TESTING.md`; `.github/workflows/build.yml` is the five-job matrix. Docs: README
(installing per platform, Linux and Windows build sections, options, CI, known limitations), DEVELOPMENT.md (a section and
the open-work entry). Windows and Linux compile only on CI so far: the first push shows whether the vendored engine and the
mc68k fork take MSVC and GCC as Monomodule's do (the grep for GCC-only constructs found none).
