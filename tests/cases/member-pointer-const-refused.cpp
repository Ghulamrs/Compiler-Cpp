// [expr.mptr.oper]/6: `.*` on a const object yields a const lvalue. cxx1 gave
// the member's own type back, so a `const S` could be written through a pointer
// to member with no diagnostic - the same hole V-09 was on the ordinary member
// path, one operator over, and the refusal comes from the same place: the
// assignment finds a `const int` on its left. Reading through a const object
// still works, `->*` through a pointer-to-const too.
extern "C" int printf(const char *, ...);
struct S { int n; };
int main() { const S s = {}; int S::*p = &S::n; (s.*p) = 3; printf("%d\n", s.n); return 0; }
