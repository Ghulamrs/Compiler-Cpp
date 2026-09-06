// **A destructible local and a `try` in one function**, which rung 6.4 refused
// because "each is a range in the call-site table and one would have to split
// the other". Splitting is exactly what happens now: the block records which
// of its statements are `try`s, its cleanup region is emitted in the pieces
// between them, and the `try`'s own pad destroys what it found alive when
// nothing matched - with a trailing filter-0 action so phase 2 installs it.
//
// **What is still refused is the overlapping half**: an object inside a
// `try`'s body or a handler's, and a temporary in a thrown expression. There
// the region sits inside the row rather than beside it, and the table's rows
// are written innermost-first for a linear scan rather than sorted by address
// - so the outer cannot be split around the inner without composing the pads.
// Sorting them was tried and broke unwinding through a nested scope; see
// CLAUDE.md.
extern "C" int printf(const char *, ...);

struct S { const char *n; S(const char *s); ~S(); };
S::S(const char *s) : n(s) {}
S::~S() { printf("~%s ", n); }

static void boom() { throw 7; }

// The local is built before the try and destroyed after it, and the handler
// runs in between.
int before() {
    S a("a");
    try { boom(); } catch (int e) { printf("c%d ", e); }
    return 0;
}

// Two locals and two trys interleaved: the region is split at each one.
int interleaved() {
    S b("b");
    try { boom(); } catch (int) { printf("x "); }
    S c("c");
    try { boom(); } catch (int) { printf("y "); }
    return 0;
}

// Nothing matches here, so the exception leaves and the pad destroys `d` on
// the way out - which is the half a plain `return` never exercises.
static void escapes() {
    S d("d");
    try { boom(); } catch (double) { printf("wrong "); }
}

int main() {
    before();
    printf("| ");
    interleaved();
    printf("| ");
    try { escapes(); } catch (int e) { printf("out %d", e); }
    printf("\n");
    return 0;
}
