// **`(T(x) == T(y))` is a parenthesised comparison of two temporaries, not a
// cast.** A `(` before a type name opens a C-style cast only when the type-id
// closes with its own `)`; `T(x)` after it is a functional-cast expression, so
// the cast attempt backs off. This is the shape an `assert(T(x) == T(y))`
// expands to, its condition wrapped in parentheses.
extern "C" int printf(const char *, ...);
template <int N> struct V {
    double a[N];
    explicit V(double f) { for (int i = 0; i < N; ++i) a[i] = f; }
    V(double x, double y, double z) { a[0] = x; a[1] = y; a[2] = z; }
    bool operator==(const V &o) const { return a[0] == o.a[0]; }
};
typedef V<3> Vec3;
int main() {
    bool same = (Vec3(2.5) == Vec3(2.5, 2.5, 2.5));
    printf("%d\n", same ? 1 : 0);
    return 0;
}
