// Counts heap allocations made by one thread, for md-plugin-check's real-time checks (--check-rt, --bench): the
// audio path must not allocate. Linked into md-plugin-check only (AllocCounter.cpp replaces the global operator new
// and delete), never into the plugins.
//
// Two hooks: the replaced operator new (every C++ allocation, any platform) and, on macOS, libmalloc's malloc_logger,
// which sees every allocation through the system allocator, C++ or not (JUCE's HeapBlock uses std::malloc, so a growing
// MidiBuffer only shows up there). Only the thread that called begin() is counted. The hooks never allocate, lock or
// touch thread_local storage (whose first use can allocate on macOS); they keep each allocation's call stack (a
// frame-pointer walk into a preallocated table), and end() sorts them afterwards: an allocation made while dsp56300's
// JIT compiles a voice-program block (dsp56k::JitBlockChain::create or dsp56k::funcCreate on the stack) is counted
// apart, as the JIT compiles on the audio thread the first time the program reaches a block (inherent to the emulator).
// The stacks are read through the executable's symbol table: a stripped binary counts every allocation as the
// caller's, which fails a check rather than passing one.
#pragma once
#include <cstdint>
#include <string>

namespace md::plugin::check {

struct AllocCounts {
    uint64_t all = 0;   // every allocation (malloc level on macOS, else operator new)
    uint64_t news = 0;  // of which through operator new
    uint64_t jit = 0;   // of which inside a JIT compile (macOS; 0 elsewhere)
    uint64_t outsideJit() const { return all - jit; }
    AllocCounts& operator+=(const AllocCounts& o) { all += o.all; news += o.news; jit += o.jit; return *this; }
};

class AllocCounter {
public:
    // Counts the calling thread's allocations from now until end()
    static void begin();
    static AllocCounts end();
    // True when malloc-level counting (and the JIT attribution) works here (macOS); otherwise only operator new
    static bool mallocHooked();
    // Sanity check of the hooks: an operator new and a malloc on this thread are both seen, outside any JIT compile
    static bool selfTest();
    // The call stacks of the first allocations outside JIT compiles in the last begin()/end() (symbolised; allocates)
    static std::string report(int maxStacks = 4);
};

} // namespace md::plugin::check
