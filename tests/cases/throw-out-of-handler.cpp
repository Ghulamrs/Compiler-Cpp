// [except.handle]/16 again, for the one way out of a handler that no jump can
// be written for: an exception. Leaving a handler ends the handling, and on
// Itanium `__cxa_end_catch` is what ends it - so a `throw` from inside a
// `catch` needs the call on the *unwinder's* path, which is a cleanup region
// over the handler's block whose pad makes it. clang emits exactly that, an
// invoke of `__cxa_throw` with a cleanup landing pad beside it.
//
// cxx1 had the call after the block and nothing on the way out, so a throw
// from a handler left the runtime's caught-exception chain set and the
// exception object undestroyed: the ledger read live=1 per call where clang
// read live=0, with identical output above the ledger line. It was
// `tests/open/catch-return-end-catch.cpp`'s other half, and the last thing
// handler-exit-end-catch.cpp left open.
//
// **Counted rather than traced**, for the reason lifetime.h gives: C++11
// permits eliding the copy of a thrown object, cxx1 does not elide it and
// clang does, so `built` and `gone` differ between them by a factor that says
// nothing. live, double, phantom, over and lost are what this pins, and each
// must be zero.
//
//   plain      a new exception out of one handler
//   rethrow    `throw;`, which keeps the object the handler holds
//   handedOver an enclosing `try` in the *same function* catches it - the pad
//              hands the selector to that chain rather than resuming, which
//              would leave the frame without trying it
//   noMatch    the enclosing `try` does not match, so it leaves the function
//   twoDeep    a handler inside a handler: two catches to end, one per pad,
//              the inner pad handing on to the outer one rather than jumping
//              past it to the chain outside both
//   outerLocal an object alive *outside* the `try` while the handler throws -
//              a `try` is covered by no enclosing cleanup region, so its
//              handler's pad owes those destructors too
//   byValue    caught by value, where the handler's own copy dies as well
//   any        `catch (...)` rethrowing into the enclosing handler
#include "lifetime.h"

struct A {
    int v;
    A(int n) : v(n) { lfBuilt(this, "A"); }
    ~A() { lfGone(this, "A"); }
};

struct E {
    int v;
    E(int n) : v(n) { lfBuilt(this, "E"); }
    E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
    ~E() { lfGone(this, "E"); }
};

int plain() { try { throw E(1); } catch (E &e) { throw E(2); } }

int rethrow() { try { throw E(3); } catch (E &e) { throw; } }

int handedOver() {
    int n = 0;
    try {
        try { throw E(1); }
        catch (E &e) { n += e.v; throw E(10); }
    } catch (E &e) { n += e.v; }
    return n;                                          // 11
}

int noMatch() {
    int n = 0;
    try {
        try { throw E(2); }
        catch (E &e) { n += e.v; throw 7; }            // an int; the outer takes E
    } catch (E &e) { n += 100; }
    return n;
}

int twoDeep() {
    int n = 0;
    try {
        try { throw E(3); }
        catch (E &a) {
            n += a.v;
            try { throw E(4); }
            catch (E &b) { n += b.v; throw E(20); }
        }
    } catch (E &e) { n += e.v; }
    return n;                                          // 27
}

int outerLocal() {
    A a(5);
    try { throw E(1); } catch (E &e) { throw E(a.v); }
}

int byValue() { try { throw E(1); } catch (E e) { throw E(2); } }

int any() {
    int n = 0;
    try {
        try { throw E(5); }
        catch (...) { n += 1; throw; }
    } catch (E &e) { n += e.v; }
    return n;                                          // 6
}

int main() {
    lfWatch();
    int a = 0, b = 0, c = 0, d = 0;
    try { a = plain(); }      catch (E &e) { a = e.v; }
    try { b = rethrow(); }    catch (E &e) { b = e.v; }
    try { c = noMatch(); }    catch (int k) { c = k; }
    try { d = outerLocal(); } catch (E &e) { d = e.v; }
    int f = 0;
    try { f = byValue(); }    catch (E &e) { f = e.v; }
    printf("%d %d %d %d %d %d %d %d\n", a, b, c, d, f,
           handedOver(), twoDeep(), any());
    return 0;
}
