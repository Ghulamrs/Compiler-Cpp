// [class.base.init]/8: a member the mem-initialiser list does not name is
// default-initialised, and for a class type that means its default
// constructor runs. The implicit default constructor did this from the day
// it was written; a constructor the programmer wrote did not - S1 here left
// `m` holding whatever was on the stack, printed 1 where clang prints 3, and
// then ran `~M` on it from the destructor the compiler wrote. One silent
// wrong answer, so every shape near it is pinned, with the constructors and
// destructors printing so that the *order* is checked as well as the values:
//
//   M    a user-provided default constructor      S1, and beside a scalar
//                                                 the list names in S5
//   N    every parameter defaulted, [class.ctor]/5 S2, and named with an
//                                                 argument in S6 - `: n(9)`
//                                                 constructs now, where it
//                                                 was refused as an
//                                                 assignment of an int
//   I    the implicit, non-trivial constructor    S2 and, nested twice, S3
//   M[2] an array member                          S4
//   B    a base beside a member, unnamed and named S7, S9
//   S8   a constructor that is not the default one, same rule
//   C, D a written copy constructor: `: m(o.m)` reaches M's copy
//        constructor, and one that names nothing default-constructs
//
// Every line is what clang prints, destructors included. What is *not* here:
// a class-typed member with its own initialiser, `M m = M(2);`, which is
// still assigned from the temporary rather than built from it - CLAUDE.md
// records that door and what each compiler prints through it.
extern "C" int printf(const char *, ...);
struct M { int v; M() : v(3) { printf("M "); }
           M(const M &o) : v(o.v + 100) { printf("Mcopy "); }
           ~M() { printf("~M "); } };
struct N { int w; N(int a = 5) : w(a) { printf("N "); } };
struct I { M m; };
struct Nest { I i; N n; };
struct B { B() { printf("B "); } };
struct S1 { M m; S1() { printf("S1 "); } };
struct S2 { M a; N b; I c; S2() { printf("S2 "); } };
struct S3 { Nest x; S3() { printf("S3 "); } };
struct S4 { M arr[2]; S4() { printf("S4 "); } };
struct S5 { int k; M m; S5() : k(7) { printf("S5 "); } };
struct S6 { N n; M m; S6() : n(9) { printf("S6 "); } };
struct S7 : B { M m; S7() { printf("S7 "); } };
struct S8 { M m; int q; S8(int a) : q(a) { printf("S8 "); } };
struct S9 : B { M m; S9() : B() { printf("S9 "); } };
struct C { M m; C() { printf("C "); } C(const C &o) : m(o.m) { printf("Ccopy "); } };
struct D { M m; D() { printf("D "); } D(const D &o) { (void)o; printf("Dcopy "); } };
int main() {
    { S1 s; printf("| %d\n", s.m.v); }
    { S2 s; printf("| %d %d %d\n", s.a.v, s.b.w, s.c.m.v); }
    { S3 s; printf("| %d %d\n", s.x.i.m.v, s.x.n.w); }
    { S4 s; printf("| %d %d\n", s.arr[0].v, s.arr[1].v); }
    { S5 s; printf("| %d %d\n", s.k, s.m.v); }
    { S6 s; printf("| %d %d\n", s.n.w, s.m.v); }
    { S7 s; printf("| %d\n", s.m.v); }
    { S8 s(4); printf("| %d %d\n", s.m.v, s.q); }
    { S9 s; printf("| %d\n", s.m.v); }
    { C c; C d(c); printf("| %d %d\n", c.m.v, d.m.v); }
    { D c; D d(c); printf("| %d %d\n", c.m.v, d.m.v); }
    printf("\n");
    return 0;
}
