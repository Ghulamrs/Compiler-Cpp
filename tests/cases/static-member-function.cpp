// `static` on a member function - a member with no `this`.
//
// **It is a member in every way but the one that matters at a call.** It is
// named inside the class, it obeys access, it overloads against the others and
// it is mangled as a member - and it is not passed an object. So the question a
// call site asks is no longer "does this belong to a class?" but "does this
// need a `this`?", and those two came apart here for the first time.
//
// The four ways it is reached are all below: `C::f(...)`, its bare name from
// inside another member, its bare name from inside another *static* member -
// where there is no `this` local to fall back on - and through an object, which
// [class.static]/1 allows. That last one still evaluates the object expression
// ([expr.ref]), which is what `mk().twice(5)` is here to prove: `made` counts
// it. The same rule and the same Comma the static *data* member path uses.
//
// The names are the ABI's, measured: Itanium spells a static member exactly as
// it spells any other, `_ZN1S5twiceEi`, while the Microsoft ABI has a code of
// its own for each access - S public, K protected, C private - and writes no
// `this` qualifier after it: `?twice@S@@SAHH@Z` against `?plain@S@@QEAAHH@Z`.

extern "C" int printf(const char *, ...);

int made = 0;

struct S {
    int v;

    static int twice(int a) { return a * 2; }        // defined in the class
    static int shifted(int a);                       // and outside it
    static int none(void) { return 7; }
    static int chained(int a) { return twice(a) + 1; }   // a static calls a static
    static int over(int a) { return a; }                 // and they overload
    static int over(int a, int b) { return a + b; }

    int plain(int a) { return twice(a) + v; }        // a member calls one
    int constly(void) const { return twice(v); }     // a const member too
};

int S::shifted(int a) { return a + 100; }

struct Base {
    static int fromBase(int a) { return a + 1000; }
};
struct Derived : Base {
    int useInherited(void) { return fromBase(1); }   // found up the base chain
};

namespace N {
    struct T { static int f(int a) { return a + 50; } };
}

class Guarded {
public:
    static int open(int a) { return secret(a); }     // reaches its own private
private:
    static int secret(int a) { return a * 3; }
};

S mk(void) { made++; S s; s.v = 0; return s; }

int main(void) {
    S s;
    s.v = 4;
    S *p = &s;
    Derived d;
    printf("%d %d %d %d\n", S::twice(3), S::shifted(2), S::none(), S::chained(3));
    printf("%d %d %d %d\n", S::over(2), S::over(2, 3), s.plain(1), s.constly());
    printf("%d %d %d\n", s.twice(6), p->twice(7), mk().twice(5));
    printf("%d %d %d %d\n", made, N::T::f(1), d.useInherited(), Guarded::open(2));
    return 0;
}
