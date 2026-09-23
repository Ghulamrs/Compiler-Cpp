#pragma once

// **What an instruction reads, writes and touches**, the one fact every pass
// asks. An instruction this table does not know is opaque: it reads and
// writes everything, so no pass moves anything across it.

#include "OptIr.h"

struct Abi;

namespace opt {

// What a call reads and what it may leave changed, from the ABI's own table.
struct Convention {
    RegSet arguments = 0;
    RegSet clobbered = 0;
    RegSet returned = 0;
    RegSet preserved = 0;
    int shadow = 0;          // bytes a caller leaves above the return address
};
Convention conventionOf(const Abi &abi);

Effects effectsOf(const Instr &i, const Convention &c);

// Whether an instruction reads registers only through its operands.
bool explicitOnly(const Instr &i);

// The condition a jcc or setcc tests, and its opposite; "" for anything else.
std::string conditionOf(const std::string &m);
std::string inverse(const std::string &cc);

}
