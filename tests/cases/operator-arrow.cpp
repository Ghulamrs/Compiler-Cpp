// `operator->` - the operator that is applied again to its own answer.
//
// **[over.ref] does not stop at one call.** `it->m` asks the class for a
// pointer; if what comes back is another class with an `operator->`, the rule
// applies again, and again, until something hands back a real pointer. That is
// what makes an iterator wrapping an iterator work, and it is the reason this
// is a loop here rather than a single call - with a hop count, because a class
// whose `operator->` answers with its own type is a cycle and has to be a
// diagnostic rather than a hung parser.
//
// A class on the left of `->` with no `operator->` is an error, not a
// reinterpretation of its first bytes as a pointer.

extern "C" int printf(const char *, ...);

struct Cell {
    int v;
    int twice(void) const { return v * 2; }
};

struct It {
    Cell *p;
    Cell *operator->() const { return p; }        // one hop: straight to a pointer
    Cell &operator*() const { return *p; }
};

struct Wrap {
    It inner;
    It operator->() const { return inner; }       // two hops: a class, then a pointer
};

struct Deeper {
    Wrap inner;
    Wrap operator->() const { return inner; }     // three
};

int main(void) {
    Cell c;
    c.v = 21;
    It i;
    i.p = &c;
    Wrap w;
    w.inner = i;
    Deeper d;
    d.inner = w;
    i->v = 21;                                    // and it is an lvalue
    printf("%d %d %d %d %d\n", i->v, i->twice(), (*i).v, w->v, d->twice());
    return 0;
}
