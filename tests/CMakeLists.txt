# Unit tests. The tests that run the engine need a Machinedrum OS file; without one (MD_OS_SYX environment variable
# or -DMD_OS_SYX=...) they are reported as skipped.
add_executable(md-tests
  mini_test.cpp
  test_firmware.cpp
  test_golden.cpp
  test_kits.cpp
)
target_link_libraries(md-tests PRIVATE mdcore)
target_compile_definitions(md-tests PRIVATE MD_OS_SYX="${MD_OS_SYX}")

# The plugin's LCD artwork reader (no JUCE). Its own executable: the artwork is process-wide state.
add_executable(md-ui-tests mini_test.cpp test_romart.cpp ${PROJECT_SOURCE_DIR}/src/plugin/MdRomArt.cpp)
target_include_directories(md-ui-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/plugin)
target_link_libraries(md-ui-tests PRIVATE mdcore)
target_compile_definitions(md-ui-tests PRIVATE MD_OS_SYX="${MD_OS_SYX}")
add_test(NAME romart COMMAND md-ui-tests romart_)
set_tests_properties(romart PROPERTIES SKIP_RETURN_CODE 77)

# md-tests <filter> runs the cases whose name contains <filter>; 77 = all of them skipped
add_test(NAME firmware COMMAND md-tests firmware_)
add_test(NAME golden-hash COMMAND md-tests golden_)
# kits: the sysex layout on a synthetic kit always runs; the 64-kit snapshot (MD_KITS_SYX) and the OS (MD_OS_SYX) add the rest
add_test(NAME kits COMMAND md-tests kits_)
set_tests_properties(firmware golden-hash kits PROPERTIES SKIP_RETURN_CODE 77)

# The host-rate converter (src/core/dsp; needs no OS file)
add_executable(md-dsp-tests mini_test.cpp test_resampler.cpp)
target_link_libraries(md-dsp-tests PRIVATE mddsp)
target_compile_definitions(md-dsp-tests PRIVATE MD_OS_SYX="${MD_OS_SYX}")
add_test(NAME resampler COMMAND md-dsp-tests resampler_)
