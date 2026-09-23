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
        RegSet live = blk.liveOut, wide = blk.wideOut;
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
            wide = (wide & ~e.writes) | e.wide;
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

namespace {

// **A value taken out of X, worked on in t and put back** is worked on in X:
// `mov %X,%t ... mov %t,%X` with X untouched between and t dead after.
bool workInPlace(Stream &s, Flow &f, const Convention &conv, int k, int begin, RegSet live, RegSet wide) {
    const Instr &c = s[k].ins;
    if (!(c.m == "mov" || c.m == "movl" || c.m == "movq") || !gpr(c.a) || !gpr(c.b)) return false;
    const int t = c.a.reg.id, x = c.b.reg.id, w = c.b.reg.width;
    if (t == x || frameReg(t) || frameReg(x) || c.a.reg.width != w || w < 4 || (live & bit(t)) || (w == 4 && (wide & bit(x)))) return false;
    for (int p = k - 1, n = 0; p >= begin && n < 32; --p, ++n) {
        if (s[p].kind != Entry::Ins || s[p].dead) continue;
        const Instr &i = s[p].ins;
        const Effects &e = f.effects[p];
        const bool copyIn = gpr(i.a) && i.a.reg.id == x && gpr(i.b) && i.b.reg.id == t && i.b.reg.width >= 4 &&
                            (i.m == "mov" || i.m == "movl" || i.m == "movq" || i.m == "movslq");
        if (copyIn) {
            for (int q = p; q < k; ++q) {
                if (s[q].kind != Entry::Ins || s[q].dead) continue;
                for (Operand *o : {&s[q].ins.a, &s[q].ins.b})
                    if ((o->kind == Operand::Register || o->kind == Operand::Memory) && o->reg.id == t) o->reg.id = x;
                f.effects[q] = effectsOf(s[q].ins, conv);
            }
            s[k].dead = true;
            if (s[p].ins.m != "movslq" && s[p].ins.a.reg.width == s[p].ins.b.reg.width) s[p].dead = true;
            return true;
        }
        const RegSet touched = e.reads | e.writes | e.partial;
        if ((touched & bit(x)) || e.control || e.opaque || e.stack) return false;
        if ((touched & bit(t)) && !explicitOnly(i)) return false;
    }
    return false;
}

}

bool coalesceCopies(Stream &s, Flow &f, const Convention &conv) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut, wide = blk.wideOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &copy = s[k];
            if (copy.kind != Entry::Ins || copy.dead) continue;
            const Instr &c = copy.ins;
            const bool isCopy = (c.m == "mov" || c.m == "movq") && gpr(c.a) && gpr(c.b) &&
                                !frameReg(c.a.reg.id) && !frameReg(c.b.reg.id) &&
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
            if (workInPlace(s, f, conv, k, blk.begin, live, wide)) { changed = true; break; }
            const Effects &e = f.effects[k];
            live = (live & ~e.writes) | e.reads;
            wide = (wide & ~e.writes) | e.wide;
        }
    }
    return changed;
}

}
