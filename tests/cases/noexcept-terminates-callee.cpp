// The other half of [except.spec]/9. `noexcept-terminates.cpp` covers a `throw`
// written inside the function, where the escape is certain at compile time and
// a branch to `abort` answers it. This one covers an exception arriving from a
// function the `noexcept` one *calls*, which cannot be seen from here and needs
// a landing pad over the whole body.
//
// The region is built out of the finished body rather than around the parse.
// An implicit `try` written around it would set `inTryBody_` before the body
// was read, and `for (S s; ...)` is refused under that flag on every target -
// so wrapping the parse would start refusing `noexcept` functions that compile
// today. Building it afterwards sets no flag while anything is being read.
//
// **The limit that buys, and it is a real one.** A body that already owns an
// unwind region - a destructible local, or a `try` - cannot be wrapped after
// the fact: its cleanup row resumes, the resume finds this frame again, and the
// destructor runs for ever. Those functions keep the behaviour they had, which
// is the exception propagating; `functionHasPads_` is the test.
//
// Nothing here is refused that was not refused before, and nothing that
// terminated before propagates now.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);

void thrower() { throw 2; }
void quiet() noexcept { printf("q "); }
int five() { return 5; }

// The case: no `throw` written here, and the exception arrives anyway.
void escapes() noexcept { thrower(); }

// A `noexcept` function that catches what its callee threw keeps its promise,
// so nothing terminates and this must run.
int catches() noexcept { try { thrower(); } catch (int e) { return e; } return 0; }

// Calls only what has promised: `mayThrow_` is zero, no region is built, and
// this function is emitted exactly as it was.
int leaf() noexcept { quiet(); return 7; }

// A destructible local and a call that may throw - the shape that is left
// alone. It must still compile, still run its destructor, and still return.
struct R { int n; R(int v) : n(v) { printf("+%d ", v); } ~R() { printf("-%d ", n); } };
int withLocal() noexcept { R r(1); return r.n + five(); }

// And an ordinary function is untouched: this one propagates as it always did.
int ordinary() { thrower(); return 0; }

int main() {
    printf("%d ", withLocal());
    printf("%d ", catches());
    printf("%d ", leaf());
    try { ordinary(); } catch (int e) { printf("outer%d ", e); }
    printf("before\n");
    fflush(0);
    escapes();
    printf("returned - and should not have\n");
    return 0;
}
