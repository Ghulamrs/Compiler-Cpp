// **Function templates overload, and deduce non-type parameters.** Every
// function template of a name is a candidate; a call deduces against each and
// overload resolution ranks the specializations - so `10.0 * v` reaches
// operator*(double, V<N>) while `v * 2.0` reaches operator*(V<N>, double),
// two templates of one name. A non-type parameter is deduced from a class
// template argument: V<N> against V<3> gives N=3. Measured against clang
// -std=c++11 -pedantic-errors; the manglings are checked by names.sh.
extern "C" int printf(const char *, ...);

template <int N> struct V { double a[N]; };

template <int N> V<N> operator*(V<N> v, double s) {
    V<N> r; for (int i = 0; i < N; ++i) r.a[i] = v.a[i] * s; return r;
}
template <int N> V<N> operator*(double s, V<N> v) {
    V<N> r; for (int i = 0; i < N; ++i) r.a[i] = s * v.a[i]; return r;
}
template <int N> V<N> operator+(V<N> a, const V<N> &b) {
    V<N> r; for (int i = 0; i < N; ++i) r.a[i] = a.a[i] + b.a[i]; return r;
}
template <int N> int sizeOf(V<N>) { return N; }

int main() {
    V<3> v; v.a[0] = 1; v.a[1] = 2; v.a[2] = 3;
    V<3> left = v * 2.0;
    V<3> right = 10.0 * v;
    V<3> sum = left + right;
    printf("%.0f %.0f %.0f\n", sum.a[0], sum.a[1], sum.a[2]);
    printf("N=%d\n", sizeOf(v));
    return 0;
}
