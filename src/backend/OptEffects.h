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
};
Convention conventionOf(const Abi &abi);

struct Effects {
    RegSet reads = 0;        // every register whose value is used
    RegSet writes = 0;       // registers given a wholly new value
    RegSet partial = 0;      // written in part, so also read
    bool flagsRead = false;
    bool flagsWritten = false;
    bool memoryRead = false;
    bool memoryWritten = false;
    bool control = false;    // a jump, a call or a return
    bool stack = false;      // moves rsp: push, pop, add or sub to it
    bool opaque = false;
};
Effects effectsOf(const Instr &i, const Convention &c);

// The condition a jcc or setcc tests, and its opposite; "" for anything else.
std::string conditionOf(const std::string &m);
std::string inverse(const std::string &cc);

}
