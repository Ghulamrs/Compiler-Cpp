// A converting constructor called to make an argument.
//
// **[over.ics.user].** `f("x")` where the parameter is `const std::string &`
// builds a string and binds the reference to it. This compiler did that in an
// initialisation - `S s = 7;` always worked - and not in a call, so a library
// could be written and not used: `m["key"]` and every function taking a string
// by const reference needed the argument spelled out.
//
// The rules that come with it, each with a line below:
//
//   - **It ranks below every standard conversion** - [over.ics.rank]/2. A
//     candidate needing a constructor loses to one that does not, however bad
//     the other's conversion is, which is why `take(7)` finds `take(int)`
//     rather than `take(const S &)` and why `take('c')` does too - a char to
//     int conversion still beats making an S.
//   - **`explicit` means not this.** That is the whole of what the keyword is
//     for, and the case for it is a refusal.
//   - **A non-const reference gets nothing**, because the object made here has
//     nowhere to live past the call.
//   - **Only one per sequence** - [over.ics.user]/1. Two conversions in a row
//     is not a sequence the language forms, and `Far` below is the case:
//     nothing makes a `Far` out of an `int` even though a `Near` is made out of
//     an int and a `Far` out of a `Near`.
//   - **Two that both match is an ambiguity**, and the refusal is the answer.

extern "C" int printf(const char *, ...);

struct S {
    int v;
    S(int x) : v(x * 10) {}
    S(const S &o) : v(o.v) {}
};

struct E {
    int v;
    explicit E(int x) : v(x) {}
};

struct Near { int v; Near(int x) : v(x) {} };
struct Far  { int v; Far(const Near &n) : v(n.v) {} };

static int byRef(const S &s) { return s.v; }
static int byValue(S s) { return s.v + 1; }

// Two overloads: the exact one must win, and the conversion must not be tried.
static int ranked(const S &s) { return 1; }
static int ranked(int n) { return 2; }

// A conversion beside an ellipsis, which it must beat.
static int overEllipsis(const S &s) { return 3; }
static int overEllipsis(...) { return 4; }

static int takesFar(const Far &f) { return f.v; }
static int takesE(const E &e) { return e.v; }

int main(void) {
    printf("%d %d\n", byRef(7), byValue(7));          // 70 71
    printf("%d %d\n", ranked(7), ranked('c'));        // 2 2 - the int overload
    printf("%d\n", overEllipsis(5));                  // 3 - beats the ellipsis

    Near n(9);
    printf("%d\n", takesFar(n));                      // 9 - one conversion, allowed

    E e(4);
    printf("%d\n", takesE(e));                        // 4 - explicit, given one

    // A conversion in a member call, and in an operator, which reach the
    // argument through different paths in this compiler.
    S made(1);
    printf("%d %d\n", byRef(made), byValue(made));    // 10 11
    return 0;
}
