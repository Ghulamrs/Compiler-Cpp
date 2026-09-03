// **A by-value parameter and a reference to the same type are the same match**,
// and [over.ics.rank] gives neither a way to beat the other: one copies the
// argument, the other binds it, and both are the identity conversion. cxx1 used
// to hand this to `take(int)` without a word, because its reference binding was
// charged a qualification conversion for the const it adds - which is the rule
// that separates `int &` from `const int &`, and is not a difference at all
// against a parameter taken by value.
//
// clang, g++ and cl all refuse both calls below, the lvalue and the prvalue
// alike. reference-constness.cpp is the neighbour that must keep resolving.
extern "C" { int printf(const char *, ...); }

int take(int a)          { return 1 + 0 * a; }
int take(const int &a)   { return 2 + 0 * a; }

int main(void) {
    int v;
    v = 3;
    printf("%d %d\n", take(v), take(4));
    return 0;
}
