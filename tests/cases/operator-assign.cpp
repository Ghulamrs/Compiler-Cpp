// `operator=` written by hand - both the copy assignment and the converting one.
//
// **A class that owns something cannot be assigned by copying its bytes**, and
// this is the operator that says what to do instead. Two shapes, and this
// compiler reaches them by different routes:
//
//   - `b = a`, where the right side is the class itself. This is the copy
//     assignment, and it is called instead of the member-wise copy the compiler
//     would otherwise emit. `assigns` counts it, which is the only way to see
//     from inside the program that the function ran at all.
//   - `t = 5`, where the right side is something else. [over.ass] makes
//     assignment a member and puts no constraint on what it takes, so this is
//     an ordinary overload - but the built-in check has to be asked *after* the
//     operator, because on its own it knows only that an int is not a T.
//
// It answers `X &`, so `(b = a).v` reads the object that was assigned to and
// `c = b = a` groups from the right, both of which are checked below.

extern "C" int printf(const char *, ...);

int assigns = 0;

struct S {
    int v;
    S() : v(0) {}
    S(const S &o) : v(o.v) {}
    S &operator=(const S &o) { assigns++; v = o.v; return *this; }
};

struct T {
    int v;
    T &operator=(int n) { v = n * 2; return *this; }
};

struct Held {
    int v;
    Held &operator=(const Held &o) { v = o.v + 1; return *this; }   // not a plain copy
};

int main(void) {
    S a;
    a.v = 3;
    S b;
    b = a;
    S c;
    c = b = a;                       // right to left, two calls
    T t;
    t = 5;                           // the converting one
    Held h;
    h.v = 0;
    Held g;
    g.v = 41;
    h = g;                           // proves the function ran, not a byte copy
    // `(b = a).v` is its own statement, not a printf argument beside the read
    // of `assigns`: the order function arguments are evaluated in is
    // unspecified, and the Windows box evaluated the assignment first and
    // counted 4 where clang and the Itanium targets counted 3. The first
    // version of this case recorded one compiler's choice as the language's.
    const int chained = (b = a).v;
    printf("%d %d %d %d %d %d\n", b.v, c.v, assigns, t.v, chained, h.v);
    return 0;
}
