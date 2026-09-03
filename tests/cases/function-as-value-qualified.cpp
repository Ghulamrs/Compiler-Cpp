// A function named but not called, found through a namespace.
//
// **The call form and the value form are one lookup**, and they were not. A
// call, `endl(o)`, asked qualifyForLookup and so found `std::endl` from inside
// a using-directive; the same name passed rather than called - `cout << endl`,
// the whole idiom of a manipulator - asked for the bare name and found nothing.
// Three spellings of the same thing, each with a case below: written qualified
// `N::nl`, through `using namespace N`, and through `using N::nl`. The fourth,
// a global, always worked and is here to show the others answer the same way.

extern "C" int printf(const char *, ...);

struct O { int v; };
typedef O &(*Manip)(O &);

O &plain(O &o) { o.v += 1; return o; }

namespace N {
    O &nl(O &o) { o.v += 10; return o; }
    O &tab(O &o) { o.v += 100; return o; }
}

namespace M {
    O &deep(O &o) { o.v += 1000; return o; }
}

int apply(Manip m, O &o) { return m(o).v; }

using N::tab;                       // a using-declaration

int main(void) {
    O o;
    o.v = 0;
    int a = apply(plain, o);        // global: 1
    int b = apply(N::nl, o);        // qualified: 11
    int c = apply(tab, o);          // using-declaration: 111
    using namespace M;
    int d = apply(deep, o);         // using-directive, in a block: 1111
    Manip held = N::nl;             // and stored, not just passed
    int e = held(o).v;              // 1121
    printf("%d %d %d %d %d\n", a, b, c, d, e);
    return 0;
}
