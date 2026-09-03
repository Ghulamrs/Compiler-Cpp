// `dynamic_cast<T *>(p)` - the one cast that asks the object.
//
// **Every other cast is answered from the types written down.** This one reads
// the vtable and walks the inheritance graph hung off it, so what the compiler
// emits is only the question: two `_ZTI` objects and a call to the runtime's
// `__dynamic_cast`. The graph is the type_info objects themselves - a class
// with no base is `__class_type_info`, one with a single public base is
// `__si_class_type_info` and carries its base's `_ZTI` as a third word - and
// the vtable's second slot, a plain zero until this landed, points at the one
// for the complete object.
//
// Three shapes are not the runtime's business and are answered here:
//
//   - **A null pointer casts to a null pointer without asking.** The runtime
//     reads the vtable *through* the pointer it is handed and would fault.
//   - **An upcast is not a question** - [expr.dynamic.cast]/5. If the target is
//     the operand's own class or a public base of it, the types decide. This
//     one has teeth: libc++abi's `__dynamic_cast` is written for the other
//     direction and answers *null* for an upcast, so handing it one produced a
//     null that the next `->` dereferenced.
//   - **A class with no virtual function** carries nothing that says what it
//     is, so there is nothing to ask; refused by name.
//
// The failing cast answering null is the half worth having a case for: it is
// what the 354 sites in the Compiler++ tree are written on.

extern "C" int printf(const char *, ...);

struct Base { virtual ~Base(); virtual int who(); };
struct Mid : Base { int who(); };
struct Leaf : Mid { int who(); };
struct Other : Base { int who(); };

Base::~Base() {}
int Base::who() { return 0; }
int Mid::who() { return 1; }
int Leaf::who() { return 2; }
int Other::who() { return 3; }

namespace N {
    struct Shape { virtual ~Shape(); virtual int sides(); };
    struct Square : Shape { int sides(); };
    Shape::~Shape() {}
    int Shape::sides() { return 0; }
    int Square::sides() { return 4; }
}

int main(void) {
    Leaf l;
    Mid m;
    Other o;
    Base *bl = &l, *bm = &m, *bo = &o, *nil = 0;

    // Down the chain it was built from, and across to a sibling, and off a null
    printf("%d %d %d %d\n", dynamic_cast<Leaf *>(bl) != 0,
                            dynamic_cast<Leaf *>(bm) != 0,
                            dynamic_cast<Leaf *>(bo) != 0,
                            dynamic_cast<Leaf *>(nil) != 0);

    // One rung down, the sibling to itself, and a Mid that really is a Leaf
    printf("%d %d %d\n", dynamic_cast<Mid *>(bl) != 0,
                         dynamic_cast<Other *>(bo) != 0,
                         dynamic_cast<Mid *>(bm) != 0);

    // The answer is a usable pointer, not just a yes
    Leaf *back = dynamic_cast<Leaf *>(bl);
    Mid *middle = dynamic_cast<Mid *>(bl);
    printf("%d %d\n", back->who(), middle->who());

    // An upcast, which the types answer, and one to the operand's own class
    printf("%d %d\n", dynamic_cast<Base *>(bl)->who(),
                      dynamic_cast<Base *>(bm) == bm);

    // A class in a namespace, whose `_ZTS` spells the whole nested name
    N::Square sq;
    N::Shape *sh = &sq;
    printf("%d %d\n", dynamic_cast<N::Square *>(sh) != 0,
                      dynamic_cast<N::Square *>(sh)->sides());
    return 0;
}
