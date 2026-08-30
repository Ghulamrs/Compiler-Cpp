// The same tie, in a constructor set.
extern "C" { int printf(const char *, ...); }

class P {
public:
    int tag;
    P(int a, double b) { tag = 1 + 0 * (a + (int)b); }
    P(double a, int b) { tag = 2 + 0 * ((int)a + b); }
};

int main(void) {
    P p(1, 1);
    printf("%d\n", p.tag);
    return 0;
}
