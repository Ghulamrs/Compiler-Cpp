// **The Itanium ABI's primary base, which need not be the first one
// written.** `X : A, D1` with A an ordinary class and D1 holding a virtual
// base: D1 is dynamic - it carries a vptr, because that is where the offset
// to V is kept - and the ABI requires a dynamic class's vptr at offset 0. So
// clang makes D1 the *primary base* and lays it first, with A after it, and
// X's vptr is D1's. From -fdump-record-layouts, clang++ -std=c++11:
//
//      0 | struct X
//      0 |   struct D1 (primary base)
//      0 |     (D1 vtable pointer)
//      8 |     int b
//     12 |   struct A (base)
//     12 |     int x
//     16 |   int y
//     20 |   struct V (virtual base)
//     20 |     int a
//        | [sizeof=24, dsize=24, align=8, nvsize=20, nvalign=8]
//
// cxx1 laid the bases in the order written, A at 0 and D1 after it, so
// nothing dynamic sat at offset 0 and reading `x.a` loaded A's `x` as if it
// were a vptr: a segfault where clang prints 6 7 8 9. Registered as
// tests/open/itanium-primary-base.cpp on 2026-09-10 and closed the same day.
//
// What this file checks is the behaviour those numbers produce: the four
// members read back through the complete object, through a `D1 *` and a
// `V *` into it, which go through the vptr and the `vbase_offset` behind it,
// and A's distance from the top - 12, which is D1's nvsize. The bases are
// still *built* in the order written, A before D1, whichever is laid first;
// the trace says so, and clang prints the same one.
//
// **x86_64-windows is not this rule's, and gets the same answers by its own
// layout.** cl lays the bases in the order written - A at 0, D1's vbptr at 8,
// V at 32, sizeof 40, measured on the box in `vbl.cpp` - because a vbptr does
// not have to be at offset 0 the way a vptr does. cxx1 matches that byte for
// byte. That is why this file prints neither a size nor A's offset: one
// .expected serves all three targets, and the numbers live in this comment
// and in the layout dump, where the two ABIs are allowed to differ.
extern "C" int printf(const char *, ...);

struct V  { int a; V() : a(0) { printf("V "); } };
struct A  { int x; A() : x(0) { printf("A "); } };
struct D1 : virtual V { int b; D1() : b(0) { printf("D1 "); } };
struct X  : A, D1     { int y; X() : y(0) { printf("X "); } };

int main() {
    X x;
    printf("\n");
    x.a = 6; x.b = 7; x.x = 8; x.y = 9;
    printf("%d %d %d %d\n", x.a, x.b, x.x, x.y);

    // Through the dynamic base's own type, which is the read that has to
    // find a vptr at the base's offset 0 - and through the virtual base's.
    D1 *pd = &x;
    V  *pv = &x;
    pd->a = 16;
    printf("%d %d %d %d\n", pd->a, pd->b, pv->a, x.a);
    return 0;
}
