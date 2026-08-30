// Constructor overloading. A constructor is chosen by the same ranking as any
// other function - [over.match.ctor] hands the candidate set to the same
// machinery - so what this checks is that a constructor really does go through
// it, arity, promotions and all.
extern "C" { int printf(const char *, ...); }

class P {
public:
    int tag;
    P()                  { tag = 1; }
    P(int a)             { tag = 2 + 0 * a; }
    P(double a)          { tag = 3 + 0 * (int)a; }
    P(int a, int b)      { tag = 4 + 0 * (a + b); }
    P(const char *s)     { tag = 5 + 0 * (int)s[0]; }
    P(char *s, int n)    { tag = 6 + 0 * ((int)s[0] + n); }
};

int main(void) {
    char buf[4];
    char c;
    float fl;
    short sh;
    buf[0] = 0; c = 'q'; fl = 1.0f; sh = 7;

    P a;
    P b(1);
    P c2(1.5);
    P d(1, 2);
    P e("x");
    P f(c);        // char -> int by promotion, which beats double
    P g(fl);       // float -> double by promotion
    P h(sh);       // short -> int by promotion
    P i(buf, 1);

    printf("%d %d %d %d %d %d %d %d %d\n",
           a.tag, b.tag, c2.tag, d.tag, e.tag, f.tag, g.tag, h.tag, i.tag);
    return 0;
}
