// The range-based `for` - rung 7.3, and [stmt.ranged] is a rewrite that this
// performs rather than a construct that needs new machinery.
//
// The standard says what `for (T x : a)` means by writing another loop, and
// every node that loop needs was already here:
//
//     T *__b = a;              the array, decayed
//     T *__e = __b + N;
//     for (; __b != __e; __b = __b + 1) { T x = *__b; <body> }
//
// The range is evaluated exactly once, which is what binding it to a name
// buys in the standard's version and what assigning it to `__b` buys here.
//
// **Telling a range-for from an ordinary one takes a scan, and a `?` claims
// the next `:`.** `for (int i = 0, n = c ? 2 : 5; ...)` has a colon in it and
// is not a range-for, so the question marks are counted. The last loop below
// is there to hold that.
extern "C" { int printf(const char *, ...); }

struct P { int x; int y; };

int main() {
    int a[4];
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4;

    int total = 0;
    for (auto v : a) total = total + v;
    printf("%d\n", total);

    for (int v : a) printf("%d ", v);
    printf("\n");

    double d[3];
    d[0] = 0.5; d[1] = 1.5; d[2] = 2.5;
    for (const auto v : d) printf("%.1f ", v);
    printf("\n");

    // The element is a class, so the loop variable is a copy of one.
    P ps[2];
    ps[0].x = 1; ps[0].y = 2;
    ps[1].x = 3; ps[1].y = 4;
    for (auto p : ps) printf("(%d,%d) ", p.x, p.y);
    printf("\n");

    // Nested, with a break in the inner one.
    int b[2];
    b[0] = 10; b[1] = 20;
    for (auto x : a) {
        for (auto y : b) {
            if (y == 20) break;
            printf("%d-%d ", x, y);
        }
    }
    printf("\n");

    for (int i = 0, n = a[0] ? 2 : 5; i < n; i++) printf("%d", i);
    printf("\n");
    return 0;
}
