#pragma once

// The passes over one function. Each says whether it changed anything, so the
// driver can run them until nothing does.

#include "OptFlow.h"

namespace opt {

// What nobody reads, a sign extension included when only its low half is read.
bool removeDead(Stream &s, Flow &f);

// Code no label leads to after a jump or return, and a jump to the next label.
bool removeUnreachable(Stream &s);

// **A copy out of a dying register** folds into the instruction that wrote it.
bool coalesceCopies(Stream &s, Flow &f, const Convention &c);

// The shortest encoding of what is read after it: no REX where no upper half is read.
bool shrink(Stream &s, Flow &f, const Convention &c);

// **A load read once** becomes that instruction's memory operand.
bool foldLoads(Stream &s, Flow &f, const Convention &c);

// A local the walker placed in the frame, rbp-relative.
struct Local {
    long long disp;
    int size;
};

// **Scalar locals whose address never escapes, kept in callee-saved
// registers** - the busiest first, by loop depth. Returns what the prologue saves.
std::vector<SavedReg> promoteLocals(Stream &s, const Convention &c, const std::vector<Local> &locals,
                                    int frameSize, int maxRegs, long minWeight);

// **Shadow space reserved once in the frame**, not per call; true if it grows.
bool reserveShadow(Stream &s, Flow &f, const Convention &c);

// **What each register holds, followed forward through a block** - see OptValues.cpp.
bool forwardValues(Stream &s, Flow &f, const Convention &c);

}
