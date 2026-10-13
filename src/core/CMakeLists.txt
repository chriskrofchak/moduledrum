# mdcore: the Machinedrum engine, vendored from sd88me/mpc-vst-machinedrum (engine/ and tools/mdfw/, see
# THIRD_PARTY_NOTICES.md). md/Engine.cpp is upstream's empty stub, kept for diffing and not built (as upstream).
add_library(mdcore STATIC
  md/Firmware.cpp
  md/HostModel.cpp
  md/MachineRunner.cpp
  md/MasterFx.cpp
  md/Mixer.cpp
  md/ParallelVoiceEngine.cpp
  md/TrackFx.cpp
  md/VoiceEngine.cpp
  # ours, not upstream's: sysex kit files (md/library) and the WAV writer (util)
  md/library/MdSysex.cpp
  util/Wav.cpp
)
target_include_directories(mdcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The vendored headers are system headers to their consumers: upstream code under the plugin's stricter JUCE warning flags
target_include_directories(mdcore SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/md)
# 68kEmu is private: only MachineRunner.cpp includes it (its headers must not reach consumers, see cmake/mc68k.cmake)
target_link_libraries(mdcore PUBLIC dsp56kEmu PRIVATE 68kEmu)

# RAM per emulated DSP (increment 0009): a booted DSP took 358 MB (JIT; the MPC port's HANDOFF said 420-500), two of them in the
# engine with the master effects 837 MB. Two causes, both in the emulator's defaults:
# - the audio ring buffers of its ESSI ports (2 x 32768 frames each, 80 MB per DSP): nothing here streams audio through them
#   (the harnesses feed and drop the ESSI frames through callbacks), and dsp56300 has a switch for hosts like that. PUBLIC:
#   the Audio class layout must agree in every target that includes audio.h.
target_compile_definitions(dsp56kEmu PUBLIC DSP56300_AUDIO_RINGBUFFER_SIZE=64)
# - MmuHelper::mapRegion's mlock() of every alias of the DSP's memory (192 MB of wired pages): ext/patches/0002-dsp56300-md-memory.patch
#   takes it out. Applied here, idempotently, as cmake/dsp56300.cmake applies 0001 (when MD_DSP56300_PATCH is set, i.e. when
#   this project's patches are wanted); a checkout that cannot take it just keeps the 192 MB per DSP. The audio is identical
#   either way (it is memory accounting, not emulation).
set(MD_DSP56300_MEMORY_PATCH "${CMAKE_SOURCE_DIR}/ext/patches/0002-dsp56300-md-memory.patch" CACHE FILEPATH
    "Patch for the emulator's memory footprint (no mlock), applied after MD_DSP56300_PATCH; empty = none")
if(MD_DSP56300_PATCH AND MD_DSP56300_MEMORY_PATCH AND EXISTS "${MD_DSP56300_MEMORY_PATCH}")
  find_package(Git QUIET)
  if(GIT_FOUND)
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${MD_DSP56300_MEMORY_PATCH}" "${MD_DSP56300_DIR}/source/dsp56kBase/mmuhelper.cpp")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn --reverse --check "${MD_DSP56300_MEMORY_PATCH}"
                    WORKING_DIRECTORY "${MD_DSP56300_DIR}" RESULT_VARIABLE _md_mem_applied OUTPUT_QUIET ERROR_QUIET)
    if(NOT _md_mem_applied EQUAL 0)
      execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${MD_DSP56300_MEMORY_PATCH}"
                      WORKING_DIRECTORY "${MD_DSP56300_DIR}" RESULT_VARIABLE _md_mem_result OUTPUT_QUIET ERROR_QUIET)
      if(_md_mem_result EQUAL 0)
        message(STATUS "dsp56300: applied ${MD_DSP56300_MEMORY_PATCH}")
      else()
        message(WARNING "dsp56300: cannot apply ${MD_DSP56300_MEMORY_PATCH} to ${MD_DSP56300_DIR}; every emulated DSP keeps "
                        "its 192 MB of wired memory (nothing else changes)")
      endif()
    endif()
  endif()
endif()

if(NOT MD_USE_JIT)
  target_compile_definitions(mdcore PUBLIC DSP56K_NO_JIT_RUNTIME)   # also via dsp56kBase; stated here for readers
endif()

# mddsp: the plugin's conversion between the engine's 44.1 kHz and the host's rate (dsp/Resampler, from Monomodule)
add_library(mddsp STATIC dsp/Resampler.cpp)
target_include_directories(mddsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link-time optimisation across the engine and the emulators, as the MPC port builds them: the interpreter's hot path
# spans their libraries (md-hash 2.13 s instead of 2.40 s on an M-series Mac). Whatever links them does the LTO step.
# (asmjit is left out: only the JIT uses it, and its CMake project ignores the property.)
include(CheckIPOSupported)
check_ipo_supported(RESULT _md_ipo LANGUAGES C CXX)
if(_md_ipo)
  set_target_properties(mdcore dsp56kEmu dsp56kBase 68kEmu PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()
