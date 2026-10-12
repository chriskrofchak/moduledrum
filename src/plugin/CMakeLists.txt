# Copy each built plugin into the user's plugin folders (macOS ~/Library/Audio/Plug-Ins, Linux ~/.vst3). Off by
# default on Windows, where the system VST3 folder needs administrator rights.
if(WIN32)
  option(MD_COPY_PLUGINS "Copy the plugins to the system plugin folders after building" OFF)
else()
  option(MD_COPY_PLUGINS "Copy the plugins to the user's plugin folders after building" ON)
endif()

# As Monomodule: the hardened runtime may generate code (dsp56300's JIT, see MD_USE_JIT) and load unsigned libraries.
set(MD_HARDENED_OPTS com.apple.security.cs.allow-jit com.apple.security.cs.allow-unsigned-executable-memory com.apple.security.cs.disable-library-validation)

# Moduledrum: the Machinedrum engine as an instrument. The OS file is chosen at run time (shared by every Moduledrum
# plugin, see SharedSettings.h); nothing of it is compiled in.
juce_add_plugin(MdOne
  PRODUCT_NAME "Moduledrum"
  COMPANY_NAME "Moduledrum"
  BUNDLE_ID "md.elektron-md.one"
  PLUGIN_MANUFACTURER_CODE Mdrm
  PLUGIN_CODE Mdo1
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE   # the copy is added below, after the signing
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MD_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Sign the bundles with the hardened runtime and the JIT entitlements JUCE wrote (MD_CODESIGN_IDENTITY, cmake/codesign.cmake),
# then copy the signed ones: JUCE's own copy step would run first if COPY_PLUGIN_AFTER_BUILD were set above
include(${CMAKE_SOURCE_DIR}/cmake/codesign.cmake)
foreach(_md_format AU VST3 Standalone)
  md_codesign_bundle(MdOne_${_md_format})
endforeach()
if(MD_COPY_PLUGINS)
  juce_enable_copy_plugin_step(MdOne)
endif()

set(MD_ONE_SOURCES one/OneProcessor.cpp one/OneEditor.cpp one/MachinePicker.cpp one/KitBrowser.cpp one/SkinDialog.cpp one/Lcd.cpp one/SpecData.cpp
  KitLibrary.cpp MdRomArt.cpp)
target_sources(MdOne PRIVATE one/OneFactory.cpp ${MD_ONE_SOURCES})
target_include_directories(MdOne PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MdOne PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 MD_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(MdOne PRIVATE mdcore mddsp juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Headless checks of the processor's OS-file handling (a development tool, not installed; see Check.cpp)
juce_add_console_app(MdPluginCheck PRODUCT_NAME "md-plugin-check")
target_sources(MdPluginCheck PRIVATE Check.cpp AllocCounter.cpp ${MD_ONE_SOURCES})
target_include_directories(MdPluginCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MdPluginCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_STANDALONE_APPLICATION=1 MD_OS_SYX="${MD_OS_SYX}" MD_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(MdPluginCheck PRIVATE mdcore mddsp juce::juce_audio_utils juce::juce_recommended_config_flags)
if(MD_BUILD_TESTS)
  # OS-file handling: unconfigured start, a wrong file, the OS file (needs it: MD_OS_SYX or -DMD_OS_SYX=..., else skipped)
  add_test(NAME plugin-os-load COMMAND MdPluginCheck --check-os-load)
  set_tests_properties(plugin-os-load PROPERTIES SKIP_RETURN_CODE 77)
  # The audio path: the regression render's hash at 44.1 kHz, 48/96 kHz conversion, buses, tempo, MIDI (needs the OS file)
  add_test(NAME plugin-render COMMAND MdPluginCheck --check-render)
  set_tests_properties(plugin-render PROPERTIES SKIP_RETURN_CODE 77)
  # Parameters, machine changes, kits and the state round trip (needs the OS file; the kit snapshot, MD_KITS_SYX, adds the kit cases)
  add_test(NAME plugin-state COMMAND MdPluginCheck --check-state)
  set_tests_properties(plugin-state PROPERTIES SKIP_RETURN_CODE 77)
  # The editor, headless: knobs on their parameters (drag, shift, wheel, typed values, host changes), track select, the
  # machine picker, the LFO page's names, the mixer and globals, the kit folder and browser (needs the OS file; the kit
  # snapshot, MD_KITS_SYX, adds the browser's kit cases), every page rendered
  add_test(NAME plugin-editor COMMAND MdPluginCheck --check-editor)
  set_tests_properties(plugin-editor PROPERTIES SKIP_RETURN_CODE 77)
  # The real-time path: no heap allocation in processBlock once the test sequence has played once (needs the OS file)
  add_test(NAME plugin-rt COMMAND MdPluginCheck --check-rt)
  set_tests_properties(plugin-rt PROPERTIES SKIP_RETURN_CODE 77)
endif()
