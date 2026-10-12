# DSP56300 emulator (GPLv3, https://github.com/dsp56300/dsp56300), sd88me's fork (ext/dsp56300 submodule): upstream
# plus the aarch64 JIT fixes and Monomodule's patch (saturation mode, MPYRI/PFLUSH; the "MNM patch" markers), so it is
# built as is. Only the emulator's static libraries are built: its own top-level CMake project adds tools and global
# optimisation flags this project does not want.
set(MD_DSP56300_DIR "${CMAKE_SOURCE_DIR}/ext/dsp56300" CACHE PATH
    "dsp56300 checkout; defaults to this repo's own submodule")
if(NOT EXISTS "${MD_DSP56300_DIR}/source/dsp56kEmu/dsp.h" OR NOT EXISTS "${MD_DSP56300_DIR}/source/asmjit/CMakeLists.txt")
  message(FATAL_ERROR "dsp56300 not found at ${MD_DSP56300_DIR} -- run 'git submodule update --init --recursive ext' "
                      "first, or pass -DMD_DSP56300_DIR=<an existing checkout>")
endif()

set(ASMJIT_STATIC TRUE)
set(ASMJIT_NO_INSTALL TRUE)
add_subdirectory("${MD_DSP56300_DIR}/source/asmjit"     "${CMAKE_BINARY_DIR}/dsp56300/asmjit"     EXCLUDE_FROM_ALL)
add_subdirectory("${MD_DSP56300_DIR}/source/dsp56kBase" "${CMAKE_BINARY_DIR}/dsp56300/dsp56kBase" EXCLUDE_FROM_ALL)
add_subdirectory("${MD_DSP56300_DIR}/source/dsp56kEmu"  "${CMAKE_BINARY_DIR}/dsp56300/dsp56kEmu"  EXCLUDE_FROM_ALL)
set(_dsp_libs asmjit dsp56kBase dsp56kEmu)
if(WIN32 OR (UNIX AND NOT APPLE))   # dsp56kEmu links Intel's JIT profiling API there, as dsp56300's own build does
  add_subdirectory("${MD_DSP56300_DIR}/source/vtuneSdk" "${CMAKE_BINARY_DIR}/dsp56300/vtuneSdk" EXCLUDE_FROM_ALL)
  list(APPEND _dsp_libs vtuneSdk)
endif()
foreach(t ${_dsp_libs})
  set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_options(${t} PRIVATE -w)   # third-party warnings off, in their own code and in their headers elsewhere
  get_target_property(_inc ${t} INTERFACE_INCLUDE_DIRECTORIES)
  if(_inc)
    set_property(TARGET ${t} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_inc})
  endif()
endforeach()

# Interpreter by default. g_jitSupported (dspconfig.h) is a constant in the emulator's headers, so the define must
# reach every target that includes them, not only the emulator's own: PUBLIC on dsp56kBase carries it to dsp56kEmu,
# mdcore and everything linking those.
if(NOT MD_USE_JIT)
  target_compile_definitions(dsp56kBase PUBLIC DSP56K_NO_JIT_RUNTIME)
endif()
