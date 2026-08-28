// Passing and returning a class by value.
//
// **Whether the copy is a call decides how the class is passed**, and that is
// measured rather than reasoned: cl passes a class with a non-trivial copy
// constructor by address whatever its size, and clang does the same on both
// Itanium targets, where a trivially copyable class of the same size goes in a
// register. So the two halves have to agree - the copy has to happen, and it
// has to happen into storage the callee can be handed the address of.
//
// The caller makes the copy and the caller destroys it, which is the Itanium
// rule; the Microsoft ABI has the callee destroy its parameter instead, and
// docs/CONFORMANCE.md records what that costs. The copy goes at the end of the
// *full expression* and not when the call it was made for returns, which is
// what the `useD` line below is watching.
//
// Nothing here counts constructor calls where elision could change the count.
// C++11 permits eliding the copy and does not require it - clang elides at -O0
// and cl does not - so a case that counted them would be recording one
// compiler's choice as though it were the language's.
extern "C" { int printf(const char *, ...); }

struct Plain { int n; };                       // trivial, and stays in a register
struct Wide  { int a[8]; };                    // trivial, and goes in memory

class Counted {
public:
    Counted();
    Counted(const Counted &o);
    int n;
};
Counted::Counted()                  { n = 1; }
Counted::Counted(const Counted &o)  { n = o.n + 100; }

class Noisy {
public:
    Noisy();
    Noisy(const Noisy &o);
    ~Noisy();
    int n;
};
Noisy::Noisy()                { n = 1; }
Noisy::Noisy(const Noisy &o)  { n = o.n + 100; }
Noisy::~Noisy()               { printf("  ~Noisy %d\n", n); }

int takePlain(Plain p)     { return p.n; }
int takeWide (Wide w)      { return w.a[7]; }
int takeCounted(Counted c) { return c.n; }
int takeNoisy(Noisy d)     { return d.n; }

Plain   givePlain()   { Plain p; p.n = 3; return p; }
Counted giveCounted() { Counted c; return c; }

// Two levels: the parameter here is itself passed on by value, so it is copied
// again on the way down.
int passOn(Counted c) { return takeCounted(c); }

int main() {
    Plain p; p.n = 5;
    Wide w; w.a[7] = 6;
    Counted c;

    // A trivial class is unchanged by the trip.
    printf("%d %d\n", takePlain(p), takeWide(w));

    // A non-trivial one is copied on the way in, so the callee sees n + 100.
    printf("%d\n", takeCounted(c));
    printf("%d\n", c.n);

    // Two levels down: copied on the way into passOn and again into
    // takeCounted.
    printf("%d\n", passOn(c));

    // A returned value, trivial and not.
    Plain q = givePlain();
    Counted r = giveCounted();
    printf("%d %d\n", q.n, r.n);

    // A temporary passed straight in: the callee's copy is the returned object
    // itself, built where the argument had to go.
    printf("%d\n", takeCounted(giveCounted()));

    // **When the copy is destroyed is a question the two ABIs answer
    // differently**, so nothing observable may sit between the call and that
    // moment. Itanium destroys it at the end of the full expression, in the
    // caller; Microsoft destroys it inside the callee, before it returns.
    // Taking the result into a variable ends the full expression before the
    // printf, which is the same on both. See tests/cases/by-value-destructor.
    {
        Noisy d;
        int seen = takeNoisy(d);
        printf("takeNoisy %d\n", seen);
        printf("after\n");
    }
    return 0;
}
