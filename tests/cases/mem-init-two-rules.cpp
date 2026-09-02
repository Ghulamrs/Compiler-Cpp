// **One initialiser list, two rules, and the empty pair is what tells them
// apart.** `: m(1, 2)` constructs the member - [class.base.init]/7 hands the
// arguments to overload resolution - and `: p()` value-initialises it,
// [dcl.init]/8, which for a class with no constructor is zeroing and for one
// with a user-provided default constructor is that constructor and no
// zeroing. member-default-init.cpp pins the first rule and
// mem-init-value-init.cpp the second; this is the seam, where a single list
// asks for both and every entry has to reach the right one.
//
// It is a case because the seam is where the two mends met: construction was
// given the whole list, empty pairs included, and `: p()` on a plain struct
// was refused with "'p' takes one value here, given 0" - a form ordinary C++
// writes daily, refused by the arity of a member nobody had asked about.
// The arity question belongs to the entries that are neither constructed nor
// value-initialised, which is what mem-init-arity-refused.cpp pins.
//
// A multi-argument entry is here for the same reason: the check that moved
// was the one that had allowed a member exactly one value, so `: m(1, 2)` is
// what proves it moved rather than went.
extern "C" int printf(const char *, ...);

struct M {
    int a;
    int b;
    M(int x, int y) : a(x), b(y) { printf("M2 "); }
    M() : a(7), b(8) { printf("M0 "); }
    ~M() { printf("~M "); }
};
struct P { int v; int w; };
struct Q { int q; Q() : q(3) { printf("Q "); } };
struct V { int a; virtual int f() { return a; } };

struct S {
    M m;
    P p;
    int k;
    double d;
    M m2;
    Q q;
    V v;
    int arr[2];
    S() : m(1, 2), p(), k(), d(), m2(), q(), v(), arr() { printf("S "); }
};

int main() {
    // The frame is dirtied first: on a clean stack a member nobody zeroed
    // reads as zero and the fault is invisible.
    volatile char pad[512];
    for (int i = 0; i < 512; i++) pad[i] = 0x55;
    {
        S s;
        printf("| %d %d %d %d %d %d %d %d %d %d %d %d\n", s.m.a, s.m.b,
               s.p.v, s.p.w, s.k, (int)s.d, s.m2.a, s.m2.b, s.q.q,
               s.v.f(), s.arr[0], s.arr[1]);
    }
    printf("\n");
    return 0;
}
