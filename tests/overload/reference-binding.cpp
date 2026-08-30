// Ranking a reference parameter, which is a question about the argument's
// value category rather than about a conversion: an rvalue reference is not
// viable for an object that has an address, and where both are viable it is
// the better match. Those are the two lines of rankArgument that make a move
// get chosen over a copy, checked here against clang directly.
extern "C" { int printf(const char *, ...); }

class S {
public:
    int x;
    S(int n) { x = n; }
};

int f(S &s)        { return 1 + 0 * s.x; }
int f(const S &s)  { return 2 + 0 * s.x; }

int g(const S &s)  { return 3 + 0 * s.x; }
int g(S &&s)       { return 4 + 0 * s.x; }

int main(void) {
    S a(1);
    const S b(2);
    // a non-const object prefers S &; a const one can only reach const S &
    printf("%d %d\n", f(a), f(b));
    // an lvalue takes const S &; a cast to S && takes the rvalue overload
    printf("%d %d\n", g(a), g(static_cast<S &&>(a)));
    return 0;
}
