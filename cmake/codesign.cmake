# Code signing of the plugin bundles (macOS).
#
# A Ninja or Makefile build leaves each bundle with the linker's ad-hoc signature and no entitlements: JUCE writes the
# hardened-runtime entitlements (HARDENED_RUNTIME_OPTIONS in src/plugin/CMakeLists.txt: allow-jit,
# allow-unsigned-executable-memory, disable-library-validation) into <target>.entitlements, but only the Xcode
# generator applies them. The emulated DSP needs them: under the hardened runtime, which a Developer ID signature for
# notarization requires, a process without allow-jit cannot map the JIT's executable memory.
# md_codesign_bundle() therefore signs each bundle again after it is built, with the hardened runtime and the
# entitlements JUCE wrote. Call it before juce_enable_copy_plugin_step(), so that the copy in ~/Library/Audio/Plug-Ins
# is the signed one (post-build commands run in the order they are added).
# codesign stores entitlements in executables only: the standalone app shows the three (codesign -d --entitlements -);
# the AU and VST3 bundles are loadable code, which accepts and drops them. A plugin runs in its host's process, and
# the host's own entitlements decide whether it may use the JIT (the hardened DAWs that load plugins have them).
# The hardened-runtime flag and the timestamp are what notarization asks of every bundle.
#
#   MD_CODESIGN_IDENTITY   what to sign with: "-" (the default) is ad-hoc, which runs on this Mac only; for a release,
#                          the name or SHA-1 of a "Developer ID Application" certificate in the keychain
#   MD_CODESIGN_TIMESTAMP  the signature's timestamp: "none" (the default; offline, enough for local builds), "" for
#                          Apple's timestamp server (notarization needs it), or the URL of another server
# (A change of either alters the post-build command line, so the next build links and signs the bundles again.)
if(NOT APPLE)
  function(md_codesign_bundle _target)
  endfunction()
  return()
endif()

set(MD_CODESIGN_IDENTITY "-" CACHE STRING "Code signing identity for the plugin bundles ('-' = ad-hoc)")
set(MD_CODESIGN_TIMESTAMP "none" CACHE STRING "Signature timestamp: 'none', empty (Apple's server) or a server URL")
find_program(MD_CODESIGN_TOOL codesign REQUIRED)
mark_as_advanced(MD_CODESIGN_TOOL)

if(MD_CODESIGN_TIMESTAMP STREQUAL "")
  set(_md_codesign_timestamp "--timestamp")
else()
  set(_md_codesign_timestamp "--timestamp=${MD_CODESIGN_TIMESTAMP}")
endif()

function(md_codesign_bundle _target)
  get_target_property(_entitlements ${_target} XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS)
  if(NOT _entitlements)
    message(FATAL_ERROR "md_codesign_bundle: JUCE wrote no entitlements file for ${_target}")
  endif()
  add_custom_command(TARGET ${_target} POST_BUILD
    COMMAND "${MD_CODESIGN_TOOL}" --force --options runtime "${_md_codesign_timestamp}"
            --entitlements "${_entitlements}" -s "${MD_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:${_target}>"
    COMMENT "Signing ${_target} (${MD_CODESIGN_IDENTITY}, hardened runtime, JIT entitlements)"
    VERBATIM)
endfunction()
