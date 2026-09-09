// [class.base.init]/8: every element of an array member is initialised, and
// `E g[2][3]` is **six** elements of `E` - not two of `E[3]`.
//
// The three walks that build, copy and destroy an array member each took
// `length()` and one `pointee()`, so a two-dimensional member had the outer
// length for a count and the *row* for an element: the constructor ran twice,
// over the first two objects of the first row, and four were left holding the
// frame. Copying such a class copied two rows' worth of bytes as two elements,
// and the destructor destroyed two. All three read the same helper now.
//
// **A local array of the same type was right the whole time** -
// `constructLocalArray` multiplies every dimension - which is what made this
// look like a class problem rather than an array one. It was
// `tests/open/two-d-array-member.cpp`, the only *miscompile* in that register
// rather than a refusal or an over-acceptance.
//
// Counted with the ledger as well as printed: `built` is the number this case
// is about, and both compilers report the same 42 objects here, so the count
// is a fact rather than a balance. Nothing in this program leaves elision any
// freedom - there is no return by value in it, for that reason.
#include "lifetime.h"

struct E {
    int v;
    E();
    E(const E &o);
    ~E();
};

E::E() : v(1) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }

struct Two { E g[2][3]; };                 // six
struct Three { E g[2][2][2]; };            // eight
struct Mixed { int n; E one; E row[3]; E grid[2][2]; };
struct Scalars { int g[2][3]; };           // no constructor to run at all

int sum(const E (&g)[2][3]) {
    int t = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++) t += g[i][j].v;
    return t;
}

int main() {
    lfWatch();
    int a = 0, b = 0, c = 0;
    {
        Two t;
        t.g[1][2].v = 7;
        Two copy(t);                       // the copy constructor's walk
        Two assigned;
        assigned = t;                      // the assignment's walk
        a = sum(t.g) + copy.g[1][2].v + assigned.g[1][2].v;
    }
    {
        Three th;
        th.g[1][1][1].v = 5;
        b = th.g[1][1][1].v + th.g[0][0][0].v;
    }
    {
        Mixed m;
        m.n = 2;
        Mixed cm(m);
        c = m.n + m.grid[1][1].v + cm.row[2].v;
    }
    Scalars s;
    s.g[1][2] = 4;
    printf("%d %d %d %d\n", a, b, c, s.g[1][2]);
    return 0;
}
