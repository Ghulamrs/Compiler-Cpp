// explicit.
//
// **It changes nothing about the function and only about who may pick it.**
// `S s(3);` and `S s = 3;` call the same constructor with the same argument
// and differ in one thing: whether that constructor is allowed to be chosen
// without being asked for by name. So `explicit` is one bool on the signature,
// checked at each place the standard calls copy-initialization, and no
// mangled name, no vtable and no emitted code changes.
//
// The flag lives on the *signature* and not on the class, because it is one
// constructor of a set that is explicit and not the class - `Both` below has
// one of each and uses both in the same function.
//
// **Three places copy-initialization happens here**, and only the first is
// written with an '='. See the four refusal cases beside this one:
// `S s = x;`, `return s;`, and a by-value parameter.

extern "C" int printf(const char *, ...);

// **Every constructor here is defined out of line**, and that is about the
// test rather than about `explicit`: for a constructor defined *inside* its
// class, clang on x86_64-linux emits only the base-object `C2` and calls it,
// where cxx1 emits and calls the complete-object `C1`. Both are self-
// consistent and both link, but `names.sh` reads it as a disagreement. Defined
// out of line, both compilers emit both. It also puts the second half of the
// rule on show: `explicit` belongs to the declaration inside the class and is
// not repeated here - explicit-outside-class-refused.cpp pins that.

struct Counted {
    int v;
    explicit Counted(int a);                    // may not be picked implicitly
    int get(void) const { return v; }
};
Counted::Counted(int a) { v = a; }

struct Both {
    int v;
    explicit Both(int a);                       // one of each, same class
    Both(char *p);
};
Both::Both(int a) { v = a * 10; }
Both::Both(char *p) { v = p == nullptr ? 1 : 2; }

struct Guarded {
    int v;
    Guarded(int a);
    explicit Guarded(const Guarded &o);         // explicit *copy* constructor
};
Guarded::Guarded(int a) { v = a; }
Guarded::Guarded(const Guarded &o) { v = o.v + 1000; }

struct Wide {
    int v;
    explicit Wide(int a, int b);                // legal on any constructor
    explicit Wide(void);                        // including the default one
};
Wide::Wide(int a, int b) { v = a + b; }
Wide::Wide(void) { v = -1; }

int byReference(const Guarded &g) { return g.v; }

int main(void) {
    // Direct-initialization asks for the constructor by name, which is
    // exactly what `explicit` requires and permits.
    Counted c(3);
    Wide two(5, 6);
    Wide none;

    // The non-explicit sibling is still reachable implicitly.
    Both fromPointer = nullptr;
    Both fromInt(7);

    // An explicit *copy* constructor: direct-init copies, and a reference
    // parameter does not copy at all, so neither needs the rule relaxed.
    Guarded g(8);
    Guarded copy(g);

    printf("%d %d %d\n", c.get(), two.v, none.v);
    printf("%d %d\n", fromPointer.v, fromInt.v);
    printf("%d %d\n", copy.v, byReference(g));
    return 0;
}
