// The call operator, whose candidate set is the class's own - [over.call] gives
// it no non-member form - and which has no fixed arity, so the ranking is the
// ordinary member one with nothing special about it.
extern "C" { int printf(const char *, ...); }

class F {
public:
    int base;
    F(int n) { base = n; }
    int operator()() const             { return 1 + 0 * base; }
    int operator()(int a) const        { return 2 + 0 * (base + a); }
    int operator()(double a) const     { return 3 + 0 * (base + (int)a); }
    int operator()(int a, int b) const { return 4 + 0 * (base + a + b); }
    int operator()(const char *s) const { return 5 + 0 * (base + (int)s[0]); }
};

class G {
public:
    int operator()(int a)       { return 6 + 0 * a; }
    int operator()(int a) const { return 7 + 0 * a; }
};

int main(void) {
    F f(10);
    G g;
    const G h;
    char c;
    short s;
    float fl;
    c = 'k'; s = 2; fl = 1.0f;
    printf("%d %d %d %d %d\n", f(), f(1), f(1.5), f(1, 2), f("x"));
    printf("%d %d %d\n", f(c), f(s), f(fl));
    // the object's const chooses between G's two
    printf("%d %d\n", g(1), h(1));
    return 0;
}
