// **A class that introduces its own vptr on top of an ordinary base.**
// `Z : A { virtual int f(); }` with A a plain struct: Z is dynamic and A is
// not, so there is no primary base to take the vptr from and Z has to put one
// at offset 0 *in front of* A. clang's layout, from -fdump-record-layouts:
//
//     0 | struct Z
//     0 |   (Z vtable pointer)
//     8 |   struct A (base)
//     8 |     int x
//    12 |   int z
//       | [sizeof=16, dsize=16, align=8, nvsize=16, nvalign=8]
//
// cxx1 lays A at 0 and moves only Z's *own* members up by the pointer's
// width: `x` stays at 0, the constructor stores the vtable address over it,
// and the first virtual call - or the first read of `x` after one - is a
// segfault where clang prints `1 2 3 16`. The same fault sits under
// `Y : virtual P, A` for a plain A, where the class's own vptr is again the
// one that has to go first.
//
// Found 2026-09-10 while closing itanium-primary-base, whose rule - the first
// non-virtual base with a vptr goes to offset 0 - is the case where a base
// *does* supply the pointer; this is the case where none does. Mending it
// moves every base, not just the members, and then a single base is no
// longer at offset 0: the Itanium type_info for Z becomes
// `__vmi_class_type_info` rather than `__si_class_type_info`, which this
// compiler does not write yet, so `dynamic_cast` and `catch` on such a class
// are part of the same round. The Microsoft branch of the same layout code
// moves only the class's own members up as well, so x86_64-windows is
// expected to fail the same way; that half is read from the code, not
// measured - cl's layout of Z has not been taken on the box, and should be
// before the fix is written for that target.
extern "C" int printf(const char *, ...);

struct A { int x; };
struct Z : A { int z; virtual int f() { return x + z; } };

int main() {
    Z z;
    z.x = 1; z.z = 2;
    printf("%d %d %d %d\n", z.x, z.z, z.f(), (int)sizeof(Z));
    return 0;
}
