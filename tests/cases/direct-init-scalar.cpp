// `int z(5);` - direct-initialisation of a scalar, [dcl.init]/16, and the
// parenthesised half of it. cxx1 read the parentheses as a parameter list and
// answered "expected a type" at the 5: a class with a constructor had this
// since rung 3 and nothing else did. This tree's own <utility> writes
// `T held(a);`, so `std::swap` on two ints failed *inside the header* -
// std-swap-two-ints.cpp is that program, and it is why the shape was found.
//
// **What decides it is [dcl.ambig.res]/1**: anything that can be a declaration
// is one. So the question is only whether what follows the `(` could begin a
// parameter, and `atParenInitialiser` is the one place that asks it - for a
// local, for a file-scope object, for `auto`, and for the class-with-no-
// constructor path that already existed. The four shapes it says no to are all
// here: an empty pair, a type name, an ellipsis, and a parameter pack whose
// name is not yet a type.
extern "C" int printf(const char *, ...);

typedef int Alias;

// These four stay function declarations, which is the whole difficulty.
int declaredF();
int declaredG(int);
int declaredH(void);
int declaredI(Alias);

int declaredF() { return 1; }
int declaredG(int) { return 2; }
int declaredH(void) { return 3; }
int declaredI(Alias) { return 4; }

int helper(int k) { return k + 1; }

// At file scope as well as inside a function.
int global(5);
const double scaled(1.5);

struct T { int v; T() : v(1) {} };
struct S { int v; S(T t) : v(t.v + 1) {} };

// The most vexing parse: a function taking a pointer to a function returning T,
// which is what C++ says this is - not an object.
S vexing(T());

// A template's own parameter, which is where <utility> meets this.
template <class U> U twiceOf(U a) { U held(a); return held + held; }

// A pack, whose name is not a type until this is instantiated: the `(` after
// `howMany` opens a parameter list and the scan has to say so.
template <class... Us> int howMany(Us... a) { return (int)sizeof...(a); }

int main() {
    int a(5);
    double d(1.5);
    int *p(0);
    const int c(9);
    int fromCall(helper(1));
    auto deduced(7);
    int two(2), three(3);
    static int kept(11);

    // A declaration inside a `for`'s init-statement is an ordinary one.
    int sum = 0;
    for (int k(0); k < 3; k++) sum += k;

    printf("%d %d %d %d %d %d %d %d %d %d\n", a, (int)d, p == 0, c, fromCall,
           deduced, two + three, kept, sum, global);
    printf("%d %d %.1f %d\n",
           declaredF() + declaredG(0) + declaredH() + declaredI(0),
           twiceOf(21), (double)scaled, howMany(1, 2, 3));
    return 0;
}
