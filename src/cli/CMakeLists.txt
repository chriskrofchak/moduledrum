# Command-line tools. MD_OS_SYX (CMake option) is their default OS file; the MD_OS_SYX environment variable overrides it.

# md-hash: an audio hash of a fixed pattern, to compare builds (the golden test runs the same pattern)
add_executable(md-hash md_hash.cpp)
target_link_libraries(md-hash PRIVATE mdcore)
target_compile_definitions(md-hash PRIVATE MD_OS_SYX="${MD_OS_SYX}")

# md-machines: lists the OS's machine table (names, SYN labels) or computes one machine's voice-slot words
add_executable(md-machines md_machines.cpp)
target_link_libraries(md-machines PRIVATE mdcore)
target_compile_definitions(md-machines PRIVATE MD_OS_SYX="${MD_OS_SYX}")

# md-kits: lists the kits of a Machinedrum .syx file and renders one through the engine to a WAV
add_executable(md-kits md_kits.cpp)
target_link_libraries(md-kits PRIVATE mdcore)
target_compile_definitions(md-kits PRIVATE MD_OS_SYX="${MD_OS_SYX}")
