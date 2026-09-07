// A destructible object inside a `catch` handler, refused - the half of
// [except.ctor] this compiler does not do yet, and the reason is structural
// rather than a missing case.
//
// **A handler is emitted past the `try`'s range.** The body's cleanup regions
// became rows *inside* that range, so each can carry the `try`'s catch types
// and hand the selector to the one chain in the `try`'s pad - which is what
// `try-body-local.cpp` measures. A handler's block is not inside any row: it
// runs after the exception has already been chosen, so there is no row to
// carry types on and no chain to hand over to. It wants a region of its own,
// with its own pad, and that is its own step.
//
// A local beside the `try` in the same block works, and one inside the body
// works. clang compiles all three; this one is cxx1's gap and says so.
struct A { int v; A(int x); ~A(); };
A::A(int x) : v(x) {}
A::~A() {}

void boom();

// **No handler here returns**, and that is not incidental: `return` inside a
// `catch` is refused for x86_64-windows, and a case whose handler returns meets
// that refusal first on that target and never reaches this one. Measured - it
// is what the Windows leg reported the first time this case was written. Same
// rule catch-by-reference.cpp follows.
int seen = 0;

int main() {
    try { boom(); }
    catch (int) { A a(1); seen = a.v; }
    return seen - 1;
}
