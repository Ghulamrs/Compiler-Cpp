// **A declaration in an `if` or `while` condition**, which CLAUDE.md had
// deferred since rung 1 as "a change to how `If` is built rather than an
// addition to it" - and that was the right description. The name is in scope
// across both arms, so the statement goes inside a block of its own rather
// than the condition becoming a bigger expression.
//
// The two forms are not the same rule, and reading them as one is the way to
// get this wrong. [stmt.select]/2 evaluates the condition of an `if` once, so
// the declaration is hoisted in front of the `If`. [stmt.iter]/2 creates and
// destroys the loop variable **on every turn**, so hoisting the initialiser
// out of a `while` would evaluate it once and then loop for ever on the value
// it got. There the slot is declared once - it is one object as far as the
// frame is concerned - and the initialisation moves into the condition, which
// is what `pull()` below is here to prove.
extern "C" int printf(const char *, ...);

struct Node { int v; Node *next; };
static Node c = { 3, 0 };
static Node b = { 2, &c };
static Node a = { 1, &b };
Node *head() { return &a; }

// At file scope rather than a function-static, which would drag in a separate
// difference: cxx1 spells a local static `_pull.box` where Itanium writes
// `_ZZ4pullvE3box`, and that has nothing to do with conditions.
int step = 0;
int box[3] = { 7, 8, 0 };
int *pull() { return box[step] ? &box[step++] : 0; }

// A class with a destructor, so the object built by the condition is destroyed
// after the whole statement rather than at the end of an arm.
// Defined out of line, because on x86_64-linux clang emits only the C2 form of
// an *inline* constructor where cxx1 emits C1 and C2 - a recorded divergence
// that has nothing to do with conditions, and one this case need not carry.
struct Guard {
    int v;
    Guard(int n);
    ~Guard();
    operator bool() const { return v != 0; }
};
Guard::Guard(int n) : v(n) { printf("ctor %d\n", n); }
Guard::~Guard() { printf("dtor %d\n", v); }
Guard make(int n) { return Guard(n); }

int main() {
    if (Node *n = head()) printf("if %d\n", n->v); else printf("if none\n");
    if (Node *n = 0) printf("bad %d\n", n->v); else printf("if null\n");
    // The name is in scope in both arms - the else reads it too.
    if (int k = 5) printf("then %d\n", k); else printf("else %d\n", k);
    if (int k = 0) printf("then %d\n", k); else printf("else %d\n", k);

    // Each turn re-evaluates the initialiser. Hoisted, this never ends.
    while (int *p = pull()) printf("while %d\n", *p);

    // A class in an `if` condition: one construction, and the destructor runs
    // after the arm has been taken, in both directions.
    if (Guard g = make(5)) printf("held %d\n", g.v);
    printf("after\n");
    if (Guard g = make(0)) printf("bad\n"); else printf("zero %d\n", g.v);

    // The declared name belongs to the condition's own scope and shadows.
    int k = 100;
    if (int k = 2) printf("inner %d\n", k);
    printf("outer %d\n", k);
    return 0;
}
