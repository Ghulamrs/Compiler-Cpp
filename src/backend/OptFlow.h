#pragma once

// **The function as blocks, and what is live across each.** A block starts at
// a label and ends after a jump or a return; liveness is solved over the
// edges to a fixpoint, so a value is dead only if no path reads it.

#include "OptEffects.h"

#include <vector>

namespace opt {

struct Block {
    int begin = 0, end = 0;              // entries [begin, end)
    std::vector<int> next;
    bool leaves = false;                 // falls or jumps somewhere not seen here
    RegSet liveIn = 0, liveOut = 0;
    bool flagsIn = false, flagsOut = false;
};

struct Flow {
    std::vector<Block> blocks;
    std::vector<Effects> effects;        // one per entry; empty for labels and events

    void build(const Stream &s, const Convention &c);
    void solve(const Stream &s);
};

}
