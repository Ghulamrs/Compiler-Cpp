// The wall SFINAE stops at, and it is a mangling wall rather than a parsing
// one. Itanium spells a specialization's return type from the *pattern*, and
// a pattern holding an expression is spelled as that expression: clang writes
// `_Z4pickIiEN9enable_ifIXeqstT_Li4EEiE4typeES1_`, where `XeqstT_Li4EE` is
// "sizeof(T) == 4". Nothing here can write that.
//
// So it is refused where it is written. The alternative was to mangle from
// the substituted signature instead, which would compile and run correctly
// and produce a name no other compiler agrees with - and a name that links
// with nothing is worse than a refusal that says why.
template <bool B, class T> struct enable_if { };
template <class T> struct enable_if<true, T> { typedef T type; };
int pick(double x) { (void)x; return 2; }
template <class T>
typename enable_if<sizeof(T) == 4, int>::type pick(T x) { (void)x; return 1; }
int main() { return pick(1); }
