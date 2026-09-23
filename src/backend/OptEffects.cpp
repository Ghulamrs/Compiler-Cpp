#include "OptEffects.h"

#include "../Abi.h"

#include <cstring>

namespace opt {

namespace {

bool starts(const std::string &m, const char *p) { return m.compare(0, std::strlen(p), p) == 0; }
bool oneOf(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}

// The conditions jcc, setcc and cmovcc spell, each with its opposite.
struct Cond { const char *cc, *inv; };
const Cond kConds[] = {
    {"e", "ne"}, {"ne", "e"}, {"z", "nz"}, {"nz", "z"}, {"l", "ge"}, {"ge", "l"},
    {"le", "g"}, {"g", "le"}, {"b", "ae"}, {"ae", "b"}, {"be", "a"}, {"a", "be"},
    {"s", "ns"}, {"ns", "s"}, {"p", "np"}, {"np", "p"}, {"o", "no"}, {"no", "o"},
};

// What an operand reads when it is only a source: its register, or the
// register an address is formed from.
RegSet readsOf(const Operand &o) {
    switch (o.kind) {
    case Operand::Register:
    case Operand::Indirect:
    case Operand::Memory: return o.reg.id >= 0 ? bit(o.reg.id) : 0;
    default: return 0;
    }
}

bool inMemory(const Operand &o) { return o.kind == Operand::Memory || o.kind == Operand::RipSymbol; }

// **A write of four or eight bytes is whole**; a narrower one leaves the rest.
void write(Effects &e, const Operand &o) {
    if (inMemory(o)) { e.memoryWritten = true; e.reads |= readsOf(o); return; }
    if (o.kind != Operand::Register) return;
    if (o.reg.id < 0) { e.opaque = true; return; }
    if (o.reg.id < kGprs && o.reg.width < 4) e.partial |= bit(o.reg.id);
    else e.writes |= bit(o.reg.id);
}

void read(Effects &e, const Operand &o) {
    e.reads |= readsOf(o);
    if (inMemory(o)) e.memoryRead = true;
    if (o.kind == Operand::Register && o.reg.id < 0) e.opaque = true;
}

bool isMove(const std::string &m) {
    return oneOf(m, {"mov", "movq", "movl", "movw", "movb", "movabs", "movslq", "movsbq",
                     "movswq", "movsbl", "movswl", "movzbq", "movzbl", "movzwl", "movzwq",
                     "lea", "movd", "movaps", "movapd"}) ||
           starts(m, "cvt");
}

// The SSE moves into a register keep its upper lanes: a partial write.
bool mergesXmm(const std::string &m) { return m == "movsd" || m == "movss" || starts(m, "cvt"); }

bool isRmw(const std::string &m) {
    return oneOf(m, {"add", "sub", "and", "or", "xor", "adc", "sbb", "imul", "shl", "sar", "shr",
                     "addl", "subl", "orb", "andl", "orl", "xorl", "shll", "sarl", "shrl",
                     "addsd", "subsd", "mulsd", "divsd", "addss", "subss", "mulss", "divss",
                     "pxor", "xorps", "xorpd", "andpd", "andps"});
}

// **SSE arithmetic leaves the flags alone**, and that has to be exact: a flags
// write claimed where there is none would let a compare be taken for dead.
bool writesFlags(const std::string &m) {
    return !oneOf(m, {"addsd", "subsd", "mulsd", "divsd", "addss", "subss", "mulss", "divss",
                      "pxor", "xorps", "xorpd", "andpd", "andps"});
}

}

bool explicitOnly(const Instr &i) {
    const std::string &m = i.m;
    return starts(m, "mov") || starts(m, "set") ||
           oneOf(m, {"lea", "add", "sub", "and", "or", "xor", "cmp", "test", "addl", "subl", "cmpl", "imul"});
}

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// **A register named at four bytes or fewer is read only there**; anything
// implicit, or an address, is read whole.
RegSet wideOf(const Instr &i, const Effects &e) {
    if (!explicitOnly(i)) return e.reads;
    // A move's destination, and setcc's, is written, not read.
    const bool writesOnly = starts(i.m, "mov") || starts(i.m, "set") || i.m == "lea";
    RegSet narrow = 0, wide = 0;
    for (const Operand *o : {&i.a, &i.b}) {
        const bool destination = o == (i.operands == 1 ? &i.a : &i.b) && writesOnly;
        if (gpr(*o) && !destination) (o->reg.width <= 4 ? narrow : wide) |= bit(o->reg.id);
        else if (o->kind == Operand::Memory && o->reg.id >= 0) wide |= bit(o->reg.id);
    }
    return e.reads & ~(narrow & ~wide);
}

}

Convention conventionOf(const Abi &abi) {
    Convention c;
    for (int i = 0; i < abi.intCount; ++i) c.arguments |= bit(parseReg(abi.intRegs[i]).id);
    for (int i = 0; i < abi.sseCount; ++i) c.arguments |= bit(parseReg(abi.sseRegs[i]).id);
    if (abi.variadicSseCountInAl) c.arguments |= bit(RAX);
    for (int i = 0; i < abi.preservedCount; ++i) c.preserved |= bit(parseReg(abi.preservedRegs[i]).id);
    c.clobbered = kAllRegs & ~c.preserved;
    c.returned = bit(RAX) | bit(RDX) | bit(kXmm0) | bit(kXmm0 + 1);
    return c;
}

std::string conditionOf(const std::string &m) {
    std::string rest;
    if (m.size() > 1 && m[0] == 'j' && m != "jmp") rest = m.substr(1);
    else if (starts(m, "set")) rest = m.substr(3);
    else return std::string();
    for (const Cond &c : kConds) if (rest == c.cc) return rest;
    return std::string();
}

std::string inverse(const std::string &cc) {
    for (const Cond &c : kConds) if (cc == c.cc) return c.inv;
    return std::string();
}

Effects effectsOf(const Instr &i, const Convention &conv) {
    Effects e;
    const std::string &m = i.m;

    if (m == "call") {
        e.control = e.memoryRead = e.memoryWritten = true;
        e.reads = conv.arguments | bit(RSP) | readsOf(i.a);
        e.writes = conv.clobbered & ~bit(RSP);
        e.flagsWritten = true;
    } else if (m == "ret") {
        e.control = true;
        e.reads = conv.returned | conv.preserved | bit(RSP);
    } else if (m == "jmp") {
        e.control = true;
        if (i.a.kind != Operand::Label) { e.reads = readsOf(i.a); e.opaque = true; }
    } else if (!conditionOf(m).empty() && m[0] == 'j') {
        e.control = true;
        e.flagsRead = true;
    } else if (!conditionOf(m).empty()) {           // setcc
        e.flagsRead = true;
        write(e, i.a);
    } else if (m == "push" || m == "pushq") {
        read(e, i.a);
        e.reads |= bit(RSP);
        e.writes |= bit(RSP);
        e.memoryWritten = e.stack = true;
    } else if (m == "pop" || m == "popq") {
        e.reads |= bit(RSP);
        e.writes |= bit(RSP);
        e.memoryRead = e.stack = true;
        write(e, i.a);
    } else if (m == "leave") {
        e.reads = bit(RBP);
        e.writes = bit(RSP) | bit(RBP);
        e.memoryRead = e.stack = true;
    } else if (oneOf(m, {"cqo", "cqto", "cdq", "cltd"})) {
        e.reads = bit(RAX);
        e.writes = bit(RDX);
    } else if (oneOf(m, {"cltq", "cdqe"})) {
        e.reads = e.writes = bit(RAX);
    } else if (oneOf(m, {"idiv", "div", "idivl", "divl", "idivq", "divq"})) {
        read(e, i.a);
        e.reads |= bit(RAX) | bit(RDX);
        e.writes |= bit(RAX) | bit(RDX);
        e.flagsWritten = true;
    } else if (oneOf(m, {"cmp", "cmpl", "cmpq", "cmpb", "test", "testb", "testl", "testq",
                          "ucomisd", "ucomiss", "comisd", "comiss"})) {
        read(e, i.a);
        read(e, i.b);
        e.flagsWritten = true;
    } else if (oneOf(m, {"neg", "not", "inc", "dec", "negq", "notq"})) {
        read(e, i.a);
        write(e, i.a);
        if (i.a.kind == Operand::Register) e.reads |= readsOf(i.a);
        e.flagsWritten = m != "not" && m != "notq";
    } else if (isMove(m) || mergesXmm(m)) {
        if (m != "lea") read(e, i.a);
        else e.reads |= readsOf(i.a);
        write(e, i.b);
        if (mergesXmm(m) && i.b.kind == Operand::Register && i.a.kind == Operand::Register)
            { e.partial |= e.writes; e.writes = 0; }
    } else if (isRmw(m) && i.operands == 2) {     // one operand: imul's rdx:rax, a shift by one
        const bool zeroing = oneOf(m, {"xor", "xorl", "pxor", "xorps", "xorpd", "sub"}) &&
                             i.a.kind == Operand::Register && i.b.kind == Operand::Register &&
                             i.a.reg.id == i.b.reg.id && i.a.reg.id >= 0;
        if (!zeroing) { read(e, i.a); read(e, i.b); }
        write(e, i.b);
        if (!zeroing && i.b.kind == Operand::Register) e.reads |= readsOf(i.b);
        e.flagsWritten = writesFlags(m);
        if (i.b.isReg(RSP)) e.stack = true;
    } else if (m[0] == 'f') {                       // x87: memory and its own stack
        read(e, i.a);
        e.memoryRead = e.memoryWritten = true;
        e.flagsWritten = oneOf(m, {"fucomip", "fcomip", "fucomi", "fcomi"});
    } else {
        e.opaque = true;
    }

    if (e.opaque) {
        e.reads = e.writes = kAllRegs;
        e.flagsRead = e.flagsWritten = e.memoryRead = e.memoryWritten = true;
    }
    e.reads |= e.partial;
    e.wide = wideOf(i, e);
    return e;
}

}
