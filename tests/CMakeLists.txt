# Unit tests. The tests that run the engine need a Machinedrum OS file; without one (MD_OS_SYX environment variable
# or -DMD_OS_SYX=...) they are reported as skipped.
add_executable(md-tests
  mini_test.cpp
  Dsp1Reference.cpp
  test_firmware.cpp
  test_golden.cpp
  test_jit.cpp
  test_kits.cpp
  test_master.cpp
)
target_include_directories(md-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
# jit: the dsp56300 JIT against its interpreter on small programs, one per fix in ext/patches (skipped with MD_USE_JIT=OFF)
add_test(NAME jit COMMAND md-tests jit_)
# master: MasterFx (the mixer DSP's master section on a second emulated DSP) against the interpreter and against DSP1's whole main loop
# (Dsp1Reference), its cost and that a live block compiles nothing; master-host: the OS side that works out its 38 words (HostModel);
# golden-master: the golden pattern's master output hash, identical on the JIT and the interpreter (the same constants in both builds)
add_test(NAME master COMMAND md-tests mfx_)
add_test(NAME master-host COMMAND md-tests mhost_)
add_test(NAME golden-master COMMAND md-tests gmaster_)
# master-ram: the footprint of a booted DSP (macOS), in a process of its own so that earlier tests' freed memory does not hide it
add_test(NAME master-ram COMMAND md-tests ram_)
set_tests_properties(firmware golden-hash kits jit master master-host golden-master master-ram PROPERTIES SKIP_RETURN_CODE 77)

# The host-rate converter (src/core/dsp; needs no OS file)
add_executable(md-dsp-tests mini_test.cpp test_resampler.cpp)
target_link_libraries(md-dsp-tests PRIVATE mddsp)
target_compile_definitions(md-dsp-tests PRIVATE MD_OS_SYX="${MD_OS_SYX}")
add_test(NAME resampler COMMAND md-dsp-tests resampler_)
