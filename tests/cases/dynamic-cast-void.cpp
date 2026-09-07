// `dynamic_cast<void *>(p)` - [expr.dynamic.cast]/7, "where does the complete
// object begin?"
//
// **It is the one dynamic_cast that names no target class**, so it asks the
// object without asking the type graph: no second `_ZTI` is handed over and no
// base chain is walked. The answer is a number the object already carries.
//
// The two ABIs answer it differently, and neither was guessed:
//
//   - **Itanium generates it inline.** Measured from clang at -O0 on
//     arm64-darwin: load the vptr, load a signed offset-to-top from two words
//     in front of the address point, add it to the pointer.
//
//         ldr  x9, [x8]
//         ldur x9, [x9, #-16]
//         add  x8, x8, x9
//
//   - **Microsoft calls the runtime.** There is no offset-to-top; the complete
//     object is reached through the Complete Object Locator in front of the
//     vftable, and `__RTCastToVoid(p)` is what does it - one argument, where
//     `__RTDynamicCast` takes five.
//
// **No address is printed**, because addresses differ from run to run and
// between compilers; every line is a comparison of two of them.
//
// The null operand is not an optimisation. On Itanium the load goes *through*
// the pointer, so a null one would fault where [expr.dynamic.cast]/2 says the
// answer is null.
//
// The base-not-at-offset-zero shape is `dynamic-cast-void-multiple.cpp`, which
// is a separate file because the Microsoft ABI refuses that layout for an older
// reason and this one has to run on all three targets.

extern "C" int printf(const char *, ...);

struct Base { virtual ~Base(); int x; };
struct Derived : Base { int y; };
struct Deeper : Derived { int z; };

Base::~Base() {}

int main(void) {
    Derived d;
    Base *base = &d;
    printf("derived through base: %d\n",
           dynamic_cast<void *>(base) == (void *)&d);

    Deeper three;
    Base *deep = &three;
    printf("two levels up: %d\n",
           dynamic_cast<void *>(deep) == (void *)&three);

    Base plain;
    printf("already most derived: %d\n",
           dynamic_cast<void *>(&plain) == (void *)&plain);

    const Base *readOnly = &d;
    printf("through a const pointer: %d\n",
           dynamic_cast<const void *>(readOnly) == (const void *)&d);

    Base *none = 0;
    printf("null answers null: %d\n", dynamic_cast<void *>(none) == (void *)0);
    return 0;
}
