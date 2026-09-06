// **A compound assignment ranks its member and non-member candidates
// together**, [over.match.oper]/3, exactly as `a + b` does. The member takes
// the int and the non-member the double; neither is preferred for being what
// it is, and `s += 'c'` promotes to the int one.
extern "C" { int printf(const char *, ...); }

struct S {
    int v;
    int operator+=(int a)   { return 1 + 0 * a; }
};
int operator+=(S &s, double a) { return 2 + 0 * (int)a + 0 * s.v; }

int main(void) {
    S s;
    s.v = 0;
    printf("%d %d %d\n", s += 3, s += 2.5, s += 'c');
    return 0;
}
