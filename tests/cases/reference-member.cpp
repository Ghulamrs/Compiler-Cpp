// A reference data member, refused since rung 3 and the thing `[&]` was
// really waiting for.
//
// **What it occupies is a pointer, and asking the type is the wrong
// question.** `sizeof` a reference is the size of what it refers to - right
// for `sizeof` and wrong for a slot - so the layout asks a slot type instead
// while the *declared* type stays the reference, which is what tells every
// read of it to dereference.
//
// **It is bound, never assigned**, and the mem-initialiser list is the one
// place that can happen: `bindReference` supplies the address, the same road a
// reference local's initialiser takes.
//
// **A const object does not reach through it.** [dcl.ref]: the const stops at
// the reference, which could not be rebound anyway - so `h.r` on a const `h`
// is still `int &`, and `get() const` can return it. Applying the object's
// const here made a const member function unable to return its own member.
extern "C" { int printf(const char *, ...); }

struct Holder {
    int &r;
    int n;
    Holder(int &v);
    int get() const;
};

Holder::Holder(int &v) : r(v), n(1) { }
int Holder::get() const { return r; }

struct Two {
    int &a;
    int &b;
    Two(int &x, int &y);
};

Two::Two(int &x, int &y) : a(x), b(y) { }

int main(void) {
    int k = 5;
    int p = 1;
    int q = 2;
    Holder h(k);
    Two t(p, q);
    printf("%d %d ", h.get(), h.r);
    k = 9;                      // the member refers to k, so it follows
    h.r = 11;                   // and writing through it writes k
    printf("%d %d %d %d\n", h.get(), k, t.a * 10 + t.b, (int)sizeof(Holder));
    return 0;
}
