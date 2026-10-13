# DSP56300 emulator (GPLv3, https://github.com/dsp56300/dsp56300), sd88me's fork (ext/dsp56300 submodule): upstream
# plus the aarch64 JIT fixes and Monomodule's patch (saturation mode, MPYRI/PFLUSH; the "MNM patch" markers), plus this
# project's own JIT fixes (ext/patches/0001-dsp56300-md.patch, the "MD patch" markers), applied below. Only the
# emulator's static libraries are built: its own top-level CMake project adds tools and global optimisation flags this
# project does not want.
set(MD_DSP56300_DIR "${CMAKE_SOURCE_DIR}/ext/dsp56300" CACHE PATH
    "dsp56300 checkout; defaults to this repo's own submodule")
if(NOT EXISTS "${MD_DSP56300_DIR}/source/dsp56kEmu/dsp.h" OR NOT EXISTS "${MD_DSP56300_DIR}/source/asmjit/CMakeLists.txt")
  message(FATAL_ERROR "dsp56300 not found at ${MD_DSP56300_DIR} -- run 'git submodule update --init --recursive ext' "
                      "first, or pass -DMD_DSP56300_DIR=<an existing checkout>")
endif()

# ext/patches/0001-dsp56300-md.patch goes into the checkout itself, at configure time, whether or not the build uses the
# JIT (build directories share the checkout; the patch only changes JIT code). Idempotent: a checkout that already has
# it is left alone. Each build directory keeps a copy of what it applied, so a newer version of the patch replaces the
# old one; anything else in the way (other local changes to those files) stops the configure. Editing the patch, or
# restoring the patched files (git checkout, submodule update), re-runs this on the next build. MD_DSP56300_PATCH empty:
# build the checkout as it is (one that has the fixes already, or to see what they change).
set(MD_DSP56300_PATCH "${CMAKE_SOURCE_DIR}/ext/patches/0001-dsp56300-md.patch" CACHE FILEPATH
    "Patch applied to the dsp56300 checkout at configure time; empty = none")
function(md_patch_dsp56300 _dir _patch)
  find_package(Git REQUIRED QUIET)
  set(_applied "${CMAKE_BINARY_DIR}/dsp56300-md.patch.applied")
  file(SHA256 "${_patch}" _hash)
  file(STRINGS "${_patch}" _targets REGEX "^\\+\\+\\+ b/")
  list(TRANSFORM _targets REPLACE "^\\+\\+\\+ b/" "${_dir}/")
  set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}" ${_targets})
  macro(_md_git_apply _result)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn ${ARGN} WORKING_DIRECTORY "${_dir}"
                    RESULT_VARIABLE ${_result} OUTPUT_QUIET ERROR_QUIET)
  endmacro()
  _md_git_apply(_rc --reverse --check "${_patch}")
  if(NOT _rc EQUAL 0)
    _md_git_apply(_rc --check "${_patch}")
    if(NOT _rc EQUAL 0 AND EXISTS "${_applied}")   # an older version of the patch, applied by this build directory
      _md_git_apply(_rc --reverse --check "${_applied}")
      if(_rc EQUAL 0)
        _md_git_apply(_rc --reverse "${_applied}")
        message(STATUS "dsp56300: removed the previous version of ${_patch}")
        _md_git_apply(_rc --check "${_patch}")
      endif()
    endif()
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "dsp56300: cannot apply ${_patch} to ${_dir} (other local changes in the files it patches?). "
                          "Restore them ('git -C ${_dir} checkout -- source') and configure again.")
    endif()
    _md_git_apply(_rc "${_patch}")
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "dsp56300: applying ${_patch} failed")
    endif()
    message(STATUS "dsp56300: applied ${_patch}")
  endif()
  configure_file("${_patch}" "${_applied}" COPYONLY)
  get_filename_component(_name "${_patch}" NAME)
  message(STATUS "dsp56300: ${_name} in place (sha256 ${_hash})")
endfunction()
if(MD_DSP56300_PATCH)
  md_patch_dsp56300("${MD_DSP56300_DIR}" "${MD_DSP56300_PATCH}")
else()
  message(STATUS "dsp56300: no patch (MD_DSP56300_PATCH is empty), building ${MD_DSP56300_DIR} as it is")
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
  if(MSVC)
    # as dsp56300's own build: conformance mode (without it MSVC finds DSP::writeMem ambiguous), room for its large
    # compile-time tables, third-party warnings off
    target_compile_options(${t} PRIVATE /w /permissive- /constexpr:steps10000000)
  else()
    target_compile_options(${t} PRIVATE -w)   # third-party warnings off, in their own code and in their headers elsewhere
  endif()
  get_target_property(_inc ${t} INTERFACE_INCLUDE_DIRECTORIES)
  if(_inc)
    set_property(TARGET ${t} APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_inc})
  endif()
endforeach()

# MD_USE_JIT=OFF: the interpreter. g_jitSupported (dspconfig.h) is a constant in the emulator's headers, so the define
# must reach every target that includes them, not only the emulator's own: PUBLIC on dsp56kBase carries it to dsp56kEmu,
# mdcore and everything linking those (a target without it would disagree with the others about g_useJIT).
if(NOT MD_USE_JIT)
  target_compile_definitions(dsp56kBase PUBLIC DSP56K_NO_JIT_RUNTIME)
endif()
