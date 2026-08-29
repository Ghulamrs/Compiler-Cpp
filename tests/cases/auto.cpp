// `auto` - rung 7.1, and the rule is one this compiler already had.
//
// [dcl.spec.auto] deduces a variable's `auto` **as if by template argument
// deduction from a function call**, which is not an analogy worth borrowing
// but the rule itself. So `deduceOne` does the work - the same function a
// call's template arguments go through - and Kind::Deduced stands where
// Kind::TemplateParam stands in a pattern. Everything that follows from that
// comes free: an array decays, a top-level const is dropped, and a reference
// looks through itself and keeps what it found.
//
// Which is why the four lines below differ. `a` is int, because `n` decays to
// its value and its const goes; `r` is int & and writing through it is
// writing to `n`; `cr` is const int & and binds to the same object; `p` is
// int * because `auto *` says a pointer is what to deduce through.
//
// The initialiser is read twice - once to learn its type, once to build it -
// with the tokens put back in between. Reading it once and threading the
// expression through every branch of the declaration path would save a parse
// that costs nothing.
extern "C" { int printf(const char *, ...); }

struct Point { int x; int y; };
int twice(int v) { return v + v; }

// Not const, deliberately: a const object at file scope is one clang folds
// away and gives no storage, which is a difference about constants rather
// than about `auto` and belongs in a case of its own.
auto atFileScope = 11;
auto alsoHere = 2.5;

int main() {
    auto a = 7;
    auto b = 2.5;
    auto c = 'x';
    const auto d = twice(a);

    int n = 4;
    auto *p = &n;
    auto &r = n;
    const auto &cr = n;
    r = 9;

    printf("%d %.1f %c %d %d %d %d\n", a, b, c, d, *p, r, cr);
    printf("%d %d %d %d\n", (int)sizeof(a), (int)sizeof(b), (int)sizeof(c),
           (int)sizeof(p));

    // A class, and an array that decays.
    Point one;
    one.x = 3;
    one.y = 4;
    auto copy = one;
    copy.y = 5;
    printf("%d %d %d\n", one.y, copy.x, copy.y);

    int table[3];
    table[0] = 6;
    auto first = table;             // int *, not int[3]
    printf("%d %d\n", *first, (int)sizeof(first));

    printf("%d %.1f\n", atFileScope, alsoHere);
    return 0;
}
