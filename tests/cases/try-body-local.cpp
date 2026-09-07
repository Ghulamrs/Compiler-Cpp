// A destructible object *inside* a `try` body - [except.ctor]/2, an object
// with automatic storage destroyed as the exception leaves its scope, when
// that scope is the guarded block itself.
//
// **Why this could not simply be allowed.** A cleanup region is a call-site
// row, and one built inside a `try` body is a row *within* the `try`'s range.
// The personality routine's phase 1 takes the first row whose range holds the
// PC; if that row is cleanup-only it offers no handler, and the whole frame is
// skipped. Measured before the fix: this file's first case terminated on an
// uncaught `int` where clang printed `-a | c1`.
//
// So every segment of the body carries the `try`'s **catch types**, and its pad
// destroys what the body built and then jumps to the one chain that tests the
// selector, which lives in the `try`'s own pad. `alsoCleanup` puts the trailing
// filter-0 on those rows, or phase 2 installs no pad where nothing matched and
// the destructors never run.
//
// **The pad destroys from the `try`'s entry, not from its own block's.** A pad
// that jumps to the chain is the last one to run in this frame, so a nested
// block inside the body has to destroy what the body built above it too. The
// fifth case is that shape and it is where this was caught: `+o +i -i` where
// clang gives `+o +i -i -o`, one destructor short and printing nothing about
// it beyond the trace - which is why the ledger is here.
#include "lifetime.h"

// **Defined out of line on purpose.** clang emits an inline-defined
// constructor as a comdat linkonce_odr and cxx1 as an ordinary definition, and
// names.sh then reports an emission difference wearing the shape of a mangling
// one. The house habit, and it costs two lines.
struct A {
    const char *n;
    A(const char *x);
    ~A();
};

A::A(const char *x) : n(x) { printf("+%s ", n); lfBuilt(this, "A"); }
A::~A() { lfGone(this, "A"); printf("-%s ", n); }

void boom() { throw 7; }
void quiet() {}

int main() {
    lfWatch();

    // One local in the body: destroyed BEFORE the handler runs.
    try { A a("a"); boom(); } catch (int) { printf("| c1 "); }
    printf("\n");

    // Two: reverse order, both before the handler.
    try { A a("a2"); A b("b2"); boom(); } catch (int) { printf("| c2 "); }
    printf("\n");

    // No exception at all - the ordinary path still destroys it exactly once,
    // which `gone` and `live` are what check.
    try { A a("a3"); quiet(); } catch (int) { printf("| never "); }
    printf("\n");

    // One outside and one inside. The inner goes before the handler; the outer
    // belongs to the block holding the `try` and goes after it.
    { A out("out"); try { A in("in"); boom(); } catch (int) { printf("| c4 "); } }
    printf("\n");

    // A nested block inside the body: both, innermost first.
    try { A o("o"); { A i("i"); boom(); } } catch (int) { printf("| c5 "); }
    printf("\n");
    return 0;
}
