// **Itanium picks the primary base, and cxx1 lays the bases in the order
// written.** `X : A, D1` where A is an ordinary class and D1 has a virtual
// base: D1 is dynamic, so a `D1 *` into an X must find a vptr, and the ABI
// puts the dynamic base at offset 0 for that reason. clang's layout, from
// -fdump-record-layouts:
//
//     0 | struct X
//     0 |   struct D1 (primary base)
//     0 |     (D1 vtable pointer)
//     8 |     int b
//    12 |   struct A (base)
//    12 |     int x
//    16 |   int y
//    20 |   struct V (virtual base)
//
// cxx1 puts A at 0 and D1 after it, so nothing dynamic sits at offset 0 and
// reading a member of the virtual base loads A's `x` as a vptr: it segfaults
// rather than printing. Found 2026-09-10 while writing the Microsoft vbtable
// case, which needed exactly this shape to settle what its entry 0 means.
//
// **x86_64-windows gets this one right**, and that is not luck: cl lays the
// bases in the order written too - A at 0, D1's vbptr at 8, V at 32, sizeof
// 40 - because a vbptr does not have to be at offset 0 the way a vptr does.
// Measured on the box, `vbl.cpp`, and cxx1 agrees with it byte for byte.
extern "C" int printf(const char *, ...);

struct V  { int a; };
struct A  { int x; };
struct D1 : virtual V { int b; };
struct X  : A, D1     { int y; };

int main() {
    X x;
    x.a = 6; x.b = 7; x.x = 8; x.y = 9;
    printf("%d %d %d %d\n", x.a, x.b, x.x, x.y);
    return 0;
}
