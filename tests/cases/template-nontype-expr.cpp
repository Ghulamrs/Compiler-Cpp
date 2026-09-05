// **A non-type template argument that is an expression of the parameters**,
// `V<N + M>` - the concatenation's dimension. It folds at instantiation, where
// N and M are concrete. On Itanium clang spells such an argument symbolically
// (the expression between X and E); cxx1 folds it to the concrete value, a
// name that links with itself but not with clang's - recorded in .nonames.
extern "C" int printf(const char *, ...);
template <int N> struct V { double a[N]; };
template <int N, int M>
V<N + M> cat(const V<N> &x, const V<M> &y) {
    V<N + M> r;
    for (int i = 0; i < N; ++i) r.a[i] = x.a[i];
    for (int i = 0; i < M; ++i) r.a[N + i] = y.a[i];
    return r;
}
int main() {
    V<1> a; a.a[0] = 5;
    V<2> b; b.a[0] = 6; b.a[1] = 7;
    V<3> c = cat(a, b);
    printf("%.0f %.0f %.0f\n", c.a[0], c.a[1], c.a[2]);
    return 0;
}
