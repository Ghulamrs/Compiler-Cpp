// **`: k()` in an initialiser list value-initialises the member.**
// [class.base.init] hands the empty parens to [dcl.init], so a scalar member
// zeroes and a class member with no user-provided constructor zeroes leaf by
// leaf - the same rule new-value-init.cpp and class-temporary.cpp pin for the
// other two places `()` can say it. This was refused outright before -
// "'p' takes one value here, given 0" - for a form ordinary C++ writes daily.
// A member whose class has a constructor still is refused: the assignment
// shape an initialiser-list entry lowers to cannot run a constructor on the
// member itself, and pretending with a temporary's bytes would not be
// running it.
extern "C" int printf(const char *, ...);

struct P { int v; int w; };
struct W {
    P p;
    int k;
    double d;
    W() : p(), k(), d() {}
};

int main() {
    W w;
    printf("%d %d %d %d\n", w.p.v, w.p.w, w.k, (int)w.d);
    return 0;
}
