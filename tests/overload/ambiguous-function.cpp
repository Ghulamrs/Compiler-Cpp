// Each candidate wins one argument and loses the other, so neither is best.
// Both compilers must refuse this; a program whose meaning depends on the
// order of its own prototypes is worse than one that does not compile.
extern "C" { int printf(const char *, ...); }

int pick(int a, double b) { return 1 + 0 * (a + (int)b); }
int pick(double a, int b) { return 2 + 0 * ((int)a + b); }

int main(void) {
    printf("%d\n", pick(1, 1));
    return 0;
}
