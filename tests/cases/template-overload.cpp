// A name that is both a template and an ordinary function.
//
// [over.match.best]: where a specialization and a non-template are equally
// good, the non-template wins. Deduction is what makes this the ordinary case
// rather than a corner - `pick(3)` deduces T as int, and that specialization
// matches the argument exactly, and so does `int pick(int)`. Without the rule
// every such call is ambiguous.
//
// The specialization that loses is not emitted at all. It had to be made
// before it could be ranked, but a candidate nothing chose gets no body -
// which is what keeps the symbol list level with clang's, where a losing
// candidate was never instantiated in the first place.
extern "C" { int printf(const char *, ...); }

template <class T> T pick(T x) { return x + x; }
int pick(int x) { return x * 10; }

// Here the template is the only one that fits, so it wins on being the only
// viable candidate rather than on any tie.
template <class T> int kind(T x) { (void)x; return 1; }
int kind(const char *s) { (void)s; return 2; }

int main() {
    printf("%d\n", pick(3));
    printf("%.1f\n", pick(1.5));
    printf("%d\n", pick<int>(3));
    printf("%d %d\n", kind(1.5), kind("s"));
    return 0;
}
