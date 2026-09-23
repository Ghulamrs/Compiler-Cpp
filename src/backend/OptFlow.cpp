#include "OptFlow.h"

#include <map>

namespace opt {

namespace {

bool endsBlock(const Instr &i) { return i.m[0] == 'j' || i.m == "ret"; }

}

void Flow::build(const Stream &s, const Convention &c) {
    blocks.clear();
    effects.assign(s.size(), Effects());
    std::map<std::string, int> at;

    Block cur;
    cur.begin = 0;
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        const Entry &e = s[k];
        if (e.kind == Entry::Label && k > cur.begin) {
            cur.end = k;
            blocks.push_back(cur);
            cur = Block();
            cur.begin = k;
        }
        if (e.kind == Entry::Label) at[e.label] = static_cast<int>(blocks.size());
        if (e.kind != Entry::Ins) continue;
        effects[k] = effectsOf(e.ins, c);
        if (!e.dead && endsBlock(e.ins)) {
            cur.end = k + 1;
            blocks.push_back(cur);
            cur = Block();
            cur.begin = k + 1;
        }
    }
    cur.end = static_cast<int>(s.size());
    if (cur.end > cur.begin || blocks.empty()) blocks.push_back(cur);

    // Edges: the jump's target, and the next block unless the last live
    // instruction was an unconditional jump or a return.
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        Block &blk = blocks[b];
        const Instr *last = nullptr;
        for (int k = blk.end - 1; k >= blk.begin; --k)
            if (s[k].kind == Entry::Ins && !s[k].dead) { last = &s[k].ins; break; }
        bool falls = true;
        if (last && endsBlock(*last)) {
            if (last->m == "ret") falls = false;
            else {
                if (last->a.kind == Operand::Label) {
                    auto t = at.find(last->a.text);
                    if (t != at.end()) blk.next.push_back(t->second);
                    else blk.leaves = true;
                } else blk.leaves = true;
                if (last->m == "jmp") falls = false;
            }
        }
        if (falls) {
            if (b + 1 < blocks.size()) blk.next.push_back(static_cast<int>(b + 1));
            else blk.leaves = true;
        }
    }
}

void Flow::solve(const Stream &s) {
    for (Block &b : blocks) { b.liveIn = b.wideIn = 0; b.flagsIn = false; }
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = static_cast<int>(blocks.size()) - 1; b >= 0; --b) {
            Block &blk = blocks[b];
            RegSet live = blk.leaves ? kAllRegs : 0, wide = live;
            bool flags = blk.leaves;
            for (int n : blk.next) {
                live |= blocks[n].liveIn;
                wide |= blocks[n].wideIn;
                flags = flags || blocks[n].flagsIn;
            }
            blk.liveOut = live;
            blk.wideOut = wide;
            blk.flagsOut = flags;
            for (int k = blk.end - 1; k >= blk.begin; --k) {
                if (s[k].kind != Entry::Ins || s[k].dead) continue;
                const Effects &e = effects[k];
                live = (live & ~e.writes) | e.reads;
                wide = (wide & ~e.writes) | e.wide;
                flags = e.flagsRead || (flags && !e.flagsWritten);
            }
            if (live != blk.liveIn || wide != blk.wideIn || flags != blk.flagsIn) {
                blk.liveIn = live;
                blk.wideIn = wide;
                blk.flagsIn = flags;
                changed = true;
            }
        }
    }
}

}
