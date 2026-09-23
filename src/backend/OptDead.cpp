#include "OptPasses.h"

namespace opt {

namespace {

// rsp and rbp are the frame; nothing that writes them is ever taken for dead.
constexpr RegSet kFrame = bit(RSP) | bit(RBP);

bool removable(const Effects &e) {
    return !e.control && !e.memoryWritten && !e.stack && !e.opaque &&
           ((e.writes | e.partial) & kFrame) == 0;
}

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// Instructions that read registers only through their operands.
bool explicitOnly(const Instr &i) {
    const std::string &m = i.m;
    return m.compare(0, 3, "mov") == 0 || m == "lea" || m == "add" || m == "sub" || m == "and" ||
           m == "or" || m == "xor" || m == "cmp" || m == "test" || m == "addl" || m == "subl" ||
           m == "cmpl" || m == "imul" || m.compare(0, 3, "set") == 0;
}

// **The registers whose upper half this instruction reads.** A register named
// at four bytes or fewer is read only there; anything implicit is read whole.
RegSet wideReads(const Instr &i, const Effects &e) {
    if (!explicitOnly(i)) return e.reads;
    RegSet narrow = 0, wide = 0;
    for (const Operand *o : {&i.a, &i.b}) {
        if (gpr(*o)) (o->reg.width <= 4 ? narrow : wide) |= bit(o->reg.id);
        else if (o->kind == Operand::Memory && o->reg.id >= 0) wide |= bit(o->reg.id);
    }
    return e.reads & ~(narrow & ~wide);
}

// `movslq %eax, %rax` and `mov %eax, %eax`: the low half stays as it was.
bool extendsInPlace(const Instr &i) {
    if (!gpr(i.a) || !gpr(i.b) || i.a.reg.id != i.b.reg.id || i.a.reg.width != 4) return false;
    return (i.m == "movslq" && i.b.reg.width == 8) || (i.m == "mov" && i.b.reg.width == 4);
}

bool unconditional(const Instr &i) { return i.m == "jmp" || i.m == "ret"; }

}

bool removeDead(Stream &s, Flow &f) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut, wide = blk.liveOut;
        bool flags = blk.flagsOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &en = s[k];
            if (en.kind != Entry::Ins || en.dead) continue;
            const Effects &e = f.effects[k];
            const bool used = ((e.writes | e.partial) & live) != 0 || (e.flagsWritten && flags);
            const bool idle = extendsInPlace(en.ins) && (wide & bit(en.ins.b.reg.id)) == 0;
            if ((removable(e) && !used) || idle) {
                en.dead = true;
                changed = true;
                continue;
            }
            live = (live & ~e.writes) | e.reads;
            wide = (wide & ~e.writes) | wideReads(en.ins, e);
            flags = e.flagsRead || (flags && !e.flagsWritten);
        }
    }
    return changed;
}

bool removeUnreachable(Stream &s) {
    bool changed = false, gone = false;
    for (std::size_t k = 0; k < s.size(); ++k) {
        Entry &en = s[k];
        if (en.kind == Entry::Label) gone = false;
        if (en.kind != Entry::Ins || en.dead) continue;
        if (gone) { en.dead = changed = true; continue; }
        if (!unconditional(en.ins)) continue;
        gone = true;
        if (en.ins.m != "jmp" || en.ins.a.kind != Operand::Label) continue;
        for (std::size_t j = k + 1; j < s.size(); ++j) {
            if (s[j].kind == Entry::Label && s[j].label == en.ins.a.text) { en.dead = changed = true; break; }
            if (s[j].kind == Entry::Ins && !s[j].dead) break;
        }
    }
    return changed;
}

bool coalesceCopies(Stream &s, Flow &f) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &copy = s[k];
            if (copy.kind != Entry::Ins || copy.dead) continue;
            const Instr &c = copy.ins;
            const bool isCopy = (c.m == "mov" || c.m == "movq") && gpr(c.a) && gpr(c.b) &&
                                c.a.reg.width == 8 && c.b.reg.width == 8 && c.a.reg.id != c.b.reg.id;
            int p = k - 1;
            while (p >= blk.begin && (s[p].kind == Entry::Event || s[p].dead)) --p;
            if (isCopy && p >= blk.begin && s[p].kind == Entry::Ins && !(live & bit(c.a.reg.id))) {
                Instr &w = s[p].ins;
                const Effects &we = f.effects[p];
                const RegSet r = bit(c.a.reg.id);
                const bool pure = gpr(w.b) && w.b.reg.id == c.a.reg.id && w.b.reg.width >= 4 &&
                                  (we.writes & r) && !(we.reads & r) && !(we.partial & r) &&
                                  explicitOnly(w) && w.operands == 2 && !we.flagsWritten;
                if (pure) {
                    w.b.reg.id = c.b.reg.id;
                    f.effects[p].writes = (we.writes & ~r) | bit(c.b.reg.id);
                    copy.dead = changed = true;
                    continue;
                }
            }
            const Effects &e = f.effects[k];
            live = (live & ~e.writes) | e.reads;
        }
    }
    return changed;
}

}
