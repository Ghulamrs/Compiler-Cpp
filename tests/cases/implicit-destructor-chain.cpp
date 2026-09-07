// The implicit destructor of an *intermediate* class, which was referenced and
// never defined - so a three-level hierarchy failed to **link**.
//
// `C : B : A`, with a destructor written only on A. B's destructor is implicit
// and exists solely to destroy its A subobject; C's is implicit and calls B's.
// `synthesizeDestructor` built that call through `completeCall` directly and
// never marked the callee used - where `destructorCall` does, and
// `synthesizeCopy` does for its bases and members. `defineImplicitFunctions`
// runs to a fixed point for exactly this reason and was never told, so B's
// destructor was emitted by nobody:
//
//     Undefined symbols: "B2::~B2()", referenced from: C2::~C2()
//
// **Two levels hid it**, because there the base's destructor is the written one
// and has a body whoever asks. It took a third class - the first one whose
// destructor is implicit *and* has an implicit caller - and it is a plain C++98
// shape with no templates, no virtuals and no exceptions near it.
//
// It matters here because `<stdexcept>` is this hierarchy: a base, an
// intermediate that adds nothing but its own type, and concrete classes below.
// `catcher2.cpp`'s `CustomError : std::runtime_error` is exactly three deep.
// It was found by throwing a three-level object and catching it by its top
// base - but no throw is left here, deliberately. `throw C2(3)` makes the trace
// elision-dependent: cxx1 builds the temporary and copy-constructs the
// exception object where clang builds one object, so cxx1 destroys twice and
// clang once, and [class.copy]/31 allows both. The destructor chain is what
// this case is about and it needs no exception at all, which also lets it run
// on x86_64-windows, where a class throw is still refused.
//
// The fourth level is in the case because the fix is a fixed point rather than
// one step: D's destructor pulls in C's, which pulls in B's.
//
// Constructors are out of line, as every case here defines them, or names.sh
// reports the comdat difference as though it were a mangling one.

extern "C" int printf(const char *, ...);

struct A2 { int v; A2(int x); ~A2(); };
struct B2 : A2 { B2(int x); };            // implicit destructor
struct C2 : B2 { C2(int x); };            // implicit destructor, calling B2's
struct D2 : C2 { D2(int x); };            // and one more, to make it a chain

A2::A2(int x) : v(x) { printf("+A%d ", v); }
A2::~A2() { printf("-A%d ", v); }
B2::B2(int x) : A2(x) {}
C2::C2(int x) : B2(x) {}
D2::D2(int x) : C2(x) {}

int main(void) {
    { C2 c(1); printf("c=%d ", c.v); }
    printf("\n");
    { D2 d(2); printf("d=%d ", d.v); }
    printf("\n");

    return 0;
}
