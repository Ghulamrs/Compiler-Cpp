// **An enumerator of a class template specialization is reached like a static
// member of one**, and was not: the branch that resolves `S<int>::N` asked for
// a static member function and then a static data member, and never for an
// enumerator - so a plain class answered here and a template-id qualifier did
// not, named enum and anonymous alike.
//
// Found by the widened sweep, where it looked like a gap about anonymous
// enums and was not: `enum E { N };` failed the same way.
extern "C" int printf(const char *, ...);

template <class T> struct Anon { enum { N = 2 }; };
template <class T> struct Named { enum E { M = 5 }; };
template <int K> struct ByValue { enum { Twice = K * 2 }; };

struct Plain { enum { P = 7 }; };

int main() {
    int a[Anon<int>::N];
    a[0] = Anon<int>::N;
    printf("%d %d %d %d %d\n", Anon<int>::N, Named<char>::M,
           ByValue<3>::Twice, Plain::P, a[0]);
    return 0;
}
