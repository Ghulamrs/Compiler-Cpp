// **A reference-to-array parameter binds an array lvalue without decay** -
// `const double (&)[N]` takes a `double[N]`, the element gaining const, which
// is a qualification conversion. The argument is not decayed to a pointer the
// way a by-value parameter's would be. [dcl.init.ref], [over.ics.ref].
extern "C" int printf(const char *, ...);
template <int N> struct V {
    double a[N];
    explicit V(const double (&arr)[N]) { for (int i = 0; i < N; ++i) a[i] = arr[i]; }
};
int main() {
    double r[3] = {2, 4, 6};
    V<3> v(r);
    printf("%.0f %.0f %.0f\n", v.a[0], v.a[1], v.a[2]);
    return 0;
}
