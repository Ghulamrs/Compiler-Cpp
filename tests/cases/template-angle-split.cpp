// The `>` that closes Y's argument list here is the front half of a `>>`,
// and between taking that half and taking the other, cxx1 instantiates
// Y<char> - whose member is ill-formed for char - so a substitution failure
// is thrown while `angleSplit_` marks a half-taken token. `Trial` restores
// that mark along with the rest of its state; before it did, the mark leaked,
// and the *second* call's replay of the same declaration met its own `>>`
// already "half taken", swallowed both halves at the inner list, and refused
// a program it had just accepted - this one printed 2 and then failed to
// compile its second call. The typedef sits above main on purpose: any `>>`
// parsed between the two calls overwrites the single stale mark and hides
// the bug, which is what defeated the first attempts to reproduce it.
//
// clang prints the same two lines without ever instantiating Y<char>: a
// template argument needs no complete type, so its deduction never sees the
// failure cxx1 recovers from. If eager instantiation here is ever deferred,
// this case's first call stops exercising the window and needs a new shape.
extern "C" int printf(const char *, ...);
template <class U> struct Y { U m[sizeof(U) - 5]; };
template <class W> struct X { int w; };
template <class T> int f(T a, X<Y<T>> *b) { return 1; }
int f(char a, int p) { return 2; }
typedef X<Y<double>> XP;
int main() {
    printf("%d\n", f('x', 3));
    XP *p = 0;
    printf("%d\n", f(1.5, p));
    return 0;
}
