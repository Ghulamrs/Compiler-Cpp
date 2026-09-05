// **`Base::f(...)` inside an override - the version this class replaced.**
// [expr.call]/1: naming the function with a qualified-id suppresses the
// dispatch, which is the whole reason an override writes it. Two of
// Compiler++'s sixteen sources do, as `cc::Lowering::lowerDecl(d)`.
//
// Three things had to be true.
//
// **The lookup does not walk up.** A qualified call says which class's version
// it means, so `findMemberOwner` is not asked - the owner is the class named.
//
// **The dispatch is off.** The same call through a `Base *` still reads the
// vtable; this one names the function, and the two appear in the output side by
// side below.
//
// **And the base's own name resolves.** `cc::Base::f` is in the type table;
// plain `Base::f` is the **injected class name**, which is not - so the bases
// are walked and their `localName()` compared, the same half the mem-initialiser
// list needed.
//
// Beside it, [class.access.base]/5: a **protected** member is reachable from a
// derived class, which "are we inside that class" alone cannot say. That was
// refused for a protected static called unqualified from a derived member -
// `isArrayType(f->type)` in Compiler++ - and the access check now takes the
// member's access, because private and protected are different questions.
extern "C" int printf(const char *, ...);

namespace cc {
    struct Base {
        virtual ~Base();
        virtual int f(int v);
        int plain(int v);
    protected:
        int helper(int v);
        static int guarded(int v);
    };
}
cc::Base::~Base() {}
int cc::Base::f(int v) { return v + 1; }
int cc::Base::plain(int v) { return v + 100; }
int cc::Base::helper(int v) { return v * 2; }
int cc::Base::guarded(int v) { return v * 3; }

struct D : cc::Base {
    int f(int v);
    int g(int v);
    int prot(int v);
};
// The qualified spelling, and the dispatch is off: this is Base's f, not D's.
int D::f(int v) { return cc::Base::f(v) * 10; }
// The injected class name, which is how it is usually written.
int D::g(int v) { return Base::f(v) + plain(v); }
// A protected member of the base, unqualified and qualified, and a protected
// static - none of which is "inside cc::Base" by any reading.
int D::prot(int v) { return helper(v) + cc::Base::helper(v) + guarded(v); }

int main() {
    D d;
    // Built directly as well as as a subobject: without this, clang has no use
    // for cc::Base's complete-object constructor and emits none, which reads as
    // a name difference and is a difference in what the program asks for.
    cc::Base alone;
    cc::Base *through = &d;
    // d.f(1) is D's, which calls Base's; through->f(1) dispatches to D's.
    printf("%d %d %d %d %d\n",
           d.f(1), d.g(1), through->f(1), d.prot(2), alone.f(1));
    return 0;
}
