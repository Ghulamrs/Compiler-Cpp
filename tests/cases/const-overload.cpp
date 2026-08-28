// `get()` and `get() const` are two functions, and which one a call means
// depends on the object it is made on.
//
// **A member function has an implicit object parameter** - [over.match.funcs] -
// and overload resolution ranks it like any other argument. Binding it is a
// reference binding: an exact match where the constness agrees, a
// qualification conversion where a const member is called on a non-const
// object, and no match at all the other way round. That last one is what makes
// a non-const member simply unavailable on a const object rather than a worse
// candidate; the first two are what make the non-const member win on a
// non-const object, since an exact match beats a qualification.
//
// The two are already different symbols - _ZN1P3getEv and _ZNK1P3getEv, and
// QEAA against QEBA on Windows - so nothing here is about names. What was
// missing was the ranking: before it, a class declaring both could not be
// called at all, because the two candidates tied.
extern "C" { int printf(const char *, ...); }

class P {
public:
    P();
    int get();
    int get() const;
    int only() const;
    int scale(int n);
    int scale(int n) const;

    // Inside a member function the object is `this`, so the same ranking
    // decides an unqualified call - and a const member function can only
    // reach the const one.
    int fromPlain();
    int fromConst() const;

    int a;
};

P::P()                    { a = 1; }
int P::get()              { return 10; }
int P::get() const        { return 20; }
int P::only() const       { return 30; }
int P::scale(int n)       { return n * 100; }
int P::scale(int n) const { return n * 200; }
int P::fromPlain()        { return get(); }
int P::fromConst() const  { return get(); }

int throughRef(P &r)            { return r.get() + r.scale(1); }
int throughConstRef(const P &r) { return r.get() + r.scale(1) + r.only(); }

int main() {
    P p;
    const P c;

    printf("%d %d\n", p.get(), c.get());
    printf("%d %d\n", p.scale(1), c.scale(1));

    // Only one of them exists, and it is reachable from both.
    printf("%d %d\n", p.only(), c.only());

    P *pp = &p;
    const P *cp = &p;
    printf("%d %d\n", pp->get(), cp->get());

    printf("%d %d\n", throughRef(p), throughConstRef(p));
    printf("%d %d\n", p.fromPlain(), c.fromConst());
    return 0;
}
