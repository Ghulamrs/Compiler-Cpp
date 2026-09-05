// **A class-typed `?:`**, which needs three things that are not obvious from
// the syntax, and each was a fault of its own before it worked.
//
// **The answer needs storage.** A `Conditional` yields a value the backends
// move as a scalar, and a class has nowhere to be moved to - so both arms build
// into one slot the expression owns, the conditional becomes the `int` the arms
// answer with, and the whole wears the `*(build, &tmp)` shape a class temporary
// already wears.
//
// **A temporary an arm made belongs to that arm.** One arm runs; the other's
// temporaries were never built, and destroying them is destroying objects that
// do not exist. Each carries a guard set at the end of its own arm.
//
// **And a guard is only as good as its initialisation.** They are cleared at
// the function's entry, because a declaration flushes its temporaries through
// a path that runs after the statements it is destroying for and cannot put
// anything in front of them - so before that, the guard held whatever the frame
// did.
//
// Counted as live objects and whether constructions balance destructions, never
// as constructor calls: copy elision is permitted rather than required in C++11
// and the oracles differ, so a constructor count has no single right answer.
extern "C" int printf(const char *, ...);

int live = 0, ctors = 0, dtors = 0;

struct T { int n; T(int v); T(const T &o); ~T(); };
T::T(int v) : n(v) { live++; ctors++; }
T::T(const T &o) : n(o.n) { live++; ctors++; }
T::~T() { live--; dtors++; }

T make(int v) { return T(v); }
int take(T p) { return p.n; }

// A temporary in one arm and an lvalue in the other, both ways round.
T fromLocal(const T &p, bool b) { T r = b ? p : make(2); return r; }
T fromReturn(const T &p, bool b) { return b ? p : make(2); }
// Neither arm is a class: the temporaries are the arguments, and the arm that
// does not run must leave its own alone.
int scalarArms(bool b) { return b ? take(T(5)) : take(T(9)); }

void report(const char *what) {
    printf("%-9s live %d  balanced %d\n", what, live, ctors == dtors);
}

int main() {
    { T held(1); { T x = fromLocal(held, true); (void)x.n; } }   report("local T");
    { T held(1); { T x = fromLocal(held, false); (void)x.n; } }  report("local F");
    { T held(1); { T y = fromReturn(held, true); (void)y.n; } }  report("ret T");
    { T held(1); { T y = fromReturn(held, false); (void)y.n; } } report("ret F");
    printf("scalar %d %d\n", scalarArms(true), scalarArms(false));
    report("scalar");
    // A loop, where a guard left set from the turn before would be read again.
    for (int i = 0; i < 3; i++) { T z = (i % 2) ? make(1) : make(2); (void)z.n; }
    report("loop");
    return 0;
}
