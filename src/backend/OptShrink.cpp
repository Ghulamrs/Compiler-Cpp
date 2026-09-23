#include "OptPasses.h"

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool reg64(const Operand &o) { return gpr(o) && o.reg.width == 8; }
bool is(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}

// **The same result in fewer bytes**, given what is read after it: a write of
// four bytes zeroes the upper half, so where nobody reads that half, or the
// value is a small non-negative constant, the REX prefix goes.
bool shorter(Instr &i, RegSet wide, bool flagsLive) {
    if (is(i.m, {"movzbq", "movzwq"}) && reg64(i.b)) {
        i.m[i.m.size() - 1] = 'l';
        i.b.reg.width = 4;
        return true;
    }
    if (gpr(i.b) && i.b.reg.width == 4 && is(i.m, {"mov", "movl"}) && i.a.kind == Operand::Immediate &&
        i.a.numeric && i.a.value == 0 && !flagsLive) {
        i = Instr{"xor", i.b, i.b, 2};
        return true;
    }
    if (!reg64(i.b)) return false;
    const bool upperRead = (wide & bit(i.b.reg.id)) != 0;
    if (is(i.m, {"mov", "movq"}) && i.a.kind == Operand::Immediate && i.a.numeric) {
        const long long v = i.a.value;
        if (v == 0 && !flagsLive) { i = Instr{"xor", Operand::ofReg(i.b.reg.id, 4), Operand::ofReg(i.b.reg.id, 4), 2}; return true; }
        if ((v >= 0 && v <= 0x7fffffffLL) || (!upperRead && v == static_cast<int>(v))) { i.m = "mov"; i.b.reg.width = 4; return true; }
        return false;
    }
    if (upperRead) return false;
    // Arithmetic whose low half depends on the low halves alone.
    const bool arith = is(i.m, {"add", "sub", "and", "or", "xor", "imul"}) && i.operands == 2 && !flagsLive;
    if (arith && ((i.a.kind == Operand::Immediate && i.a.numeric && i.a.value == static_cast<int>(i.a.value)) || reg64(i.a))) {
        if (reg64(i.a)) i.a.reg.width = 4;
        i.b.reg.width = 4;
        return true;
    }
    if (i.m == "movslq" && i.a.isMem()) { i.m = "movl"; i.b.reg.width = 4; return true; }
    if (i.m == "movslq" && gpr(i.a) && i.a.reg.id != i.b.reg.id) { i.m = "mov"; i.b.reg.width = 4; return true; }
    if (is(i.m, {"mov", "movq"}) && i.a.isMem()) { i.m = "movl"; i.b.reg.width = 4; return true; }
    return false;
}

// The bytes a load fetches, and whether what it leaves may stand for them at four bytes.
int loadWidth(const Instr &l) {
    if (!l.a.isMem() || !gpr(l.b)) return 0;
    if (l.m == "movl" || l.m == "movslq") return 4;
    if (is(l.m, {"mov", "movq"}) && l.b.reg.width == 8) return 8;
    return 0;
}

}

bool shrink(Stream &s, Flow &f, const Convention &c) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet wide = blk.wideOut;
        bool flags = blk.flagsOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &en = s[k];
            if (en.kind != Entry::Ins || en.dead) continue;
            if (shorter(en.ins, wide, flags)) { f.effects[k] = effectsOf(en.ins, c); changed = true; }
            const Effects &e = f.effects[k];
            wide = (wide & ~e.writes) | e.wide;
            flags = e.flagsRead || (flags && !e.flagsWritten);
        }
    }
    return changed;
}

bool foldLoads(Stream &s, Flow &f, const Convention &c) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &use = s[k];
            if (use.kind != Entry::Ins || use.dead) continue;
            Instr &u = use.ins;
            const bool arith = is(u.m, {"add", "sub", "and", "or", "xor", "cmp", "imul"}) && u.operands == 2;
            // The operand a load could stand in for: the source, or cmp's other side.
            Operand *slot = nullptr;
            if (arith && gpr(u.a) && gpr(u.b) && u.a.reg.id != u.b.reg.id) slot = &u.a;
            else if (u.m == "cmp" && gpr(u.b) && (gpr(u.a) || u.a.kind == Operand::Immediate) &&
                     !(gpr(u.a) && u.a.reg.id == u.b.reg.id)) slot = &u.b;
            const int r = slot ? slot->reg.id : -1;
            if (slot && !(live & bit(r))) {
                int l = k - 1;
                RegSet written = 0;
                bool clear = true;
                for (int n = 0; l >= blk.begin && n < 16; --l, ++n) {
                    if (s[l].kind != Entry::Ins || s[l].dead) continue;
                    const Effects &e = f.effects[l];
                    if (e.writes & bit(r)) break;
                    if ((e.reads & bit(r)) || e.memoryWritten || e.control || e.opaque || e.stack) { clear = false; break; }
                    written |= e.writes | e.partial;
                }
                const int w = loadWidth(l >= blk.begin ? s[l].ins : Instr());
                const bool fits = w != 0 && s[l].ins.b.reg.id == r && (w == slot->reg.width ||
                                  (w == 4 && slot->reg.width == 4));
                const int base = fits ? s[l].ins.a.reg.id : -1;
                if (clear && fits && s[l].kind == Entry::Ins && !s[l].dead &&
                    (base < 0 || !(written & bit(base)))) {
                    const int width = slot->reg.width;
                    *slot = s[l].ins.a;
                    if (slot == &u.b && u.a.kind == Operand::Immediate) u.m = width == 4 ? "cmpl" : "cmpq";
                    s[l].dead = true;
                    f.effects[k] = effectsOf(u, c);
                    changed = true;
                }
            }
            const Effects &e = f.effects[k];
            live = (live & ~e.writes) | e.reads;
        }
    }
    return changed;
}

}
