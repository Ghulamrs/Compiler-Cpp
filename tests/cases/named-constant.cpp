// **A named constant is a constant expression** - [expr.const]/3: a const
// object of integral type, initialised with a constant expression, is one.
// This is the rule that lets C++ write `const int n = 4; int a[n];` where C
// has to reach for a macro or an enum, and it is what `constexpr` on a
// variable is built on rather than something separate from it.
//
// Every context that wants an integer constant is exercised, because each
// asks through a different door into the same fold().
extern "C" int printf(const char *, ...);

const int n = 4;
const int m = n * 2 + 1;
enum { E = m - n };
int outer[m];

template <int N> struct Sized { int v[N]; };

int classify(int c) {
    const int low = 1;
    const int high = low + 2;
    switch (c) {
        case low:  return 10;
        case high: return 30;
        default:   return 0;
    }
}

int main() {
    const int local = n + 1;
    int inner[local];
    Sized<m> s;

    printf("%d %d %d\n", n, m, E);
    printf("%d %d %d\n", (int)(sizeof outer / sizeof outer[0]),
                         (int)(sizeof inner / sizeof inner[0]),
                         (int)(sizeof s.v / sizeof s.v[0]));
    printf("%d %d %d\n", classify(1), classify(3), classify(2));

    // **Being a constant expression does not stop it being an object.** The
    // value is known while compiling and the object still exists, still has
    // an address and can still be read through one - which is why this
    // compiler emits storage for it. clang, left to itself, folds every use
    // and emits nothing at all, so without these lines the two disagree about
    // which symbols a translation unit contains.
    const int *pn = &n;
    const int *pm = &m;
    printf("%d %d\n", *pn, *pm);
    return 0;
}
