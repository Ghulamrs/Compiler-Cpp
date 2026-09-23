#include "OptPasses.h"

#include <algorithm>
#include <climits>
#include <map>

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool is(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}
bool frameSlot(const Operand &o) { return o.kind == Operand::Memory && o.reg.id == RBP; }

// The instructions whose frame operand a register can take as it stands.
bool renamable(const std::string &m) {
    return is(m, {"mov", "movq", "movl", "movslq", "add", "sub", "and", "or", "xor", "cmp", "test",
                  "imul", "addl", "subl", "cmpl", "push", "pushq"});
}

// **How many bytes an instruction reads or writes at its frame operand**; 16
// where this cannot tell, which only ever makes a slot look more shared.
int accessWidth(const Instr &i, const Operand &at) {
    const Operand &other = &at == &i.a ? i.b : i.a;
    if (i.m == "movslq") return 4;
    if (is(i.m, {"push", "pushq"})) return 8;
    const char last = i.m.back();
    if (renamable(i.m) && gpr(other)) return other.reg.width;
    if (renamable(i.m) && i.m.size() > 3 && (last == 'l' || last == 'q')) return last == 'l' ? 4 : 8;
    return 16;
}

struct Candidate {
    Local local;
    bool ok = true;
    long weight = 0;
};

// **A loop is a jump back to a label above it**; each one it sits inside
// makes an access count eight times as much.
std::vector<int> loopDepths(const Stream &s) {
    std::vector<int> depth(s.size(), 0);
    std::map<std::string, int> at;
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind == Entry::Label) at[s[k].label] = k;
        if (s[k].kind != Entry::Ins || s[k].dead || s[k].ins.m[0] != 'j' || s[k].ins.a.kind != Operand::Label) continue;
        const auto t = at.find(s[k].ins.a.text);
        if (t != at.end())
            for (int n = t->second; n <= k; ++n) depth[n] = std::min(depth[n] + 1, 5);
    }
    return depth;
}

}

std::vector<SavedReg> promoteLocals(Stream &s, const Convention &c, const std::vector<Local> &locals,
                                    int frameSize, int maxRegs, long minWeight) {
    std::vector<Candidate> cands;
    for (const Local &l : locals)
        if (l.size == 4 || l.size == 8) cands.push_back(Candidate{l});
    RegSet mentioned = 0;
    const std::vector<int> depth = loopDepths(s);
    auto overlapping = [&](long long disp, int width, const std::function<void(Candidate &)> &f) {
        for (Candidate &cd : cands)
            if (disp < cd.local.disp + cd.local.size && cd.local.disp < disp + width) f(cd);
    };
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        const Instr &i = s[k].ins;
        if (i.m == "call" && i.a.text.find("setjmp") != std::string::npos) return {};
        for (const Operand *o : {&i.a, &i.b}) {
            if ((o->kind == Operand::Register || o->kind == Operand::Memory || o->kind == Operand::Indirect) && o->reg.id >= 0)
                mentioned |= bit(o->reg.id);
            // rbp read as a value, not restored nor handed to rsp, is the frame escaping.
            if (o->isReg(RBP) && i.m != "pop" && !(i.m == "mov" && i.b.isReg(RSP))) return {};
        }
        for (const Operand *o : {&i.a, &i.b}) {
            if (!frameSlot(*o)) continue;
            if (i.m == "lea") {
                if (!i.b.isReg(RSP)) overlapping(o->disp, 1, [](Candidate &cd) { cd.ok = false; });
                continue;
            }
            const int w = accessWidth(i, *o);
            const bool xmm = (i.a.kind == Operand::Register && i.a.reg.id >= kXmm0) ||
                             (i.b.kind == Operand::Register && i.b.reg.id >= kXmm0);
            overlapping(o->disp, w, [&](Candidate &cd) {
                if (cd.local.disp != o->disp || cd.local.size != w || !renamable(i.m) || xmm) cd.ok = false;
                else cd.weight += 1L << (3 * depth[k]);
            });
        }
    }

    std::vector<int> free;
    for (int r = 0; r < kGprs; ++r)
        if ((c.preserved & bit(r)) && r != RSP && r != RBP && !(mentioned & bit(r))) free.push_back(r);
    std::vector<Candidate *> chosen;
    for (Candidate &cd : cands) if (cd.ok && cd.weight >= minWeight) chosen.push_back(&cd);
    std::stable_sort(chosen.begin(), chosen.end(), [](const Candidate *x, const Candidate *y) { return x->weight > y->weight; });
    const std::size_t n = std::min<std::size_t>({chosen.size(), free.size(), static_cast<std::size_t>(maxRegs)});
    if (n == 0) return {};

    std::vector<SavedReg> saves;
    std::map<long long, Operand> home;
    for (std::size_t j = 0; j < n; ++j) {
        home[chosen[j]->local.disp] = Operand::ofReg(free[j], chosen[j]->local.size);
        saves.push_back(SavedReg{regName(free[j], 8), -(frameSize + 8 * static_cast<long long>(j + 1))});
    }
    for (Entry &en : s) {
        if (en.kind != Entry::Ins || en.dead) continue;
        for (Operand *o : {&en.ins.a, &en.ins.b}) {
            if (!frameSlot(*o)) continue;
            const auto h = home.find(o->disp);
            if (h != home.end()) *o = h->second;
        }
    }

    // **Each register goes back before rsp is taken from the frame** for
    // the return, ahead of the epilogue an unwinder recognises.
    Stream out;
    out.reserve(s.size() + 4 * n);
    for (std::size_t k = 0; k < s.size(); ++k) {
        const Instr &i = s[k].ins;
        const bool leaving = s[k].kind == Entry::Ins && !s[k].dead && gpr(i.b) && i.b.reg.id == RSP &&
                             (i.m == "lea" || i.m == "mov") && (i.a.isReg(RBP) || frameSlot(i.a));
        if (leaving)
            for (const SavedReg &sv : saves) {
                Entry r;
                r.ins = Instr{"mov", Operand::ofMem(RBP, sv.disp), Operand::ofReg(parseReg(sv.reg).id, 8), 2};
                out.push_back(r);
            }
        out.push_back(std::move(s[k]));
    }
    s.swap(out);
    return saves;
}

namespace {

// How far an instruction moves rsp down; 0 for one that does not.
long stackDelta(const Instr &i) {
    if (i.m == "push" || i.m == "pushq") return 8;
    if (i.m == "pop" || i.m == "popq") return -8;
    if ((i.m == "sub" || i.m == "add") && i.b.isReg(RSP) && i.a.kind == Operand::Immediate && i.a.numeric)
        return i.m == "sub" ? i.a.value : -i.a.value;
    return 0;
}

bool namesRsp(const Instr &i) {
    for (const Operand *o : {&i.a, &i.b})
        if ((o->kind == Operand::Register || o->kind == Operand::Memory) && o->reg.id == RSP) return true;
    return false;
}

constexpr long kUnknown = LONG_MIN;

}

bool reserveShadow(Stream &s, Flow &f, const Convention &c) {
    if (c.shadow == 0) return false;
    f.build(s, c);
    // **How far below the frame's floor rsp is on entry to each block**, the
    // same along every edge or this gives up.
    std::vector<long> in(f.blocks.size(), kUnknown);
    in[0] = 0;
    for (bool again = true; again;) {
        again = false;
        for (std::size_t b = 0; b < f.blocks.size(); ++b) {
            if (in[b] == kUnknown) continue;
            long depth = in[b];
            for (int k = f.blocks[b].begin; k < f.blocks[b].end && depth != kUnknown; ++k) {
                if (s[k].kind != Entry::Ins || s[k].dead) continue;
                const Instr &i = s[k].ins;
                const long d = stackDelta(i);
                if (d != 0) depth += d;
                else if (i.b.isReg(RSP) && (i.m == "lea" || i.m == "mov")) depth = kUnknown;  // the epilogue
                else if ((f.effects[k].writes & bit(RSP)) && i.m != "call") return false;
            }
            if (depth == kUnknown) continue;
            for (int n : f.blocks[b].next) {
                if (in[n] == kUnknown) { in[n] = depth; again = true; }
                else if (in[n] != depth) return false;
            }
        }
    }
    bool reserved = false;
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        long depth = in[b];
        for (int k = f.blocks[b].begin; k < f.blocks[b].end && depth != kUnknown; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            const Instr &i = s[k].ins;
            if (depth == 0 && i.m == "sub" && stackDelta(i) == c.shadow) {
                // Nothing may name rsp between the allocation and its release but the call.
                bool called = false;
                for (int j = k + 1; j < f.blocks[b].end; ++j) {
                    if (s[j].kind != Entry::Ins || s[j].dead) continue;
                    const Instr &x = s[j].ins;
                    if (x.m == "call" && !called) { called = true; continue; }
                    if (called && x.m == "add" && stackDelta(x) == -c.shadow) {
                        s[k].dead = s[j].dead = reserved = true;
                        break;
                    }
                    if (namesRsp(x) || stackDelta(x) != 0 || f.effects[j].control || f.effects[j].opaque) break;
                }
                if (s[k].dead) continue;
            }
            depth += stackDelta(i);
        }
    }
    return reserved;
}

void dropUnusedSaves(Stream &s, std::vector<SavedReg> &saves) {
    RegSet named = 0;
    for (const Entry &e : s)
        if (e.kind == Entry::Ins && !e.dead && !(e.ins.m == "mov" && e.ins.a.isMem() && e.ins.a.reg.id == RBP))
            for (const Operand *o : {&e.ins.a, &e.ins.b})
                if ((o->kind == Operand::Register || o->kind == Operand::Memory) && o->reg.id >= 0) named |= bit(o->reg.id);
    std::vector<SavedReg> kept;
    for (const SavedReg &sv : saves) {
        const int r = parseReg(sv.reg).id;
        if (named & bit(r)) { kept.push_back(sv); continue; }
        for (Entry &e : s)
            if (e.kind == Entry::Ins && !e.dead && e.ins.b.isReg(r) && e.ins.a.isMem() && e.ins.a.disp == sv.disp)
                e.dead = true;
    }
    // The slots close up, the kept saves renumbered from the top.
    for (std::size_t j = 0; j < kept.size(); ++j) {
        const long long want = saves[0].disp - 8 * static_cast<long long>(j);
        for (Entry &e : s)
            if (e.kind == Entry::Ins && !e.dead && e.ins.a.isMem() && e.ins.a.reg.id == RBP && e.ins.a.disp == kept[j].disp &&
                e.ins.b.isReg(parseReg(kept[j].reg).id))
                e.ins.a.disp = want;
        kept[j].disp = want;
    }
    saves.swap(kept);
}

bool removeDeadStores(Stream &s) {
    // An object runs upward from its address, so an address taken at L may
    // reach anything above it in the frame.
    long long escapesFrom = 0;
    struct Access { long long disp; int width; };
    std::vector<Access> reads;
    auto isStore = [](const Instr &i) {
        return frameSlot(i.b) && i.b.disp < 0 && i.operands == 2 &&
               is(i.m, {"mov", "movq", "movl", "movw", "movb"}) && (gpr(i.a) || i.a.kind == Operand::Immediate);
    };
    for (const Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead) continue;
        const Instr &i = e.ins;
        for (const Operand *o : {&i.a, &i.b}) {
            if (!frameSlot(*o)) continue;
            if (i.m == "lea") { if (!i.b.isReg(RSP)) escapesFrom = std::min(escapesFrom, o->disp); continue; }
            if (o == &i.b && isStore(i)) continue;
            reads.push_back(Access{o->disp, accessWidth(i, *o)});
        }
    }
    bool changed = false;
    for (Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead || !isStore(e.ins)) continue;
        const long long d = e.ins.b.disp;
        const int w = accessWidth(e.ins, e.ins.b);
        if (escapesFrom < 0 && d + w > escapesFrom) continue;
        bool read = false;
        for (const Access &r : reads) read = read || (r.disp < d + w && d < r.disp + r.width);
        if (!read) { e.dead = true; changed = true; }
    }
    return changed;
}

}
