// A static data member is declared in its class and *defined* at namespace
// scope with the class in front - `const double S::k = 2.5;` - and that
// definition is where its storage comes from. [expr.const]/3 then makes a
// const object of integral type a constant expression when read, and every
// compiler folds a const floating one the same way; cxx1 made that read-back
// for a namespace-scope const and not for a static member, which is kept in
// a table of its own so that nothing walking a layout has to skip it.
//
// So this ran into `expected a constant initialiser, and this is not a
// constant` on the two lines below that build one constant from another, and
// `expected an array length` on the third - for a member that is a constant
// by exactly the rule an ordinary const object is.
//
// The read-back is keyed by the member's linkage symbol rather than by the
// name it is written with, which is what `Derived::k` below is here to check:
// a base's static member named through a derived class is one object under
// two spellings.
extern "C" int printf(const char *, ...);

struct S {
    static const double k;
    static const int n;
    static const int inClass = 3;   // folded, no storage at all
    double v;
    double scaled() const { return v * k; }
};
const double S::k = 2.5;
const int S::n = 4;

struct Derived : S { };

// Each of these is a constant initialiser only because the member is read back.
const double twiceK = S::k * 2.0;
const int    twiceN = S::n * 2;
const double viaDerived = Derived::k + 0.5;
int sized[S::n];                    // and an array length, which is /3's own ground

int main() {
    const double localK = S::k * 4.0;          // the same read-back in a local
    S s; s.v = 3.0;
    printf("%.2f %d %.2f\n", twiceK, twiceN, viaDerived);
    printf("%d %.2f %.2f %d\n", (int)(sizeof sized / sizeof sized[0]),
           localK, s.scaled(), S::inClass);
    // The object still has storage and an address; only its value is folded.
    // The addresses are taken for a second reason too: clang folds a const
    // every reader of which is a constant expression and emits no symbol at
    // all, so without this the names suite reports symbols cxx1 has and clang
    // has not - a question about emission wearing the shape of a mangling bug.
    const double *p = &S::k;
    const double *pk = &twiceK, *pd = &viaDerived;
    const int *pn = &twiceN;
    printf("%.2f %d\n", *p, (int)(*p == S::k));
    printf("%.2f %d %.2f\n", *pk, *pn, *pd);
    return 0;
}
