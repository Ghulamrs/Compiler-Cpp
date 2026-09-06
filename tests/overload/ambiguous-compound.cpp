// **A member and a non-member compound assignment that match equally well are
// ambiguous**, [over.match.oper]/3 with [over.match.best]: the member's
// implicit object parameter and the non-member's `S &` are the same binding,
// and `long` against `double` for an int argument are both conversions.
// clang refuses it.
extern "C" { int printf(const char *, ...); }

struct S {
    int v;
    int operator*=(long a)   { return 1 + 0 * (int)a; }
};
int operator*=(S &s, double a) { return 2 + 0 * (int)a + 0 * s.v; }

int main(void) {
    S s;
    s.v = 0;
    printf("%d\n", s *= 3);
    return 0;
}
