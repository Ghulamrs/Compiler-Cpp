// `operator int()` - a conversion function, and where one is used.
//
// **The mirror of the converting constructor.** That one is a constructor of
// the target; this is a member of the source, and it is the half that makes
// `if (stream)` and `if (stream >> v)` mean anything - a stream converted to
// something a test can be made of.
//
// Its name *is* its return type, which is the one place in the language where
// those two are the same thing, and it is what makes the parsing awkward: a
// member declaration with no type in front of it. One lookahead in `specifiers`
// answers all three spellings - the declaration inside the class, the
// definition outside it, and the replay of a body held inside, which restarts
// at the `operator` and would otherwise meet a declaration with no type.
//
// The names are the ABI's, measured: Itanium writes `cv` and then the type,
// `_ZNK1ScvbEv`; the Microsoft ABI writes `??B` and, unlike every other
// operator, its return type - `??BS@@QEBA_NXZ`.
//
// Where it is used, each with a line below: a condition, `!`, `?:`, `&&`, an
// initialisation, an assignment, a return, an argument, and arithmetic - the
// last through [over.match.oper]/9, where no operator matched and the built-in
// candidates are offered a class that can become a number.
//
// **A condition and an arithmetic operator do not ask the same question**, and
// `S` below is the proof: it has `operator int` *and* `operator bool`, so
// `if (a)` is unambiguous - [conv]/3 wants bool, and one of them is bool - while
// `a + 1` is ambiguous, the built-in operators being offered both. clang refuses
// that line, so it is not written here; `N`, which has one conversion, does the
// arithmetic. Answering the two alike gave `a + 1` a value the program has no
// meaning for.
//
// And where it must *not* be used: a written operator wins over it, and an
// exact match wins over it, both being better than a user-defined conversion by
// [over.ics.rank]/2.

extern "C" int printf(const char *, ...);

struct S {
    int v;
    operator int() const { return v * 2; }        // defined inside
    operator bool() const;                        // and outside
};

S::operator bool() const { return v != 0; }

struct P {
    int *p;
    operator int *() const { return p; }          // to a pointer
};

// One conversion, so the built-in operators are not ambiguous for it.
struct N {
    int v;
    operator int() const { return v * 2; }
};

struct W {
    int v;
    operator int() const { return v; }
};
static int operator+(const W &a, int n) { return 999; }   // beats the built-in

static int takesInt(int n) { return n; }
static int ranked(int n) { return 1; }
static int ranked(const S &s) { return 2; }
static int give(void) { S s; s.v = 21; return s; }

int main(void) {
    S a; a.v = 21;
    S z; z.v = 0;

    if (a) printf("condition ");
    if (!z) printf("negated ");
    printf("%d %d\n", a ? 1 : 0, (a && 1) ? 1 : 0);

    int n = a;                                    // initialisation
    int m = 0;
    m = a;                                        // assignment
    printf("%d %d %d %d\n", n, m, give(), takesInt(a));

    N k; k.v = 21;
    printf("%d %d\n", k + 1, k == 42);            // arithmetic, and comparison

    printf("%d\n", ranked(a));                    // 2 - the exact match wins

    W w; w.v = 5;
    printf("%d\n", w + 1);                        // 999 - the written one wins

    int cell = 7;
    P p; p.p = &cell;
    int *through = p;                             // a conversion to a pointer
    printf("%d %d\n", *through, (int)(bool)a);
    return 0;
}
