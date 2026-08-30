// The two enclosing functions whose names are not shaped like the rest, both
// of which a real program reaches for immediately.
//
// `main` has no mangled name to take apart, so Itanium writes it as a plain
// length-and-letters component - `_ZZ4mainEN1B3getEv`, no `_Z` to strip - and
// the Microsoft ABI writes `?main@@9`, the 9 being its way of saying a name
// carries no type information. A `static` function needs nothing special: its
// Itanium name is `_ZL4stati` and the L simply comes along inside the wrapper.
// Both measured against clang, and `tests/names.sh` checks them on every run.
extern "C" { int printf(const char *, ...); }

static int stat_(int k) {
    struct A { int v; int get() { return v + 1; } };
    A a;
    a.v = k;
    return a.get();
}

int main(void) {
    struct B { int v; int get() { return v + 2; } };
    B b;
    b.v = 10;
    printf("%d %d\n", b.get(), stat_(20));
    return 0;
}
