# Collects a built release into one zip: the plugins, the standalone, the md-hash tool and the licence files, as
# dist/Moduledrum-<version>-<platform>-<arch>.zip. Used by .github/workflows/build.yml on every platform;
# release/build_release.sh makes the macOS installer separately (.clew increment 0021).
#
#   cmake -DBUILD_DIR=build -DOUT_DIR=dist -DPLATFORM=macos|linux|windows [-DARCH=x64|arm64|universal] [-DCONFIG=Release]
#         -P cmake/stage_release.cmake
#
# The plugins must already be built (Release). ARCH defaults to the build machine's; the version is MD_VERSION in
# CMakeLists.txt. The zip holds VST3/, Standalone/, AU/ (macOS only), Tools/md-hash (the bit-exactness check every beta
# tester runs with their own OS file, see docs/BETA-TESTING.md) and the licence files. Nothing from the OS file is in it.
foreach(v BUILD_DIR OUT_DIR PLATFORM)
  if(NOT ${v})
    message(FATAL_ERROR "stage_release: -D${v}=... is required")
  endif()
endforeach()
if(NOT PLATFORM MATCHES "^(macos|linux|windows)$")
  message(FATAL_ERROR "stage_release: PLATFORM is macos, linux or windows, not '${PLATFORM}'")
endif()
if(NOT CONFIG)
  set(CONFIG Release)
endif()
get_filename_component(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
get_filename_component(OUT_DIR "${OUT_DIR}" ABSOLUTE)

file(STRINGS "${SRC_DIR}/CMakeLists.txt" _ver REGEX "^ *set\\(MD_VERSION [0-9.]+\\)")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" VERSION "${_ver}")
if(NOT VERSION)
  message(FATAL_ERROR "stage_release: no MD_VERSION in ${SRC_DIR}/CMakeLists.txt")
endif()
if(NOT ARCH)
  cmake_host_system_information(RESULT ARCH QUERY OS_PLATFORM)
  string(TOLOWER "${ARCH}" ARCH)
  if(ARCH MATCHES "^(x86_64|amd64)$")
    set(ARCH x64)
  elseif(ARCH MATCHES "^(aarch64|arm64)$")
    set(ARCH arm64)
  endif()
endif()

set(NAME "Moduledrum-${VERSION}-${PLATFORM}-${ARCH}")
set(STAGE "${OUT_DIR}/${NAME}")
file(REMOVE_RECURSE "${STAGE}" "${OUT_DIR}/${NAME}.zip")
file(MAKE_DIRECTORY "${STAGE}/VST3" "${STAGE}/Standalone" "${STAGE}/Tools")

set(ART "${BUILD_DIR}/src/plugin/MdOne_artefacts/${CONFIG}")
if(NOT EXISTS "${ART}/VST3/Moduledrum.vst3")
  message(FATAL_ERROR "stage_release: ${ART}/VST3/Moduledrum.vst3 was not built")
endif()

# On macOS the bundles are copied with ditto, which keeps the signatures and the executable bits exactly; elsewhere
# file(COPY) does (it keeps permissions).
function(stage_copy _from _to_dir)
  if(PLATFORM STREQUAL "macos")
    get_filename_component(_name "${_from}" NAME)
    execute_process(COMMAND ditto "${_from}" "${_to_dir}/${_name}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "stage_release: ditto ${_from} failed")
    endif()
  else()
    file(COPY "${_from}" DESTINATION "${_to_dir}")
  endif()
endfunction()

stage_copy("${ART}/VST3/Moduledrum.vst3" "${STAGE}/VST3")
if(PLATFORM STREQUAL "macos")
  file(MAKE_DIRECTORY "${STAGE}/AU")
  stage_copy("${ART}/AU/Moduledrum.component" "${STAGE}/AU")
endif()
file(GLOB _standalone "${ART}/Standalone/*")
foreach(f ${_standalone})
  stage_copy("${f}" "${STAGE}/Standalone")
endforeach()

# md-hash: build/src/cli/md-hash with a single-config generator, build/src/cli/<Config>/md-hash.exe with Visual Studio
file(GLOB _hash "${BUILD_DIR}/src/cli/md-hash" "${BUILD_DIR}/src/cli/md-hash.exe"
                "${BUILD_DIR}/src/cli/${CONFIG}/md-hash" "${BUILD_DIR}/src/cli/${CONFIG}/md-hash.exe")
if(NOT _hash)
  message(FATAL_ERROR "stage_release: md-hash was not built under ${BUILD_DIR}/src/cli")
endif()
list(GET _hash 0 _hash)
stage_copy("${_hash}" "${STAGE}/Tools")

file(COPY "${SRC_DIR}/LICENSE" "${SRC_DIR}/THIRD_PARTY_NOTICES.md" "${SRC_DIR}/README.md"
          "${SRC_DIR}/docs/BETA-TESTING.md" DESTINATION "${STAGE}")

if(PLATFORM STREQUAL "macos")
  execute_process(COMMAND ditto -c -k --keepParent "${NAME}" "${NAME}.zip" WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE rc)
else()
  execute_process(COMMAND ${CMAKE_COMMAND} -E tar cf "${NAME}.zip" --format=zip "${NAME}"
                  WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE rc)
endif()
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "stage_release: zip failed")
endif()
file(SHA256 "${OUT_DIR}/${NAME}.zip" _sha)
file(WRITE "${OUT_DIR}/${NAME}.zip.sha256" "${_sha}  ${NAME}.zip\n")
message(STATUS "staged ${OUT_DIR}/${NAME}.zip (sha256 ${_sha})")
