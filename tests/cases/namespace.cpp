// namespace and using namespace.
//
// **A namespace is not a Type**, and that is the one structural fact the rest
// of this follows from. A nested class is a member of an enclosing class, so
// `Outer::Inner` has an `enclosing()` to walk and both manglers walk it. A
// namespace has no such object, so `N::S` carries its scope in its *tag* and
// the manglers split the tag instead - which is why a class in a namespace
// needs a flag of its own to be told from a *local* class, whose tag has a
// "::" in it too and is one name rather than a scope.
//
// Unqualified lookup is a search: the enclosing namespaces innermost-out, then
// whatever `using namespace` has opened, then the name as written. And an
// operand's own namespace is searched for an operator, which is the part of
// argument-dependent lookup that `operator+` written beside its class needs.

extern "C" int printf(const char *, ...);

namespace N {
    int v;
    int f(void) { return 1; }
    int f(int a) { return a * 10; }        // overloads, inside a namespace

    struct S {
        int x;
        S(int a);
        int get(void) const;               // both defined below, still in N
        static int made;
    };
    S::S(int a) : x(a) {}
    int S::get(void) const { return x * 2; }
    int S::made = 4;

    struct V { int a; };
    V operator+(V l, V r) { V o; o.a = l.a + r.a; return o; }

    int twice(S s) { return s.get(); }     // takes N::S unqualified

    int useV(void) { return v + f() + f(3); }   // finds N::v and N::f
}

namespace N {                              // reopened
    int later(void) { return v * 2; }
}

namespace N { namespace M {
    struct T { int y; };
    int g(T a, S b) { return a.y + b.x; }  // S from the enclosing namespace
} }

int f(void) { return 99; }                 // shadowed inside N, not by it

namespace Q {
    struct S { int x; };                   // a second S, and not the same one
    int size(void) { return sizeof(S); }
}

int outside(N::S a, N::M::T b) { return a.x + b.y; }

int adl(void) {
    N::S s(5);
    return twice(s);                       // N::twice, through the argument
}

int op(void) {
    N::V a; a.a = 2;
    N::V b; b.a = 3;
    return (a + b).a;                      // N::operator+, likewise
}

int opened(void) {
    using namespace N;                     // and only to the end of this block
    S s(6);
    return s.get() + f(2);
}

int shadowed(void) {
    int inner;
    {
        using namespace N;
        inner = later();                   // N::later, opened only here
    }
    return inner + f();                    // and the global f afterwards
}

using namespace N;

int main(void) {
    N::v = 3;
    S atFileScope(7);                      // N::S, through the directive above
    N::M::T t;
    t.y = 8;
    N::S four(4);
    printf("%d %d %d %d %d\n",
           N::useV(), N::later(), N::S::made, N::M::g(t, four), outside(four, t));
    printf("%d %d %d %d\n", adl(), op(), opened(), shadowed());
    // `f()` on its own is *not* written here. The directive above makes both
    // `::f` and `N::f` candidates, which is an ambiguity clang reports and
    // this compiler does not - see docs/CONFORMANCE.md. `shadowed()` above
    // does the same comparison from where no directive is open.
    printf("%d %d\n", atFileScope.get(), Q::size());
    return 0;
}
