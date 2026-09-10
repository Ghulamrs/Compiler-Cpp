// Reading a data member of a virtual base - the plainest thing a virtual base
// is for, and the shape that used to **crash** this compiler for
// x86_64-windows.
//
// The Itanium targets reach one through the vtable's `vbase_offset`, which is
// what this case measures on both of them. x86_64-windows keeps the offset in
// a vbtable instead, and cxx1 emits none - so `virtualBaseMember` answered
// nothing for that target. The caller had already moved the object into the
// call, so the ordinary constant-offset access below it was built on a
// moved-from pointer, and the compiler died in a `dynamic_cast` with no
// message on a program both other targets compile. It is refused by name now;
// the .notarget beside this file carries the message.
//
// The layout that target needs is measured - CLAUDE.md, "The Microsoft
// virtual-base layout, measured" - and writing it is its own round.
extern "C" int printf(const char *, ...);

struct V {
    int a;
    V();
};

V::V() : a(1) {}

struct D : virtual V {
    int b;
    D();
};

D::D() : b(2) {}

// Through a reference to the derived class, and through a pointer to the base:
// the second is the one that has to go through the offset rather than a cast.
int throughBase(const V &v) { return v.a; }

int main() {
    D d;
    d.a = 10;
    d.b = 20;
    const V *asBase = &d;
    printf("%d %d %d %d\n", d.a, d.b, throughBase(d), asBase->a);
    return 0;
}
