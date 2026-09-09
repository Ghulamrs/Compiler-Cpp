// A destructible object inside a `catch` handler, which was refused until
// 2026-09-09 and is the exclusion the end-catch region lifted.
//
// **The reason it was refused was structural**: a handler is emitted past the
// `try`'s range, so its block was inside no row of the call-site table - there
// was nothing to carry the `try`'s catch types on and no chain to hand the
// selector to. `throw-out-of-handler.cpp` gave the handler a region of its
// own, because an exception leaving one has to call `__cxa_end_catch`; a
// local's destructor goes in the same place, and the pad it hands over to is
// the one that ends the catch afterwards.
//
// So the order out of a handler is: this block's objects, then the exception
// object, then whatever is outside the `try` - and each of the six shapes
// below is a different way of leaving.
//
//   fell          off the end of the handler
//   leaves        a `throw` past the local
//   returns       a `return` past it
//   twoAndNested  two locals and a block between them
//   inLoop        a `continue` out of the handler, three times round
//   throughCall   a handler that throws while its local is alive, caught by
//                 an enclosing `try` in the same function
//
// Out-of-line constructors, which is the suite's habit rather than the
// feature: clang emits only the C2 form of one defined inside its class on
// x86_64-linux and names.sh would report that as a difference.
#include "lifetime.h"

struct A {
    int v;
    A(int n);
    ~A();
};

A::A(int n) : v(n) { lfBuilt(this, "A"); }
A::~A() { lfGone(this, "A"); }

struct E {
    int v;
    E(int n);
    E(const E &o);
    ~E();
};

E::E(int n) : v(n) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }

int fell() {
    int n = 0;
    try { throw E(1); } catch (E &e) { A a(2); n = e.v + a.v; }
    return n;                                          // 3
}

int leaves() { try { throw E(3); } catch (E &e) { A a(4); throw E(a.v); } }

int returns() {
    try { throw E(5); } catch (E &e) { A a(6); return e.v + a.v; }
    return 0;                                          // 11
}

int twoAndNested() {
    int n = 0;
    try { throw E(1); }
    catch (E &e) { A a(2); { A b(3); n += b.v; } A c(4); n += a.v + c.v; }
    return n;                                          // 9
}

int inLoop() {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        try { throw E(1); }
        catch (E &e) { A a(2); n += a.v; if (i == 1) continue; n += e.v; }
    }
    return n;                                          // 8
}

int throughCall() {
    int n = 0;
    try {
        try { throw E(7); }
        catch (E &e) { A a(1); throw E(8); }
    } catch (E &e) { n = e.v; }
    return n;                                          // 8
}

int main() {
    lfWatch();
    int b = 0;
    try { b = leaves(); } catch (E &e) { b = e.v; }
    printf("%d %d %d %d %d %d\n", fell(), b, returns(), twoAndNested(),
           inLoop(), throughCall());
    return 0;
}
