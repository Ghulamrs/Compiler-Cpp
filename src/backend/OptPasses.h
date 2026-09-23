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
bool coalesceCopies(Stream &s, Flow &f);

// **What each register holds, followed forward through a block** - see OptValues.cpp.
bool forwardValues(Stream &s, Flow &f, const Convention &c);

}
