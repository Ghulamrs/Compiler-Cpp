// A partial specialization with a parameter its arguments never mention.
// Nothing in any argument list could ever work it out, so the specialization
// could never be chosen - refused where it is written rather than left as one
// that silently never applies.
template <class T> struct Q { int f() { return 0; } };
template <class T, class U> struct Q<T> { int f() { return 1; } };
int main() { Q<int> q; return q.f(); }
