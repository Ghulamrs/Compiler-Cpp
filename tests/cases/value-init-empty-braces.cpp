// **`{}` is not an empty list, it is value-initialisation** - [dcl.init]/11
// sends it to /8, the paragraph `T()` already followed here. Every shape below
// was measured against clang, g++ and cl before it was written down.
//
// The two halves of /8 are what the class cases are for: a constructor somebody
// wrote is the whole of the initialisation, so `User` leaves `b` alone; an
// implicit one gets the object zeroed first, so `Implied` sets `b` to 0. That
// difference is why `{}` is not simply "call the default constructor".
//
// `Held` reaches through a member with its own initialiser, which C++11 makes
// no aggregate: it has an implicit constructor, and value-initialising runs it.
extern "C" int printf(const char *, ...);

struct Plain   { int a; int b; };
struct Nested  { Plain p; int k; };
struct User    { int a; int b; User() { a = 1; } };
struct Implied { int a; int b; };
struct Held    { int a = 7; int b; };

Plain atFileScope = {};
int scalarAtFileScope = {};
const Plain constAtFileScope = {};

int fromStatic() {
    static Plain kept = {};
    return kept.a + kept.b;
}

int main() {
    // Whatever the frame held before, so a zero below is this compiler's and
    // not the stack's - the same guard const-uninitialised-ok.cpp uses.
    volatile char pad[256];
    for (int i = 0; i < 256; i++) pad[i] = 0x55;

    int n = {};
    int direct{};
    Plain p = {};
    Plain q{};
    int a[3] = {};
    Nested nest = {};
    Nested inner = {{}, 4};
    User u = {};
    Implied im = {};
    Held h = {};
    const Plain cp = {};

    Plain *heap = new Plain{};
    int *one = new int{};
    int *many = new int[3]{};

    printf("%d %d %d %d %d %d %d %d %d %d\n", n, direct, p.a + p.b, q.a + q.b,
           a[0] + a[1] + a[2], nest.p.a + nest.k, inner.p.a + inner.k,
           u.a, im.a + im.b, h.a + h.b);
    printf("%d %d %d %d %d %d %d\n", cp.a, heap->a + heap->b, *one,
           many[0] + many[1] + many[2], atFileScope.a + scalarAtFileScope,
           constAtFileScope.b, fromStatic());
    return 0;
}
