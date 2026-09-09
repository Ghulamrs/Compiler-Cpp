// [except.handle]/16: leaving a handler by `return`, `break`, `continue` or
// `goto` ends the handling, and on Itanium what ends it is `__cxa_end_catch` -
// the call that pops the runtime's caught-exception chain and destroys the
// exception object.
//
// cxx1 emitted that call *after* the handler's block, so only falling off the
// end reached it: every jump out branched straight to its target with the
// chain still set and the object never destroyed. `catch (E &e) { return e.v; }`
// printed the right answer and leaked one exception object per call, which is
// why this was found by the ledger and not by a diff of stdout. It was
// `tests/open/catch-return-end-catch.cpp` until 2026-09-09.
//
// **Counted rather than traced.** C++11 permits eliding the copy of a thrown
// object and cxx1 does not elide it where clang does, so `built` and `gone`
// differ between the two compilers by a factor that says nothing. What the
// case pins is the balance: live, double, phantom, over and lost, every one of
// which must be zero.
//
// Each function below is a different way out, and the count of handlers each
// one leaves is what decides how many calls it makes:
//
//   one      out of one handler
//   two      out of two, the inner throw caught inside the outer handler
//   broke    out of a handler, into a loop that was entered before it
//   went     the same by `continue`
//   inner    a loop *inside* the handler, whose break leaves no handler at all
//   stays    a `goto` to a label inside the handler, which leaves none either
//   copied   caught *by value*, where the order matters: the handler's own
//            copy is destroyed first and the exception object after it
//   fell     off the end, the one way that always worked
#include "lifetime.h"

// Out of line, which is the suite's habit rather than the feature: clang emits
// only the C2 form of a constructor defined inside its class on x86_64-linux,
// and names.sh would report that as a difference of its own.
struct E {
    int v;
    E(int n);
    E(const E &o);
    ~E();
};

E::E(int n) : v(n) { lfBuilt(this, "E"); }
E::E(const E &o) : v(o.v) { lfBuilt(this, "E"); }
E::~E() { lfGone(this, "E"); }

int one() { try { throw E(1); } catch (E &e) { return e.v; } return 0; }

int two() {
    try { throw E(2); }
    catch (E &a) {
        try { throw E(3); }
        catch (E &b) { return a.v + b.v; }
    }
    return 0;
}

int broke() {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        try { throw E(4); } catch (E &e) { n += e.v; break; }
    }
    return n;
}

int went() {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        try { throw E(5); } catch (E &e) { n += e.v; continue; }
    }
    return n;
}

int inner() {
    int n = 0;
    try { throw E(6); }
    catch (E &e) { for (int i = 0; i < 4; i++) { if (i == 2) break; n += e.v; } }
    return n;
}

int stays() {
    int n = 0;
    try { throw E(7); }
    catch (E &e) { again: n += e.v; if (n < 21) goto again; }
    return n;
}

// Caught by value: `e` is a copy of the exception object and a local of the
// handler, so leaving destroys the copy and then ends the catch, in that
// order. Both compilers report two objects and two destructions here - there
// is nothing left for either to elide.
int copied() { try { throw E(9); } catch (E e) { return e.v; } return 0; }

int fell() { int n = 0; try { throw E(8); } catch (E &e) { n = e.v; } return n; }

int main() {
    lfWatch();
    printf("%d %d %d %d %d %d %d %d\n", one(), two(), broke(), went(), inner(),
           stays(), copied(), fell());
    return 0;
}
