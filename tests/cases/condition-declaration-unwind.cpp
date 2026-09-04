// **An object declared in an `if` condition is destroyed when an exception
// passes through**, which the normal-path destructors do not cover: they run
// where the statement ends, and unwinding does not go that way.
//
// This was found by `tests/names.sh` rather than by reasoning - clang emitted
// `_Unwind_Resume` for the function and cxx1 did not, which reads as a naming
// difference and was a missing cleanup region. The condition's object needs
// the same `built` record a block keeps for what it constructs, or the pad has
// nothing to destroy.
extern "C" int printf(const char *, ...);

// Defined out of line, because on x86_64-linux clang emits only the C2 form of
// an *inline* constructor where cxx1 emits C1 and C2 - a recorded divergence
// that has nothing to do with conditions, and one this case need not carry.
struct Guard {
    int v;
    Guard(int n);
    ~Guard();
    operator bool() const { return v != 0; }
};
Guard::Guard(int n) : v(n) { printf("ctor %d\n", n); }
Guard::~Guard() { printf("dtor %d\n", v); }
Guard make(int n) { return Guard(n); }

void boom() { throw 7; }

// The throw and the handler are in different functions because a local with a
// destructor and a `try` in one function is refused - see rung 6.4.
int cond() { if (Guard g = make(5)) { boom(); return g.v; } return 0; }

int main() {
    try { cond(); } catch (int e) { printf("caught %d\n", e); }
    return 0;
}
