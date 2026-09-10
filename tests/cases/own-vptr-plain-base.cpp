// **A class that introduces its own vptr on top of an ordinary base.**
// `Z : A { int z; virtual int f(); }` with A a plain struct: Z is dynamic and
// A is not, so there is no base to take a vptr from and Z has to put one at
// offset 0 *in front of* A. Both oracles agree, which is unusual enough in
// this compiler's Microsoft work to be worth saying twice - clang's
// -fdump-record-layouts and cl's /d1reportSingleClassLayout give the same
// four numbers:
//
//     0 | (Z vtable pointer)          0 | {vfptr}
//     8 |   struct A                  8 | +--- (base class A)
//     8 |     int x                   8 | | x
//    12 |   int z                    12 | z
//       | [sizeof=16]                   | size(16)
//
// cxx1 laid A at 0 and moved only Z's *own* members down by the pointer's
// width, so the constructor's vtable store landed on `A::x` and the first
// virtual call after a write to it was a **segfault** - on a shape as
// ordinary as they come. Registered as tests/open/own-vptr-plain-base.cpp on
// 2026-09-10 and closed the same day, the fix being that the pointer moves
// every base and not only the members: Type::shiftBaseOffsets.
//
// **And the fix made the type_info a lie until that was mended too.** With A
// at 8, `__si_class_type_info` - which means "one public base, at offset
// zero" - describes the wrong object, and `catch (A &)` on a thrown Z read
// eight bytes early and printed rubbish. Z needs the third shape,
// `__vmi_class_type_info` with `(8 << 8) | 2`, which is what clang emits and
// what cxx1 emits now. The catch below is what holds that.
//
// The two are one round because either alone is worse than neither: the
// layout without the type_info is a wrong `catch`, and the type_info without
// the layout describes a class that does not exist.
extern "C" int printf(const char *, ...);

struct A { int x; };
struct Z : A { int z; virtual int f() { return x + z; } };

int main() {
    Z z;
    z.x = 1; z.z = 2;
    printf("%d %d %d %d\n", z.x, z.z, z.f(), (int)sizeof(Z));

    // Through the base's own type, which is the read the offset decides.
    A *a = &z;
    printf("%d\n", a->x);
    return 0;
}
