// `static_cast<T &&>` is what std::move is - a cast and nothing else - and it
// is what lets an lvalue reach a move constructor. Two things are proved here:
// that overload resolution picks the move over the copy, and that the move
// binds to the object itself rather than to a copy of it, which is what the
// -1 left behind in `a` shows.
extern "C" int printf(const char *, ...);

struct S {
    int a;
    S() { a = 0; }
    S(const S &o) { a = o.a; printf("copy %d\n", a); }
    S(S &&o) { a = o.a; o.a = -1; printf("move %d\n", a); }
};

int take(S s) { return s.a; }

int main() {
    S a;
    a.a = 7;

    S b(a);
    S c(static_cast<S &&>(a));
    printf("a=%d b=%d c=%d\n", a.a, b.a, c.a);

    S e;
    e.a = 3;
    printf("took %d\n", take(static_cast<S &&>(e)));
    printf("e=%d\n", e.a);

    // An rvalue reference is a name for the object it was given, so writing
    // through it is seen by whoever still holds the original.
    int i = 5;
    int &&r = static_cast<int &&>(i);
    r = 9;
    printf("i=%d r=%d\n", i, r);
    return 0;
}
