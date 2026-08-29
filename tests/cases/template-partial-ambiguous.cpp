// Two partial specializations fit and neither is more specialized than the
// other. [temp.class.order] orders them by matching each pattern against the
// other, and here neither match succeeds - so there is no answer and the
// program is refused, which is what clang does too.
//
// The rule to get right is that "was not beaten" is not the same as "beats".
// Asking only whether the winner was beaten lets this through and picks
// whichever was written first, which is the silent kind of wrong.
template <class A, class B> struct P { int f() { return 0; } };
template <class A> struct P<A, int> { int f() { return 1; } };
template <class B> struct P<int, B> { int f() { return 2; } };
int main() { P<int, int> p; return p.f(); }
