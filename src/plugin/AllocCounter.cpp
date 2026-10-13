// See AllocCounter.h. The global operator new/delete replacements below apply to the whole md-plugin-check
// executable (JUCE, the engine and the emulators included).
#include "AllocCounter.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <unordered_map>
#if defined(_WIN32)
#include <malloc.h>   // _aligned_malloc / _aligned_free
#endif
#if defined(__APPLE__)
#include <dlfcn.h>
#include <execinfo.h>
// libmalloc's logging hook (what MallocStackLogging uses), exported by libsystem_malloc: while it is set, every
// allocation and free in every malloc zone calls it, fast paths included
extern "C" void (*malloc_logger)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result, uint32_t skip);
#endif

namespace md::plugin::check {
namespace {

std::atomic<bool> g_active{false};
std::thread::id g_thread{};   // written before g_active's release store, read after its acquire load
std::atomic<uint64_t> g_all{0}, g_news{0};

inline bool counting()
{
    return g_active.load(std::memory_order_acquire) && std::this_thread::get_id() == g_thread;
}

// The call stack of each allocation while counting, sorted by end(); those past the table stay unsorted (and so count
// as outside the JIT)
constexpr int kMaxStacks = 16384, kFrames = 32;
void* g_stacks[kMaxStacks][kFrames];
uint8_t g_depth[kMaxStacks];
bool g_inJit[kMaxStacks];
std::atomic<int> g_stackCount{0};
int g_kept = 0;

#if defined(__APPLE__)
constexpr uint32_t kLogAllocate = 2;   // MALLOC_LOG_TYPE_ALLOCATE (realloc reports ALLOCATE | DEALLOCATE)
void (*g_previousLogger)(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) = nullptr;

void mallocHook(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip)
{
    if ((type & kLogAllocate) != 0 && counting()) {
        g_all.fetch_add(1, std::memory_order_relaxed);
        const int i = g_stackCount.fetch_add(1, std::memory_order_relaxed);
        if (i < kMaxStacks) g_depth[i] = uint8_t(backtrace(g_stacks[i], kFrames));   // a frame-pointer walk: no allocation
    }
    if (g_previousLogger != nullptr) g_previousLogger(type, a1, a2, a3, result, skip);
}

void installMallocHook()
{
    if (malloc_logger != mallocHook) {
        g_previousLogger = malloc_logger;
        malloc_logger = mallocHook;
    }
}

// Whether a return address lies in the JIT's block compiler (cached: dladdr reads the symbol table)
bool isJitCompileFrame(void* pc)
{
    static std::unordered_map<void*, bool> cache;
    if (const auto it = cache.find(pc); it != cache.end()) return it->second;
    Dl_info info{};
    bool jit = false;
    if (dladdr(pc, &info) != 0 && info.dli_sname != nullptr)
        jit = std::strstr(info.dli_sname, "6dsp56k13JitBlockChain6create") != nullptr   // dsp56k::JitBlockChain::create
           || std::strstr(info.dli_sname, "6dsp56k10funcCreate") != nullptr;            // dsp56k::funcCreate
    cache.emplace(pc, jit);
    return jit;
}
#endif

inline void countNew()
{
    if (!counting()) return;
    g_news.fetch_add(1, std::memory_order_relaxed);
#if !defined(__APPLE__)
    g_all.fetch_add(1, std::memory_order_relaxed);
#endif
}

void* allocate(std::size_t size)
{
    countNew();
    for (;;) {
        if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
        auto handler = std::get_new_handler();
        if (handler == nullptr) return nullptr;
        handler();
    }
}

// Over-aligned allocation and its free: the C runtime's aligned pair on Windows, posix_memalign/free elsewhere
void* allocateAligned(std::size_t size, std::align_val_t al)
{
    countNew();
    const auto align = std::max(std::size_t(al), sizeof(void*));
    for (;;) {
#if defined(_WIN32)
        if (void* p = _aligned_malloc(size == 0 ? 1 : size, align)) return p;
#else
        void* p = nullptr;
        if (posix_memalign(&p, align, size == 0 ? 1 : size) == 0) return p;
#endif
        auto handler = std::get_new_handler();
        if (handler == nullptr) return nullptr;
        handler();
    }
}

void freeAligned(void* p) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

} // namespace

void AllocCounter::begin()
{
#if defined(__APPLE__)
    installMallocHook();
#endif
    g_stackCount.store(0);
    g_all.store(0);
    g_news.store(0);
    g_thread = std::this_thread::get_id();
    g_active.store(true, std::memory_order_release);
}

AllocCounts AllocCounter::end()
{
    g_active.store(false, std::memory_order_release);
    AllocCounts c{g_all.load(), g_news.load(), 0};
    g_kept = std::min(g_stackCount.load(), kMaxStacks);
#if defined(__APPLE__)
    for (int i = 0; i < g_kept; ++i) {
        g_inJit[i] = std::any_of(g_stacks[i], g_stacks[i] + g_depth[i], isJitCompileFrame);
        c.jit += g_inJit[i] ? 1 : 0;
    }
#endif
    return c;
}

bool AllocCounter::mallocHooked()
{
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

bool AllocCounter::selfTest()
{
    begin();
    int* volatile a = new int[7];
    a[0] = 1;
    delete[] a;
    void* volatile b = std::malloc(40);
    std::free(b);
    const auto c = end();
    return c.news == 1 && c.all == (mallocHooked() ? 2u : 1u) && c.jit == 0;
}

std::string AllocCounter::report(int maxStacks)
{
    std::string s;
#if defined(__APPLE__)
    int shown = 0;
    for (int i = 0; i < g_kept && shown < maxStacks; ++i) {
        if (g_inJit[i]) continue;
        s += "    allocation " + std::to_string(i + 1) + ":\n";
        char** names = backtrace_symbols(g_stacks[i], g_depth[i]);
        for (int f = 0; f < g_depth[i]; ++f) s += std::string("      ") + (names ? names[f] : "?") + "\n";
        std::free(names);
        ++shown;
    }
#else
    (void) maxStacks;
#endif
    return s;
}

} // namespace md::plugin::check

using md::plugin::check::allocate;
using md::plugin::check::allocateAligned;
using md::plugin::check::freeAligned;

void* operator new(std::size_t n) { if (void* p = allocate(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = allocate(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void* operator new(std::size_t n, std::align_val_t a) { if (void* p = allocateAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n, std::align_val_t a) { if (void* p = allocateAligned(n, a)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned(n, a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocateAligned(n, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(p); }
