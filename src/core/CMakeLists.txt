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
target_link_libraries(mdcore PUBLIC dsp56kEmu 68kEmu)
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
