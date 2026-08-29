// `decltype` - rung 7.2, and the rule that matters is about *tokens* rather
// than about the tree they build.
//
// [dcl.type.simple]: an unparenthesised name answers what that entity was
// *declared* as; anything else answers the expression's type, with a `&`
// added when it is an lvalue. So `decltype(n)` is int and `decltype((n))` is
// int & - the same characters but for one pair of parentheses, and the
// second binds to `n` where the first makes a fresh variable.
//
// A reference variable is what forces the lookup rather than the tree: every
// mention of one is lowered here to a dereference, so the *expression* `ref`
// has type int where the declaration said `int &`. Asking the symbol table is
// the only way to answer what was written.
extern "C" { int printf(const char *, ...); }

struct P { int x; double y; };
template <class T> struct Box { T v; int size() { return (int)sizeof(T); } };

int twice(int v) { return v + v; }

int global = 5;
decltype(global) alsoInt = 6;

int main() {
    int n = 3;
    double d = 1.5;
    P p;
    p.x = 7;
    p.y = 2.5;

    decltype(n) a = 10;              // int
    decltype(d) b = 0.5;             // double
    decltype(p.x) c = 4;             // the member's declared type
    decltype(twice(n)) e = twice(n); // a call is a prvalue: int
    decltype(n + 1) f = 9;           // so is arithmetic

    decltype((n)) r = n;             // int &, and writing through it writes n
    r = 99;
    printf("%d %.1f %d %d %d %d %d\n", a, b, c, e, f, n, r);

    int &ref = n;
    decltype(ref) r2 = n;            // int &, because that is the declaration
    r2 = 42;

    const int k = 3;
    decltype(k) k2 = 4;              // const int

    Box<decltype(n)> box;            // Box<int>
    box.v = 8;

    int arr[3];
    decltype(arr[0]) first = arr[0]; // a subscript is an lvalue: int &
    first = 77;

    printf("%d %d %d %d %d %d\n", n, r2, k2, box.v, box.size(), arr[0]);
    printf("%d %d %d %d\n", (int)sizeof(a), (int)sizeof(b), (int)sizeof(c),
           (int)sizeof(decltype(d)));
    printf("%d\n", alsoInt);
    return 0;
}
