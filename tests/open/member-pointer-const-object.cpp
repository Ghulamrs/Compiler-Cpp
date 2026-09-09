// [expr.mptr.oper]/6: `.*` on a const object yields a const lvalue. cxx1
// yields a non-const one and writes through the const object, silently.
extern "C" int printf(const char *, ...);
struct S { int n; };
int main() { const S s = {}; int S::*p = &S::n; (s.*p) = 3; printf("%d\n", s.n); return 0; }
