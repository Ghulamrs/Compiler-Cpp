// A pointer or a reference to a `volatile` type, refused by name.
//
// The qualifier is part of the type here, and both ABIs write it: Itanium
// `_Z1fPVi` where a plain `int *` is `_Z1fPi`, cl `?f@@YAXPECH@Z` where a plain
// one is `?f@@YAXPEAH@Z`. cxx1 has no volatile in its type system, so before the
// 2026-09-07 sweep it wrote the plain name for both and said nothing.
//
// **A silent wrong linkage name is the worst answer of the four** a sweep can
// find - it compiles, it runs, and it fails only against an object file
// somebody else compiled. volatile-object.cpp is the other side: the shapes
// where dropping the qualifier costs nothing are still accepted.
//
// The refusal is at the specifiers, looking at the token after them, which is
// why it also catches `volatile int &`, `const volatile int *`, a cast written
// `(volatile int *)`, and a template argument `f<volatile int *>`. Each of those
// reached a different generic message before.

void f(volatile int *p) { *p = 1; }

int main() {
    volatile int v = 0;
    f((volatile int *)&v);
    return 0;
}
