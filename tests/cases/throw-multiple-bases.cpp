// **Throwing a class with more than one base, and catching it by either.**
// The Itanium runtime finds a base subobject by walking the thrown type's
// `type_info`, and a class with two bases needs the third shape of one -
// `__vmi_class_type_info`, which carries an offset and a set of flags per
// base where `__si_class_type_info` says only "one public base, at zero".
// cxx1 emitted the first two shapes and refused this by name; the `.error`
// beside this file recorded that refusal until 2026-09-10.
//
// What makes the offsets matter here is `catch (B &)`: B sits at 4 in a C, so
// the runtime has to add 4 to the exception object's address, and it takes
// that 4 from the type_info rather than from anything the throw wrote down.
// A wrong offset there is not a crash but a wrong answer, which is why this
// case reads a member through every one of the three handlers.
//
// clang's record for C, byte for byte what cxx1 emits now:
//
//     _ZTI1C: __vmi+16, _ZTS1C, flags 0, base_count 2,
//             _ZTI1A, 2      (offset 0, public)
//             _ZTI1B, 1026   (offset 4 << 8, public)
extern "C" int printf(const char *, ...);

struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };

int main() {
    C c;
    c.a = 1; c.b = 2; c.c = 3;
    try { throw c; } catch (B &x) { printf("B %d\n", x.b); }
    try { throw c; } catch (A &x) { printf("A %d\n", x.a); }
    try { throw c; } catch (C &x) { printf("C %d %d %d\n", x.a, x.b, x.c); }
    return 0;
}
