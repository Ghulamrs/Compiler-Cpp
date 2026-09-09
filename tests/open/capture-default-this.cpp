// [expr.prim.lambda]/8: `[=]` and `[&]` in a member function capture `this`
// implicitly, so the body may name a member. cxx1 answers "'n' was not
// declared". [this] written out works; the default captures do not.
extern "C" int printf(const char *, ...);
struct S { int n; S() : n(4) {} int f() { auto g = [=]() { return n; }; return g(); } };
int main() { S s; printf("%d\n", s.f()); return 0; }
