// A `try` inside another one's *body* - the last of the nested shapes, and it
// took three mechanisms rather than one.
//
// **The regions must not overlap.** `Walker::openRegion` splits the enclosing
// one around this one, closing at its first label and reopening at its last.
// That alone made a `try` inside a `catch` work; here it was necessary and not
// sufficient.
//
// **The action chain must continue outwards.** Measured from clang for exactly
// this program: the record covering the inner body reads `catch double,
// continue to action 4`, and action 4 is the outer's `catch int`. One record's
// chain names every handler enclosing the address, and phase 1 walks all of it
// before deciding the frame has none. cxx1 gave a row its own types and
// stopped, so the `int` was never found and the whole frame was skipped.
//
// **And the pad must hand over rather than resume.** With the chain fixed,
// phase 1 picks the outer's `int` and phase 2 lands *here*, at the inner
// region's pad, because that is the pad its record names. `_Unwind_Resume` from
// there leaves for the caller and never tries the region outside this one in
// the same frame - measured as an infinite loop, the resume finding this frame
// again and again. So the selector goes to the enclosing chain, which is the
// one place that tests it for those types.
//
// Two smaller things fell out of it. A nested `try` shares the enclosing
// region's `.ex.ptr` and `.ex.sel` slots, or the chain reads a slot nothing on
// that path wrote - measured as a crash. And the chain label is numbered from a
// counter rather than from the slot, which collided the moment the slots were
// shared.

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
