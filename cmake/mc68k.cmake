# The 68k (ColdFire) emulator: Musashi (MIT) inside gearmulator's mc68k wrapper (GPLv3), joelanders' Machinedrum fork
# (ext/mc68k submodule). Used only for MachineRunner, which runs the OS's own machine coefficient functions; there is
# no full-system emulation (see src/core/md/MachineRunner.h). Only the static library is built.
set(MD_MC68K_DIR "${CMAKE_SOURCE_DIR}/ext/mc68k" CACHE PATH "mc68k checkout; defaults to this repo's own submodule")
if(NOT EXISTS "${MD_MC68K_DIR}/Musashi/m68k.h")
  message(FATAL_ERROR "mc68k not found at ${MD_MC68K_DIR} -- run 'git submodule update --init --recursive ext' "
                      "first, or pass -DMD_MC68K_DIR=<an existing checkout>")
endif()

# Its CMakeLists.txt includes CTest and adds its unit tests when BUILD_TESTING is on: keep them out of this build.
function(_md_add_mc68k)   # a function scope: BUILD_TESTING is changed for this subdirectory only
  set(BUILD_TESTING OFF)
  add_subdirectory("${MD_MC68K_DIR}" "${CMAKE_BINARY_DIR}/mc68k" EXCLUDE_FROM_ALL)
endfunction()
_md_add_mc68k()

set_target_properties(68kEmu PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(68kEmu PRIVATE -w)
# Consumers include "mc68k/mc68k.h" and "Musashi/m68k.h"
get_filename_component(_mc68k_parent "${MD_MC68K_DIR}" DIRECTORY)
target_include_directories(68kEmu SYSTEM INTERFACE "${_mc68k_parent}" "${MD_MC68K_DIR}")
