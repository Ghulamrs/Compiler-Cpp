// A static local with a constructor - [stmt.dcl]/4. It is built the first
// time control passes through its declaration, never again on a later pass,
// and destroyed at exit after main's own locals, in the reverse of the order
// the statics were built. On the Itanium targets that is __cxa_guard_acquire
// and __cxa_guard_release around the constructor and __cxa_atexit for the
// destructor; on x86_64-windows _Init_thread_header and _Init_thread_footer
// around it and atexit with a helper - the same calls clang and cl emit.
//
// The initialiser is evaluated on the first pass only, so `count(3)` keeps
// the 3 of its first call, and a static local in a loop body is one object.
#include "lifetime.h"

struct S {
    int v;
    S(int a);
    ~S();
};
S::S(int a) : v(a) { lfBuilt(this, "S"); printf("S%d\n", v); }
S::~S() { lfGone(this, "S"); printf("~S%d\n", v); }

int count(int n) {
    static S s(n);
    s.v++;
    return s.v;
}

int inLoop() {
    int total = 0;
    for (int i = 0; i < 3; i++) {
        static S loop(100);
        loop.v += i;
        total += loop.v;
    }
    return total;
}

int lazy(bool build) {
    if (build) {
        static S late(50);
        return late.v;
    }
    return -1;
}

int main() {
    lfWatch();
    S local(1);
    printf("%d %d %d\n", count(3), count(30), count(300));
    printf("%d\n", inLoop());
    printf("%d %d %d\n", lazy(false), lazy(true), lazy(true));
    return 0;
}
