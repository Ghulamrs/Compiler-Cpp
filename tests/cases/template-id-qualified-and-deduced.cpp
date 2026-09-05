// Two more steps a real program needs:
//
//  - `lim::Bounds<int>::most()` - a *namespace-qualified* class template-id used
//    as a qualifier, which is the shape `std::numeric_limits<int>::max()` has.
//    qualifiedTypeEnd stops at the `<`, which is what says the name is a class
//    template; the arguments are read against it, the class made, and the `::`
//    after them reaches a static member the way an unqualified template-id does.
//  - `m.dot(v)` - a member function template whose arguments are *deduced* from
//    the call rather than written, [temp.deduct.call], including the operator
//    form `m * v`, which the ranking reaches because the specialization is
//    registered under the plain member name as well as under its arguments.
//
// A user-defined namespace rather than <limits>, so this measures the language
// step and not which library the two compilers happen to read.
extern "C" int printf(const char *, ...);

namespace lim {
    template <class T> struct Bounds { static T most() { return 127; } };
    template <> struct Bounds<int> { static int most() { return 2147483647; } };
}

template <int N> struct Vec { int a[N]; };

struct Mat {
    int scale;
    Mat(int s) : scale(s) {}
    template <int N> int dot(const Vec<N> &v) const {
        int s = 0;
        for (int i = 0; i < N; ++i) s += v.a[i];
        return s * scale;
    }
    template <int N> int operator*(const Vec<N> &v) const { return dot(v); }
};

int main() {
    printf("%d %d\n", lim::Bounds<int>::most(), (int)lim::Bounds<char>::most());
    Vec<3> v;
    v.a[0] = 1; v.a[1] = 2; v.a[2] = 3;
    Mat m(2);
    printf("%d %d\n", m.dot(v), m * v);
    return 0;
}
