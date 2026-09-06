// cxx1 re-enters parsing from a saved token index in eight places, and each
// door used to write its own list of per-function state to put back. Each list
// was written separately, which is how `alive_` came to be missing from one of
// them - a lambda in a function holding any destructible object emitted the
// *enclosing* function's destructors against its own frame. A review found six
// more fields missing from that same list and a worse door beside it.
//
// There is one capture list now, captureFunctionState, so a field added to the
// parser is added there and nowhere else. What each line below reaches:
//
//   noexcept over a member template  - mayThrow_, which the replay leaked, so
//                                      noexcept(e) answered false for a body
//                                      that cannot throw
//   a try around a lambda            - inTryBody_ and functionHasTry_, whose
//                                      leak made the lambda inherit the
//                                      enclosing function's two refusals
//   a condition declaration          - conditionDecl_, which refused any body
//                                      replayed inside one
//   a label in a lambda and outside  - labels_, which collided across the
//                                      return-type deduction
//   a local with a destructor        - alive_, the original: an object built in
//                                      a lambda was destroyed again in main
//   sizeof and decltype of a call    - pendingTemps_: an unevaluated operand
//                                      registered a temporary that was later
//                                      destroyed though nothing built it
//   Box<Box<int>> with no space      - angleSplit_, the one-slot >> mark, spent
//                                      by a class instantiated between halves
//   a default argument naming a
//   member of its own class          - currentClass_, which applyDefaults left
//                                      as the *caller's*
#include "lifetime.h"

struct S {
    int v;
    S(int n);
    ~S();
    template <int M> int get() noexcept { return M * 2; }
    enum { N = 3 };
    static const int m = 4;
    int withDefaults(int a = N, int b = m) { return a * 100 + b; }
};
S::S(int n) : v(n) { lfBuilt(this, "S"); }
S::~S() { lfGone(this, "S"); }

template <class T> struct Inner { T v; };
template <class T> struct Box { Inner<Inner<T>> in; };

// inTryBody_ / functionHasTry_ - a lambda written inside a try. In a function
// of its own because a local with a destructor and a `try` in one function is
// refused for this compiler's own recorded reason, which is rung 6 and not this.
static int tryHoldsALambda() {
    int r = 0;
    try { auto f = [](int a) { return a + 1; }; r = f(4); } catch (int) { r = -1; }
    return r;
}

static int live = 0;
// Out of line for the reason every other class in these cases is: written
// inside the class they are inline, and clang then emits only the variants a
// use asks for where cxx1 emits C1 and C2.
struct Counted { int v; Counted(int n); ~Counted(); };
Counted::Counted(int n) : v(n) { live++; }
Counted::~Counted() { live--; }
static Counted make(int n) { return Counted(n); }

int main() {
    lfWatch();
    S s(1);

    // mayThrow_ - the replayed member template must not report a throw
    printf("%d %d\n", (int)noexcept(s.get<7>()), s.get<7>());

    int r = tryHoldsALambda();

    // conditionDecl_ - a member template replayed inside a condition
    if (int n = s.get<4>()) r += n;

    // labels_ - the lambda has one of its own, and so does main
    auto loops = []() { int n = 0; again: n++; if (n < 3) goto again; return n; };
again:
    r += 0;
    printf("%d %d\n", r, loops());

    // alive_ - built inside the lambda, and destroyed exactly once
    auto owns = []() { S inner(9); return inner.v; };
    printf("%d\n", owns());

    // pendingTemps_ - unevaluated operands build nothing. The real call below
    // is not decoration: an unevaluated operand does not odr-use what it names,
    // so clang emits no symbol for `make` or for Counted's specials and the
    // names suite would report a difference about emission rather than about
    // naming. Calling it once makes both compilers emit both.
    int a = (int)sizeof(make(3));
    decltype(make(4)) *p = 0;
    int real = make(6).v;
    printf("%d %d %d %d\n", a, live, (int)(p == 0), real);

    // angleSplit_ - the inner class is instantiated between the two '>'
    Box<Box<int>> b;
    b.in.v.v.in.v.v = 5;

    // currentClass_ - the default names its own class's enumerator and member
    printf("%d %d\n", b.in.v.v.in.v.v, s.withDefaults());
    return 0;
}
