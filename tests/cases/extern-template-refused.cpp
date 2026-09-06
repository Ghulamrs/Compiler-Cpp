// `extern template` suppresses an implicit instantiation here and promises one
// elsewhere. Every specialization is emitted where it is used, so the promise
// cannot be kept.
template <class T> struct S { int f() { return 0; } };
extern template struct S<int>;
int main() { S<int> s; return s.f(); }
