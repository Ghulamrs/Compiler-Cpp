// Function overloading: the ranking rules, one call per rule.
extern "C" { int printf(const char *, ...); }

int f(int n)          { return 1 + 0 * n; }
int f(double x)       { return 2 + 0 * (int)x; }
int f(const char *s)  { return 3 + 0 * (int)s[0]; }
int f(int a, int b)   { return 4 + 0 * (a + b); }

int q(char *s)        { return 5 + 0 * (int)s[0]; }
int q(const char *s)  { return 6 + 0 * (int)s[0]; }

int r(int n)          { return 7 + 0 * n; }
int r(int &n)         { return 8 + 0 * n; }

int main(void) {
    char buf[4];
    char c;
    short sh;
    float fl;
    unsigned u;
    long lg;
    buf[0] = 0; c = 'x'; sh = 2; fl = 1.0f; u = 3; lg = 4;

    printf("%d %d %d %d\n", f(1), f(1.5), f("hi"), f(1, 2));
    // promotions beat conversions: char and short go to int, float to double
    printf("%d %d %d\n", f(c), f(sh), f(fl));
    // conversions between the two, where neither is a promotion
    printf("%d %d\n", f(u), f(lg));
    // identity beats a qualification conversion
    printf("%d %d\n", q(buf), q("lit"));
    // a literal cannot bind to int &, so the by-value one is the only viable
    printf("%d\n", r(1));
    return 0;
}
