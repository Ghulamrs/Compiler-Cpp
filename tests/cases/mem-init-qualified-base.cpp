// **A base named with its namespace in a mem-initialiser list** -
// `: cc::Lowering(module, l, d)` - which is how a class writes it when the
// base is not in scope unqualified. Two of Compiler++'s sixteen sources stop
// there, and both spellings were refused:
//
//   `: cc::Base(v)`  read one identifier and then wanted a `(`, finding `::`
//   `: Base(v)`      resolved nothing, because the base's tag is `cc::Base`
//                    and the entry was compared against it as a string
//
// The comparison by *type* was already here for the second - added when a base
// in a namespace first appeared - but it needs `findTypedef(entry)` to answer,
// and inside the derived class `Base` alone is the **injected class name**
// rather than anything the type table holds. So the base's `localName()` is
// compared too, which is what that injected name is.
//
// The list is read as a qualified name now, and either spelling arrives at the
// same base: what goes in the map is the tag, because that is what the walk
// over the bases looks it up by.
extern "C" int printf(const char *, ...);

namespace cc {
    struct Base {
        int n;
        Base(int v);
    };
    // A second base in the same namespace, so the walk has to choose.
    struct Other {
        int m;
        Other(int v);
    };
}
cc::Base::Base(int v) : n(v) {}
cc::Other::Other(int v) : m(v * 10) {}

// The qualified spelling.
struct A : cc::Base {
    A(int v);
};
// Written at file scope, where the base's name is not in scope unqualified at
// all - so the qualified spelling is the only one that can be used here.
A::A(int v) : cc::Base(v) {}

// The unqualified one, which is the injected class name.
struct B : cc::Base {
    B(int v);
};
// And the injected class name, which reaches the base from an out-of-line
// definition just as it does from inside the class body.
B::B(int v) : Base(v) {}

// Two bases, one of each spelling, in the other order than they are declared -
// the list may name them in any order and the construction still runs in
// declaration order.
struct C : cc::Base, cc::Other {
    int own;
    C(int v);
};
C::C(int v) : cc::Other(v), Base(v + 1), own(v + 2) {}

int main() {
    A a(1);
    B b(2);
    C c(3);
    printf("%d %d %d %d %d\n", a.n, b.n, c.n, c.m, c.own);
    return 0;
}
