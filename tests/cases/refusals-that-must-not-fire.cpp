// **The legal side of six refusals**, each one a shape that looks like the rule
// next to it and is not. They were written while adding those rules, because a
// refusal that is one case too wide is the failure mode of this kind of work -
// and each of the six had a boundary worth measuring against clang:
//
//   a member hiding a base's        [class.mem]/1 is about one class's own
//   a const member with `= 5`       [class.ctor]/5 asks only for an initialiser
//   `auto a = 1, b = 2;`            same deduced type, so [dcl.spec.auto] is met
//   `auto *p = &a, q = 5;`          **`auto` is `int` in both** - the declarators
//                                   differ and the deduced type does not, which
//                                   is what that rule compares
//   reading through a const object  `.*` and `->*` yield a const lvalue, and
//                                   reading one is what a const object is for
//   an operator over a class, and   [over.oper]/6 wants one parameter of class
//   another over an enumeration     or enumeration type, and either will do
//
// Every value here agrees with clang.
extern "C" int printf(const char *, ...);
struct Base { int a; };
struct Derived : Base { int a; };            // hides the base's, which is legal
struct WithInit { const int c = 5; int n; };
struct Q { int v; Q(int x) : v(x) {} };
enum Colour { Red, Blue };
bool operator==(const Q &l, const Q &r) { return l.v == r.v; }
int operator+(Colour c, int k) { return (int)c + k; }
struct S { int n; int twice() const { return n * 2; } };
int main() {
    Derived d; d.a = 1; Base &asBase = d; asBase.a = 2;
    WithInit w;
    auto a = 1, b = 2;                        // same type, legal
    auto *p = &a, q = 5;                      // auto is int in both
    const S s = {3};
    int S::*m = &S::n;
    int read = s.*m;                          // reading through a const object
    const S *ps = &s;
    int viaPtr = ps->*m;
    S plain = {4};
    (plain.*m) = 9;
    printf("%d %d %d %d %d %d %d %d %d\n", d.a, asBase.a, w.c, a + b, *p + q,
           read, viaPtr, plain.n, (Red + 1) + (Q(1) == Q(1) ? 1 : 0));
    return 0;
}
