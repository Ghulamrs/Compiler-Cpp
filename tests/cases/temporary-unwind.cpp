// **A temporary destroyed when an exception passes through**, which it was not:
// cleanup regions were built from `alive_`, the list of objects with names, and
// a temporary lives inside one full expression and is never in it. So an
// argument copy or a class temporary was destroyed on the normal path and
// leaked on the unwind.
//
// **The naive fix is a regression, and that is the whole design.** A pad
// covering the statement would destroy temporaries the statement had not built
// yet: in `two(A(1), A(99))`, where the second constructor throws, it would run
// `~A` on storage nothing constructed. The region cannot start after each
// construction either - a `Try` wraps statements and that boundary is inside
// one.
//
// So each temporary carries a **guard flag** in the frame: cleared in front of
// the full expression, set the moment its constructor returns, cleared again as
// it is destroyed - on the normal path and in the pad alike. The pad destroys
// each temporary under its flag, so one region can cover a whole statement and
// still destroy exactly what exists. It is MSVC's unwind state variable written
// out, and it needs nothing the AST did not already have.
//
// The flag is cleared **in front of the expression** rather than once, because
// a statement in a loop runs again and a flag left set from the turn before
// would have the pad destroy an object this turn never built - which `loop`
// below is here to catch.
//
// Counted as live objects and whether constructions balance destructions, never
// as constructor calls: elision is permitted rather than required in C++11.
extern "C" int printf(const char *, ...);

int live = 0, ctors = 0, dtors = 0;

struct A { int v; A(int n); A(const A &o); ~A(); };
A::A(int n) : v(n) { if (n == 99) throw 5; live++; ctors++; }
A::A(const A &o) : v(o.v) { live++; ctors++; }
A::~A() { live--; dtors++; }

int two(A p, A q) { if (p.v == 7) throw 6; return p.v + q.v; }
int one(A p) { return p.v; }

A make(int n) { return A(n); }
int fromCall() { return two(make(7), make(2)); }

// **Not here, and it is a target difference rather than a gap in the design:**
// `two(A(1), A(99))`, where the second argument's constructor throws after the
// first was copied into its parameter. On Itanium the caller owns that copy and
// its pad destroys it; on Microsoft the callee owns it and the callee is never
// entered, so nobody does. Telling "entered" from "not entered" is a state
// change at the call instruction, which statement-granular regions cannot
// express. docs/CONFORMANCE.md records it.
// A throw once both are built - the ordinary case, and the one that leaked.
int bothBuilt() { return two(A(7), A(2)); }
// A temporary in a loop: the guard has to start clear on every turn.
int inLoop() {
    int t = 0;
    for (int i = 0; i < 3; i++) t += one(A(i));
    return t + one(A(99));
}
// A named local beside a temporary, with the throw inside the callee - so the
// copy is the callee's on either ABI and the local is the block's.
int withLocal() { A held(3); return two(A(7), held) + held.v; }

void run(const char *what, int (*f)()) {
    try { f(); }
    catch (int e) {
        printf("%-8s caught %d  live %d  balanced %d\n",
               what, e, live, ctors == dtors);
    }
}

int main() {
    run("both", bothBuilt);
    run("loop", inLoop);
    run("local", withLocal);
    run("call", fromCall);
    return 0;
}
