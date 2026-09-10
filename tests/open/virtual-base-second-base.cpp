// **A second base with a virtual base of its own**, on the Itanium targets.
// `Two : E1, E2` where E1 names V virtually and E2 names W: reading W through
// an `E2 *` walks E2's own vptr for a `vbase_offset`, and cxx1 puts nothing
// useful there - it prints V's member instead. clang gives 8, cxx1 gives 7.
//
// **x86_64-windows gets this right**, and it is the same round's work that
// made it so: each subobject holding a vbptr gets its own vbtable there,
// `??_8Two@@7BE1@@@` (0, 40, 44) and `??_8Two@@7BE2@@@` (0, 28), measured
// against cl. The Itanium equivalent is the *secondary* vtable's negative
// entries, which this compiler does not lay down for a base that is dynamic
// only because it has a virtual base.
//
// Found 2026-09-10 while writing the Microsoft diamond.
extern "C" int printf(const char *, ...);

struct V { int a; };
struct W { int w; };
struct E1 : virtual V { int e; };
struct E2 : virtual W { int f; };
struct Two : E1, E2 { int t; };

int main() {
    Two y;
    y.a = 7; y.w = 8; y.e = 9;
    E2 *q = &y;
    E1 *r = &y;
    printf("%d %d %d %d %d\n", y.a, y.w, y.e, q->w, r->a);
    return 0;
}
