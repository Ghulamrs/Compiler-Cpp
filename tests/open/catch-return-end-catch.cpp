// [except.handle]/16: leaving a handler by return, break, continue or goto
// ends the handling - __cxa_end_catch, which is what destroys the exception
// object. cxx1 branches straight to the return, so the object is never
// destroyed and the runtime's caught-exception chain is left set.
//
// Counted rather than traced: cxx1 copy-constructs the exception object where
// clang elides, which C++11 permits, so the constructor counts differ
// legitimately and only the balance says anything. live must be 0.
#include "../cases/lifetime.h"
struct E {
    int v;
    E(int n) : v(n) { lfBuilt(this, "E"); }
    E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
    ~E() { lfGone(this, "E"); }
};
int f() { try { throw E(1); } catch (E &e) { return e.v; } return 0; }
int main() { lfWatch(); int r = f(); printf("r=%d\n", r); return 0; }
