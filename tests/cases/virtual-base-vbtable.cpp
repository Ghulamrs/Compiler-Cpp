// **The Microsoft ABI's virtual bases, which are a table and not a vtable
// entry** - the round that followed the measurement virtual-base-member's
// refusal used to point at.
//
// An object with a virtual base holds a *vbptr*, pointing at a **vbtable** of
// four-byte offsets measured from where that pointer sits. Entry 0 steps back
// to the top of the class that introduced the pointer and the rest name the
// virtual bases, so a base is at `this + vbptrOffset + table[slot]` however
// derived the complete object turns out to be. The Itanium targets keep the
// same number as a `vbase_offset` in the vtable at a negative index, which is
// why the two paths through `virtualBaseMember` share nothing but their shape.
//
// Every number below was taken from cl on the Windows box (`vbfull.cpp` and
// `vbx.cpp`, 2026-09-10) and is checked by this file only indirectly - what it
// reads is the *behaviour*, which is what the table has to produce:
//
//     D1 : virtual V           sizeof 24   vbptr 0, b 8, V 16   ??_8D1@@7B@  0, 16
//     R  : D1                  sizeof 32   vbptr 0, r 16, V 24  ??_8R@@7B@   0, 24
//     Q  : virtual V, virtual  sizeof 32   vfptr 0, vbptr 8     ??_8Q@@7B@  -8, 16
//     X  : A, D1               sizeof 40   x 0, vbptr 8, V 32   ??_8X@@7B@   0, 24
//
// X is the shape that settled entry 0, and it is **not** in this case: its
// vbptr is inherited and does not sit at offset 0, and cl still writes 0
// there - because D1, which introduced the pointer, keeps it at its own
// offset 0. Reading that as "minus this class's vbptr offset" gave -8 and put
// the base eight bytes early, which is the bug this file's numbers caught.
// It stays out because **the Itanium targets get X wrong**, and for a reason
// that has nothing to do with this round: clang makes the dynamic base the
// primary one and puts D1 at offset 0 with A after it, where cxx1 lays the
// bases in the order written and leaves no vptr at 0 at all. Registered in
// tests/open/itanium-primary-base.cpp.
extern "C" int printf(const char *, ...);

struct V { int a; };
struct D1 : virtual V { int b; };
struct R  : D1        { int r; };

struct Q : virtual V {
    int q;
    virtual int f() { return a + q; }
};

// Through the base's own type, which is the read that must go through the
// table rather than a constant.
int throughBase(const V &v) { return v.a; }

int main() {
    D1 d; d.a = 1; d.b = 2;
    R  r; r.a = 3; r.b = 4; r.r = 5;
    Q  q; q.a = 10; q.q = 11;

    // And through a pointer to the derived class, so the access is not folded
    // into the object's own address.
    D1 *pd = &d;
    R  *pr = &r;

    printf("%d %d\n", d.a, d.b);
    printf("%d %d %d\n", r.a, r.b, r.r);
    printf("%d %d %d\n", q.a, q.q, q.f());
    printf("%d %d\n", pd->a, pr->a);
    printf("%d %d\n", throughBase(d), throughBase(r));

    // The conversion in its own right, and written through: `V *pv = &r` is
    // the same table read as the reference above, and the store has to land
    // in the one V an R holds rather than in R's own member.
    V *pv = &r;
    pv->a = 30;
    printf("%d %d\n", r.a, pv->a);
    return 0;
}
