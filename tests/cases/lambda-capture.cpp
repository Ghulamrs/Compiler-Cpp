// Captures by value, which finish rung 7.6.
//
// **A capture is a member of the closure**, copied from the enclosing function
// where the lambda is written - and reading one inside the body needed no new
// rule at all: `operator()` is a member function, and an unqualified name in
// one already means `this->name`.
//
// The copy is taken *where the lambda is*, which is what `k = 100` below is
// for: `add` goes on answering 6.
//
// **The copying happens on every reading of the lambda, not only the first.**
// 7.1 reads an `auto` initialiser twice and the second reading takes the class
// built by the first - so with the copying beside the building, the object the
// declaration actually kept held whatever was on the stack. It printed
// `-15 -341155503` once.
extern "C" { int printf(const char *, ...); }

struct P {
    int x;
    P(int a);
};

P::P(int a) { x = a; }

int main(void) {
    int k = 5;
    double d = 1.5;
    P p(7);
    auto add = [k](int a) { return a + k; };
    auto both = [k, d](int a) { return a + k + (int)d; };
    auto held = [p]() { return p.x; };          // a class object, copied in
    k = 100;                                    // after the captures were taken
    int total = 0;
    for (int i = 0; i < 3; i++) {
        auto each = [i]() { return i; };        // a fresh closure per turn
        total += each();
    }
    printf("%d %d %d %d\n", add(1), both(1), held(), total);
    return 0;
}
