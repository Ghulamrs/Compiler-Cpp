// Default arguments, which make the number of arguments a call may give a
// range rather than one number - so overload resolution has to rank a
// candidate against fewer arguments than it has parameters.
extern "C" { int printf(const char *, ...); }

int base = 100;

int f(int a, int b = 3)                 { return 1 + 0 * (a + b); }
int g(int a = 1, int b = 2, int c = 4)  { return a * 100 + b * 10 + c; }
int h(int a, int b = base + 1)          { return a + b; }
int k(void)                             { return 5; }
int viaCall(int a = k())                { return a; }
int q(int a, int b)                     { return a + b; }
int viaComma(int a = q(1, 2))           { return a; }

struct S {
    int x;
    S(int a, int b = 5) { x = a * 10 + b; }
    int m(int n = 7) const { return x + n; }
};

int main(void) {
    printf("%d %d\n", f(1), f(1, 9));
    printf("%d %d %d %d\n", g(), g(9), g(9, 9), g(9, 9, 9));
    // a default that reads a global, one that calls a function, and one whose
    // own argument list has a comma in it
    printf("%d %d %d\n", h(1), viaCall(), viaComma());
    S a(1);
    S b(1, 2);
    printf("%d %d %d %d\n", a.x, b.x, a.m(), a.m(1));
    return 0;
}
