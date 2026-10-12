// The dsp56300 JIT against its interpreter on small hand-written programs: one test per fix in
// ext/patches/0001-dsp56300-md.patch (each fails without it). Every program runs on two fresh DSPs, one through the JIT
// (exec()) and one through the interpreter (execInterpreter()), until both reach the end; the registers and the memory
// the program writes must then be the same. Skipped in interpreter builds (MD_USE_JIT=OFF). Needs no OS file.
#include "mini_test.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

using namespace dsp56k;

namespace {
struct Core
{
    DefaultMemoryValidator validator;
    Memory mem{validator, 0x20000, 0x20000, 0x10000};   // P, X/Y, shared external memory from $10000
    Peripherals56303 periphX;
    PeripheralsNop periphY;
    DSP dsp{mem, &periphX, &periphY};
};

struct Program
{
    std::vector<std::string> code;                        // assembled from P:$100 on; the end is the word after it
    std::vector<std::pair<TWord, TWord>> x, y;            // initial X/Y memory
};

struct Result { std::unique_ptr<Core> jit, interp; };

Result runBoth(const Program& _p)
{
    if constexpr (!g_useJIT)
        throw mt::Skip{"interpreter build (MD_USE_JIT=OFF)"};
    Result r{std::make_unique<Core>(), std::make_unique<Core>()};
    r.interp->dsp.setInterpreterEnabled(true);
    constexpr TWord start = 0x100;
    TWord end = start;
    for (auto* c : {r.jit.get(), r.interp.get()}) {
        for (const auto& [a, v] : _p.x) c->dsp.memWrite(MemArea_X, a, v);
        for (const auto& [a, v] : _p.y) c->dsp.memWrite(MemArea_Y, a, v);
        Assembler as;
        TWord pc = start;
        for (const auto& line : _p.code) {
            const auto w = as.assemble(line.c_str());
            if (!w.success()) mt::fail(__FILE__, __LINE__, "cannot assemble '" + line + "'");
            c->dsp.memWriteP(pc++, w.word[0]);
            if (w.wordCount > 1) c->dsp.memWriteP(pc++, w.word[1]);
        }
        end = pc;
        c->dsp.memWriteP(pc, 0x0c0000 | pc);   // jmp to itself: the end
        c->dsp.setPC(start);
    }
    for (int n = 0; n < 10000 && r.jit->dsp.getPC().toWord() != end; ++n) r.jit->dsp.exec();
    for (int n = 0; n < 10000 && r.interp->dsp.getPC().toWord() != end; ++n) r.interp->dsp.execInterpreter();
    CHECK_EQ(r.jit->dsp.getPC().toWord(), end);
    CHECK_EQ(r.interp->dsp.getPC().toWord(), end);
    return r;
}

std::string hex(uint64_t v) { std::ostringstream o; o << std::hex << v; return o.str(); }

// every register the programs below touch, SR included
void checkSameRegisters(const Result& _r)
{
    const auto& a = _r.jit->dsp.regs();
    const auto& b = _r.interp->dsp.regs();
    auto same = [](const char* name, uint64_t jit, uint64_t interp) {
        if (jit != interp) mt::fail(__FILE__, __LINE__, std::string(name) + ": JIT " + hex(jit) + ", interpreter " + hex(interp));
    };
    same("x", a.x.var, b.x.var);
    same("y", a.y.var, b.y.var);
    same("a", a.a.var, b.a.var);
    same("b", a.b.var, b.b.var);
    for (int i = 0; i < 8; ++i) {
        same(("r" + std::to_string(i)).c_str(), a.r[i].var, b.r[i].var);
        same(("n" + std::to_string(i)).c_str(), a.n[i].var, b.n[i].var);
        same(("m" + std::to_string(i)).c_str(), a.m[i].var, b.m[i].var);
    }
    same("sr", _r.jit->dsp.getSR().var, _r.interp->dsp.getSR().var);
}
}

// Patch fix 1: a REP whose repeated instruction reads an M register written earlier in the block (the voice program's
// init has this at P:$10005d). The block used to end between the two, so the instruction ran 17 times.
TEST_CASE(jit_rep_keeps_its_repeated_instruction)
{
    const auto r = runBoth({{"move #>$200,r4", "move #>$40,n4", "move #>$ffffff,m4", "move #>$123456,x0",
                             "rep #16", "move x0,y:(r4)+n4", "nop"},
                            {}, {{0x600, 0xabcdef}}});
    checkSameRegisters(r);
    CHECK_EQ(r.jit->dsp.regs().r[4].var, 0x600);
    CHECK_EQ(r.jit->mem.get(MemArea_Y, 0x5c0), 0x123456u);
    CHECK_EQ(r.jit->mem.get(MemArea_Y, 0x600), 0xabcdefu);   // no 17th write
}

// Patch fix 2: a half of y (or x) written, then all 48 bits of y read, in one block: the write never reached the DSP's
// registers (5 of the 64 kits in Elektron's preset file rendered differently).
TEST_CASE(jit_xy_half_write_survives_a_48bit_read)
{
    const Program p{{"move #>$200,r1", "move #>$65,y1", "move #>$0,y0", "move #>$2,a1",
                     "do a1,$10c", "move x:(r1),y1", "sub y,a", "nop",   // the loop body is $10a-$10c
                     "nop"},
                    {{0x200, 0x111111}}, {}};
    const auto r = runBoth(p);
    checkSameRegisters(r);
    CHECK_EQ(r.jit->dsp.regs().y.var, 0x111111000000ull);
}

TEST_CASE(jit_xy_half_write_survives_a_48bit_read_outside_loops)
{
    const auto r = runBoth({{"move #>$200,r1", "move #>$65,y1", "move #>$0,y0",
                             "move l:(r1),y", "sub y,a", "move b,x0", "move x:(r1),x1", "add x,b", "nop"},
                            {{0x200, 0x111111}}, {{0x200, 0x222222}}});
    checkSameRegisters(r);
}

// Patch fix 3: ASL by a zero amount clears C (aarch64 set it for negative values); a multi-bit ASL that sets V sets L.
TEST_CASE(jit_asl_by_zero_clears_carry)
{
    checkSameRegisters(runBoth({{"move #>$946318,b", "move #>$0,x1", "asl x1,b,a", "nop"}, {}, {}}));
    checkSameRegisters(runBoth({{"move #>$946318,b", "ori #$1,ccr", "asl #0,b,a", "nop"}, {}, {}}));
    const auto r = runBoth({{"move #>$946318,b", "move #>$0,x1", "asl x1,b,a", "nop"}, {}, {}});
    CHECK_EQ(r.jit->dsp.getSR().var & CCR_C, 0);
}

TEST_CASE(jit_asl_overflow_sets_limit)
{
    checkSameRegisters(runBoth({{"move #>$400000,b", "move #>$9,x1", "asl x1,b,a", "nop"}, {}, {}}));
    const auto r = runBoth({{"move #>$400000,b", "asl #9,b,a", "nop"}, {}, {}});
    checkSameRegisters(r);
    CHECK((r.jit->dsp.getSR().var & (CCR_V | CCR_L)) == (CCR_V | CCR_L));
}

// Fix 4 (x86-64 JIT only; the aarch64 JIT never had the bug): a non-zero LSL/LSR after a zero result must clear Z, as the
// interpreter does. The zero shift leaves Z=1 in the same block, then the non-zero shift must take it away.
TEST_CASE(jit_lsl_after_zero_clears_z)
{
    const auto r = runBoth({{"move #>$0,a", "lsl #$c,a", "move #>$1f9fed,a", "lsl #$c,a", "nop"}, {}, {}});
    checkSameRegisters(r);
    CHECK((r.jit->dsp.getSR().var & CCR_Z) == 0);
}
TEST_CASE(jit_lsr_after_zero_clears_z)
{
    const auto r = runBoth({{"move #>$0,b", "lsr #$3,b", "move #>$1f9fed,b", "lsr #$3,b", "nop"}, {}, {}});
    checkSameRegisters(r);
    CHECK((r.jit->dsp.getSR().var & CCR_Z) == 0);
}
