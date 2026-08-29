// `constexpr` functions, evaluated while compiling.
//
// **C++11 lets the body be one return statement and nothing else** - no
// local, no loop, no second statement - and that restriction is what makes
// this a fold rather than an interpreter: running the function is folding one
// expression with its parameters standing for the arguments. There is no
// program counter and no state to carry, and recursion falls out of the
// folding already being recursive. C++14 relaxed the rule and is out of scope
// here, so the restriction is a gift rather than a limitation.
//
// A constexpr function is an ordinary function as well, compiled and callable
// at run time - `square(n)` below is that, with n a variable. This is an
// *extra* way to reach it, taken only where a constant is required.
extern "C" int printf(const char *, ...);

constexpr int square(int x) { return x * x; }
constexpr int cube(int x) { return x * square(x); }
constexpr int factorial(int n) { return n <= 1 ? 1 : n * factorial(n - 1); }
constexpr int add(int a, int b) { return a + b; }

struct Board {
    static constexpr int side = 4;
    // In C++11 a constexpr member function is implicitly const, which is a
    // mangling difference and not a nicety: clang spells this _ZNK5Board...
    // and anything that spelled it _ZN5Board... would not link against it.
    constexpr int twice(int n) { return n * 2; }
};

int grid[square(4)];
int deep[factorial(5)];
int board[square(Board::side)];
constexpr int mixed = add(square(3), cube(2));

int main() {
    int local[cube(2)];
    printf("%d %d %d %d\n", square(5), cube(3), factorial(6), add(2, 3));
    printf("%d %d %d %d %d\n", (int)(sizeof grid / 4), (int)(sizeof deep / 4),
           (int)(sizeof board / 4), (int)(sizeof local / 4), mixed);

    Board b;
    printf("%d %d\n", b.twice(3), Board::side);

    int n = 7;
    printf("runtime %d\n", square(n));

    // A constexpr object is still an object - see named-constant.cpp. Taking
    // its address is also what keeps this case comparable: clang folds every
    // use of `mixed` and emits no symbol for it at all, so a case that never
    // takes one makes the two compilers disagree about what this translation
    // unit contains.
    const int *pm = &mixed;
    printf("%d\n", *pm);
    return 0;
}
