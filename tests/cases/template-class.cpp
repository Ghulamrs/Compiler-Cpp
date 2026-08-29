// Class templates with their arguments written out - rung 5.4.
//
// The class is made by replaying `struct Box { ... };` with the arguments
// bound, exactly as a function specialization replays its definition. The
// only thing the class path had to be told is what tag to take: everything
// else fell out of nested classes, because tag() was already an arbitrary
// qualified string with localName() and enclosing() beside it, and both
// manglers already walked a scope.
//
// `Holder` inside `Holder`'s own body is the *injected class name* - it means
// this specialization and not the template, which is what makes `const Holder
// &` and a `Holder *` return type legal there. The source still writes
// `Holder(` for the constructor while the table keys it under the tag, so the
// two spellings are kept apart rather than conflated.
extern "C" { int printf(const char *, ...); }

template <class T, int N> struct Box {
    T slot[N];
    int size() { return N; }
    T get(int i) { return slot[i]; }
    void put(int i, T v) { slot[i] = v; }
};

template <class T> struct Holder {
    T item;
    Holder() { }
    ~Holder() { }
    void copyFrom(const Holder &o) { item = o.item; }
    Holder *self() { return this; }
};

// A specialization used as a member of another specialization, and one
// deduced from a call.
template <class T> struct Pair {
    Holder<T> first;
    Holder<T> second;
    T sum() { return first.item + second.item; }
};
template <class T> T unwrap(Holder<T> h) { return h.item; }

int main() {
    Box<int, 3> b;
    b.put(0, 7);
    b.put(2, 9);
    printf("%d %d %d\n", b.size(), b.get(0), b.get(2));

    Box<double, 2> d;
    d.put(1, 1.5);
    printf("%d %.1f\n", d.size(), d.get(1));

    Holder<int> a;
    a.item = 3;
    Holder<int> c;
    c.copyFrom(a);
    printf("%d %d\n", c.item, c.self() == &c);

    Pair<int> p;
    p.first.item = 4;
    p.second.item = 5;
    printf("%d\n", p.sum());

    printf("%d %d\n", unwrap<int>(a), unwrap(a));
    return 0;
}
