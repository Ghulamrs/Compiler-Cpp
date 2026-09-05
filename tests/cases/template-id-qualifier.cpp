// **A class template-id as a qualifier in an expression**, `CNeeds<(N==3)>::
// check()` - [temp.names]. The template-id is instantiated and a *static*
// member of it is reached with no object, keyed on the tag the same way
// `C::check()` is. Only a static member is reachable this way; a non-static one
// needs an object.
//
// This is the compile-time-check idiom: CNeeds<true> is defined and
// CNeeds<false> is only declared, so `CNeeds<(N==3)>::check()` is well-formed
// exactly when N is 3 - a static_assert written before there was one. Measured
// against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

template <bool Ok> struct Needs;
template <> struct Needs<true> { static int tag() { return 7; } };

template <int Which> struct Reg { static const int value = Which * 10; };

template <int N> struct Vec {
    double a[N];
    Vec(double x, double y, double z) {
        Needs<(N == 3)>::tag();
        a[0] = x; a[1] = y; a[2] = z;
    }
    static int label() { return Needs<(N == 3)>::tag(); }
};

int main() {
    Vec<3> v(1, 2, 3);
    printf("%.0f %.0f %.0f\n", v.a[0], v.a[1], v.a[2]);
    printf("%d %d\n", Vec<3>::label(), Reg<5>::value);
    return 0;
}
