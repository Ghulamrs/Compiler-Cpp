// An enumeration written inside a class or a namespace, which is the ordinary
// way C++11 spells a small set of named constants before `enum class` is
// reached for. Three things had to be true at once and each was missing.
//
// The declaration itself: `enum Kind { ... };` in a class body declares a type
// and no member, exactly as a nested class does - and it is told apart by the
// keyword, because the type it answers with is `int` and there is nothing in
// that to recognise. Without it the member loop said "this declares nothing".
//
// The name: the tag and every enumerator take the enclosing class's tag or the
// namespace prefix, so `C::Kind` and `n::Kind` do not collide with a global of
// the same name and two classes may each write `Red`.
//
// And the lookup, which is a scope walk rather than one flat map: an
// enumerator named unqualified inside a member function, qualified from
// outside, and the same for a namespace.
extern "C" int printf(const char *, ...);

namespace n {
    enum Level { Low, Mid = 5, High };
    int twice(Level l) { return l * 2; }
}

struct C {
    enum Kind { Red, Green, Blue };
    Kind k;
    // Unqualified inside a member, which is the walk through classStack_.
    int shifted() const { return k + Green; }
};

struct D {
    // A second class may write the same enumerator names - they are
    // `C::Red` and `D::Red` and nothing about them is shared.
    enum Kind { Red = 100, Green };
    static int base() { return Red; }
};

int main() {
    C c;
    c.k = C::Blue;
    printf("%d %d\n", c.k, c.shifted());
    printf("%d %d\n", D::base(), D::Green);
    printf("%d %d\n", n::Mid, n::twice(n::High));
    // An enumerator is a constant expression, so it may be an array length.
    int a[C::Blue + 1];
    a[C::Red] = 7;
    printf("%d %d\n", (int)(sizeof a / sizeof a[0]), a[0]);
    return 0;
}
