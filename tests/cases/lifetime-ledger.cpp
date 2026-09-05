// The object ledger, wired into the suite.
//
// Every other case here is judged by what it prints, and printing cannot show
// a leak, a double destroy, or a destructor run on a slot nothing built - the
// three failures that leave the output identical. Compiler++ matched a clang
// build on all 200 comparisons under a compiler that leaks every data member
// of every class with a written destructor, and matched *because* a leak does
// not reach stdout. lifetime.h is the instrument; this case is the first user
// of it and the proof that it is hooked up.
//
// What each shape pins is the [basic.life] invariant - one construction and
// one destruction per object - across the ordinary ways an object comes and
// goes: a scope, a member reached through an implicit destructor, a by-value
// parameter, a copy, and a temporary bound to a const reference.
//
// Read the columns as lifetime.h says: live, double, phantom, over and lost
// must be zero for any conforming implementation, whatever it elided. built
// and gone are the weaker pair and are equal here only because no shape below
// leaves elision any freedom - there is no return by value in this case for
// exactly that reason.
//
// One shape is deliberately absent and is not an oversight: a class whose
// *written* destructor has a member or a base to destroy. cxx1 does not run
// those, the ledger reports live=1 where clang reports live=0, and the case
// for it belongs with the fix rather than here.
#include "lifetime.h"

// Every special member is defined **out of line**. Written inside the class
// they are inline, and on x86_64-linux clang then emits only the C2 variant
// of a constructor where cxx1 emits C1 and C2 - a difference about emission
// that the names suite reports wearing the shape of a mangling bug. CLAUDE.md
// records that trap twice already; out of line, both compilers emit both.
struct A {
    A();
    ~A();
};
A::A() { lfBuilt(this, "A"); }
A::~A() { lfGone(this, "A"); }

struct M {
    M();
    ~M();
};
M::M() { lfBuilt(this, "M"); }
M::~M() { lfGone(this, "M"); }

// No destructor written, so the implicit one runs the member's - the path
// that works. Its written twin is the shape held back above.
struct H { M m; };

struct V {
    int n;
    V(int k);
    V(const V &o);
    ~V();
};
V::V(int k) : n(k) { lfBuilt(this, "V"); }
V::V(const V &o) : n(o.n) { lfBuilt(this, "V"); }
V::~V() { lfGone(this, "V"); }

static int use(V v) { return v.n; }
static int peek(const V &v) { return v.n; }

int main() {
    lfWatch();
    { A a; }
    { A b; A c; }
    { H h; }
    { V a(3); printf("%d\n", use(a)); }
    { V a(4); V b(a); printf("%d\n", b.n); }
    printf("%d\n", peek(V(9)));
    return 0;
}
