// **A class-typed member with its own initialiser is built from it, not handed
// its bytes.** `struct E { M m = M(2); };` used to construct a temporary, move
// its bytes into storage nothing had constructed, and then destroy the
// temporary - one constructor and two destructors, and for a class that owns
// anything, the member left holding what the temporary's destructor had given
// back. [dcl.init]/17 makes it copy-initialisation, so it goes through the same
// overload resolution `: m(x)` does and reaches the copy or move constructor.
//
// **What this prints has to survive copy elision, because C++11 permits it and
// the two compilers differ**: clang builds `M(2)` straight into the member at
// -O0 and cxx1 makes the copy the standard also allows - see
// docs/CONFORMANCE.md. So this counts *live objects*, which is 0 either way,
// rather than constructor calls, which are 1 there and 2 here. The bug it
// pins showed as -1: one construction and two destructions.
extern "C" int printf(const char *, ...);

int live = 0;

struct M {
    int v;
    M(int x) : v(x) { live++; }
    M(const M &o) : v(o.v) { live++; }
    ~M() { live--; }
};

struct FromTemporary { M m = M(2); int k = 5; };
struct FromValue     { M m = 7; };                  // a converting constructor
struct Written       { M m = M(3); Written() : m(9) {} };   // the list wins
struct Implicit      { M m = M(4); };               // no constructor written

int main() {
    { FromTemporary a; printf("%d %d %d\n", a.m.v, a.k, live); }
    { FromValue b;     printf("%d %d\n", b.m.v, live); }
    { Written c;       printf("%d %d\n", c.m.v, live); }
    { Implicit d;      printf("%d %d\n", d.m.v, live); }
    printf("%d\n", live);
    return 0;
}
