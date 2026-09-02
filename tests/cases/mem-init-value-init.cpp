// **`: m()` in an initialiser list value-initialises the member.**
// [class.base.init] hands the empty pair to [dcl.init]/8, so a scalar member
// zeroes, an array of scalars zeroes, a class member with no constructor
// zeroes leaf by leaf, a class member whose default constructor nobody wrote
// zeroes and then runs it, and a class member with a user-provided one runs
// that alone - the same rule new-value-init.cpp, class-temporary.cpp and
// value-init-implicit-ctor.cpp pin for the other places `()` can say it.
// This was refused outright before - "'p' takes one value here, given 0" -
// for a form ordinary C++ writes daily. The zeroing and the constructor call
// are both on the member itself, not a temporary's bytes assigned over it.
extern "C" int printf(const char *, ...);

struct P { int v; int w; };
struct V { int a; virtual int f() { return a; } };
struct Q { int q; Q() : q(3) {} };
struct W {
    P p;
    int k;
    double d;
    int arr[3];
    V v;
    Q q;
    W() : p(), k(), d(), arr(), v(), q() {}
};

int main() {
    volatile char j[512];
    for (int i = 0; i < 512; i++) j[i] = 0x55;
    W w;
    printf("%d %d %d %d %d %d %d %d %d\n", w.p.v, w.p.w, w.k, (int)w.d,
           w.arr[0], w.arr[1], w.arr[2], w.v.f(), w.q.q);
    return 0;
}
