cmake_minimum_required(VERSION 3.22)
# The minimum macOS the binaries load on. Set before project(), which would otherwise fix it to the build
# machine's OS version.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
# The release version: the bundles' version and the installer's. release/build_release.sh -v passes another one.
if(NOT DEFINED MD_VERSION)
  set(MD_VERSION 0.1.0)
endif()
project(Moduledrum VERSION ${MD_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MD_BUILD_PLUGIN "Build the plugins (needs JUCE, downloaded at configure time)" ON)
option(MD_BUILD_TESTS  "Build the unit tests" ON)
# The emulated DSP runs on dsp56300's JIT where it has one (aarch64, x86-64): bit for bit what the interpreter renders
# (md-hash, the machine sweep, the 64 preset kits, the plugin's regression render; .clew increment 0011) at about a
# quarter of its CPU. OFF defines DSP56K_NO_JIT_RUNTIME everywhere: the interpreter, the reference the JIT is checked
# against (MD_JIT_LOCKSTEP=1 runs both side by side).
option(MD_USE_JIT "Run the emulated DSP on dsp56300's JIT (OFF: its interpreter; the same audio at ~4x the CPU)" ON)

# Optional: your Machinedrum OS file. Used as the default by the command-line tools and by the tests that run the
# engine (they are skipped without it; the MD_OS_SYX environment variable overrides it). The plugins never use it:
# they ask for the file at run time.
set(MD_OS_SYX "" CACHE FILEPATH "Optional: path to Elektron_SPS1-1UW_OS1.63.syx for the tests and CLI tools")

include(cmake/dsp56300.cmake)
include(cmake/mc68k.cmake)

add_subdirectory(src/core)
add_subdirectory(src/cli)
if(MD_BUILD_TESTS)
  enable_testing()   # after the emulators: their own add_test() calls stay out of ctest
  add_subdirectory(tests)
endif()
if(MD_BUILD_PLUGIN)
  include(cmake/juce.cmake)
  add_subdirectory(src/plugin)
endif()
