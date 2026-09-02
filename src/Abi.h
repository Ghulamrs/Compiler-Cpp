#pragma once

// **The table of measured facts about how this target passes and returns
// things.** Every value in it was read off clang or cl for that ABI, and this
// is where a reader comes to check one - so each field is named, not counted.
//
// It used to be initialised positionally, eleven values in a row in three
// files, which made the densest table of measurements in the tree unreadable at
// exactly the place someone would go to confirm one. Each backend fills it in
// by name now; the parser reads the three fields that decide a return.
struct Abi {
    // The registers arguments travel in, and how many there are of each.
    const char *const *intRegs = nullptr;
    int intCount = 0;
    const char *const *sseRegs = nullptr;
    int sseCount = 0;

    // **An argument takes a slot from both counters where this is set** - the
    // Microsoft rule that the third argument is the third register whichever
    // class it is, where SysV and AAPCS64 count the two kinds separately.
    bool positional = false;

    // Room the caller leaves above the return address for the callee to spill
    // its register arguments into: 32 on Microsoft, none on the other two. It
    // also moves where an incoming stack argument sits, at 16 + this.
    int shadowBytes = 0;

    // The largest class that comes back in registers rather than through a
    // hidden pointer. Read together with aggregatesByReference, which replaces
    // the question rather than refining it.
    int structReturnLimit = 0;

    // **Microsoft's rule, and only Microsoft's: a class returns in a register
    // only if its size is exactly 1, 2, 4 or 8** - not "up to 8" - and a member
    // function returns through the pointer whatever the size. Measured with clang.
    //
    // arm64 set this too and read it nowhere, so the *parser* asked Microsoft's
    // question about an AAPCS64 return: a 16-byte class was called indirect where
    // the backend returned it in x0/x1, and the callee stored an uninitialised x8
    // into a slot nobody passed. Whatever sets this must be the target that means
    // it, since the parser cannot tell one true flag from another.
    bool aggregatesByReference = false;

    // A variadic call puts the number of SSE registers it used in al. SysV
    // reads it; the other two have no such rule.
    bool variadicSseCountInAl = false;

    // The scratch register this target's code generator borrows, in its 64-bit
    // and 32-bit spellings.
    const char *scratch = nullptr;
    const char *scratch32 = nullptr;

    // **AAPCS64 returns one to four floats or doubles in the float registers**
    // whatever the size rule says - the homogeneous float aggregate.
    bool homogeneousFloatAggregates = false;

    // `.type` and `.size` beside a symbol: ELF has them, Mach-O and COFF
    // do not.
    bool elfSymbolAttributes = false;
};
