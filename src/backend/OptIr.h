#pragma once

// **The optimizer's view of one function**: the instructions the walker wrote,
// each operand parsed once into registers, displacements and immediates, and
// kept in one ordered stream with the labels and the events between them.

#include "Spelling.h"

#include <functional>
#include <string>
#include <vector>

namespace opt {

// The general registers in encoding order, then xmm0-15 as 16-31.
enum : int { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
constexpr int kGprs = 16;
constexpr int kXmm0 = 16;
constexpr int kRegs = 32;

using RegSet = unsigned;
constexpr RegSet bit(int r) { return 1u << static_cast<unsigned>(r); }
constexpr RegSet kAllRegs = ~0u;

// A register's number and the width named, in bytes; -1 for one this does not
// model (the x87 stack). The name for a number and width, for writing back.
struct Reg {
    int id = -1;
    int width = 0;
};
Reg parseReg(Str name);
const char *regName(int id, int width);

struct Operand {
    enum Kind { None, Register, Immediate, Memory, RipSymbol, Indirect, Label };
    Kind kind = None;
    Reg reg;                  // Register; Indirect's target; Memory's base
    long long disp = 0;       // Memory
    bool hasDisp = false;
    long long value = 0;      // Immediate, when numeric
    bool numeric = false;
    std::string text;         // an Immediate that is not numeric, a symbol, a label

    static Operand from(const Op &o);
    static Operand ofReg(int id, int width);
    static Operand ofImm(long long v);
    static Operand ofMem(int base, long long disp);
    Op op() const;            // valid while this operand is
    bool isReg(int id) const { return kind == Register && reg.id == id; }
    bool isMem() const { return kind == Memory; }
};

struct Instr {
    std::string m;
    Operand a, b;             // AT&T order: a is the source, b the destination
    int operands = 0;
};

// **Everything between functionBegin and functionEnd, in the order it came.**
// An event is a spelling call that is not an instruction, replayed where it
// stood - which is what keeps an alignment or a location beside its code.
struct Entry {
    enum Kind { Ins, Label, Event };
    Kind kind = Ins;
    Instr ins;
    std::string label;
    std::function<void(Spelling &)> event;
    bool dead = false;
};

using Stream = std::vector<Entry>;

}
