// `final` on a class head forbids deriving from it, which is a check made at
// every later derivation rather than anything about this one.
struct B { virtual int f() { return 0; } virtual ~B() {} };
struct D final : B { int f() { return 0; } };
int main() { D d; return d.f(); }
