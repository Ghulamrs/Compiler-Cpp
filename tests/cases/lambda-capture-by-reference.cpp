// `[&]`, `[&x]`, and a mixed list - the last of the captures.
//
// **It is a reference data member and nothing else**, which is why it waited:
// the closure holds one per by-reference capture, laid out as a pointer and
// bound where the lambda is written. The refusal it replaces named that as the
// blocker rather than calling the feature unwritten, and it was the right
// blocker - once reference members existed this was mostly the same code as
// capturing by value with `bindReference` in place of an assignment.
//
// `mixed` is the one to read: `k` is copied where it stood and `m` is followed,
// so after `k = 9; m = 2;` it answers with the old k and the new m.
extern "C" { int printf(const char *, ...); }

int main(void) {
    int k = 5;
    int m = 1;
    double d = 2.5;
    auto named = [&k]() { return k; };
    auto all = [&]() { return k + m; };
    auto mixed = [k, &m]() { return k * 100 + m; };
    auto writes = [&]() { m = 7; };
    auto wider = [&]() { return (int)(d * 2); };

    k = 9;
    m = 2;
    d = 3.0;
    printf("%d %d %d %d ", named(), all(), mixed(), wider());
    writes();                    // writes through the capture, into m
    printf("%d\n", m);
    return 0;
}
