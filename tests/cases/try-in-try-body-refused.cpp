// A `try` inside another one's *body*, refused - and the reason is precise
// enough to say what would lift it.
//
// The overlap that made this impossible is gone: `Walker::openRegion` splits
// the enclosing region around this one, so no two rows cover the same address
// and the rows sort into the order the table's gap arithmetic needs. That is
// what made a `try` inside a `catch` work, and it is not enough here.
//
// **What is missing is the action chain continuing into the enclosing region.**
// Measured from clang, for exactly this program: the record covering the inner
// body reads `catch double, continue to action 4`, and action 4 is `catch int` -
// the *outer's* handler. One record's chain carries every handler that encloses
// the address. cxx1 gives a row its own types and stops, so an `int` thrown in
// the inner body finds no `int` in the chain, phase 1 concludes this frame has
// no handler, and the whole frame is skipped.
//
// **And the pad has to hand over as well.** If the outer's type does match, the
// runtime lands at *this* region's pad, because that is the pad its record
// names. The inner pad tests the inner's selectors, fails, and calls
// `_Unwind_Resume` - which leaves for the caller rather than trying the region
// outside this one in the same frame. It would have to jump to the enclosing
// pad instead, which is the same shape the `try` body's cleanup segments
// already use to reach their chain.
//
// clang compiles this and prints `outer int 7 | end`: the inner `catch (double)`
// does not match, and the outer `catch (int)` does.

extern "C" int printf(const char *, ...);

void boom(int k) { if (k) throw 7; throw 2.5; }

int main(void) {
    try {
        try { boom(1); }
        catch (double d) { printf("inner double %.1f ", d); }
        printf("after inner ");
    }
    catch (int e) { printf("outer int %d ", e); }
    printf("| end\n");
    return 0;
}
