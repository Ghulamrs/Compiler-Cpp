// **[expr.cond]/4: a `?:` whose arms are glvalues of one type is a glvalue.**
// So `int &r = p ? a : b;` is an ordinary reference binding, `(p ? a : b) = 20`
// an ordinary assignment, and `&(p ? a : b)` an ordinary address - all of them
// refused here until now for want of a shape rather than a rule.
//
// **The shape is the addresses.** `*(c ? &a : &b)` puts two pointers where the
// arms were, which every backend already moves, and the dereference around them
// is an lvalue: assignable, addressable, bindable. Nothing was added to any
// code generator.
//
// **Asked before the class lowering**, which copies both arms into a slot of
// its own. For two lvalues of one type there is nothing to copy, and copying
// would take the binding away again - `p ? x : y` below would name a temporary
// rather than `x`, and writing through it would reach nothing.
//
// The types have to match exactly, cv-qualification included: a difference
// there makes them different types and [expr.cond]/5 sends those to the prvalue
// answer, which is what `conditional-class.cpp` covers.
extern "C" int printf(const char *, ...);

struct R { int v; };

int main() {
    int a = 1, b = 2;
    bool p = true;

    // Bound to a reference, and written through it.
    int &r = p ? a : b;
    r = 9;
    // Assigned to directly, which is the form the standard's own example uses.
    (p ? a : b) = 20;
    // And its address taken.
    int *q = &(p ? a : b);
    *q += 1;

    // A class works the same way, and must not be copied on the way.
    R x; x.v = 5;
    R y; y.v = 6;
    R &z = p ? x : y;
    z.v = 77;
    printf("%d %d %d %d\n", a, b, x.v, y.v);

    // The other arm, so that the choice is doing something.
    p = false;
    int &second = p ? a : b;
    second = 33;
    printf("%d %d\n", a, b);
    return 0;
}
