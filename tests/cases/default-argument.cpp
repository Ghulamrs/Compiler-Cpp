// Default arguments. A default is kept as a *place in the token stream* and
// read again at every call that leaves the argument out, which is what
// [dcl.fct.default]/9 asks for - it is evaluated afresh each time, so one
// parsed tree could not have been handed to two call sites anyway.
//
// The caller's locals are put aside while it is read. A default at namespace
// scope may name globals, enumerators and static members and may not name a
// local or another parameter - so hiding them is what the declaration's scope
// is from here, and it stops a local of the same name in the *calling*
// function from quietly capturing the default. `shadow` below is that case.
extern "C" { int printf(const char *, ...); }

int base = 100;

int f(int a, int b = 3);            // the default on the declaration...
int f(int a, int b) { return a + b; }   // ...and not repeated on the definition

int usesGlobal(int a = base) { return a; }

struct S {
    int x;
    S(int a, int b = 5);
    int m(int n = 7) const;
};

S::S(int a, int b) { x = a * 10 + b; }
int S::m(int n) const { return x + n; }

int shadow(void) {
    int base = 1;                   // must NOT be what usesGlobal() sees
    return usesGlobal() + base;
}

int main(void) {
    S a(1);
    S b(1, 2);
    printf("%d %d %d %d %d %d\n",
           f(1), f(1, 9), a.x, b.x, a.m(), shadow());
    return 0;
}
