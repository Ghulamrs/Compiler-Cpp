// A const template argument, which the Microsoft ABI spells with a marker of
// its own and only at the top level. Measured with cl:
//
//     W<const int>    ?f@?$W@$$CBH@@QEAAHXZ    the argument carries $$CB
//     W<const int *>  ?f@?$W@PEBH@@QEAAHXZ     the const is on the pointee
//     W<int *const>   ?f@?$W@QEAH@@QEAAHXZ     the P becomes a Q instead
//
// cxx1 dropped the first of those, so `W<const int>` and `W<int>` shared one
// symbol on Windows - two different classes, one name, and whichever body was
// emitted answered for both. Silent, and only on that target. Itanium spells
// the same thing with a K and always had.
//
// The program says which class it reached, so a collision is a wrong answer
// here and not only a wrong name.
extern "C" { int printf(const char *, ...); }

template <class T> struct W { int which() { return 0; } };
template <> struct W<int> { int which() { return 1; } };
template <> struct W<const int> { int which() { return 2; } };

int main() {
    W<int> a;
    W<const int> b;
    W<const char> c;
    W<const int *> d;
    W<int *const> e;
    printf("%d %d %d %d %d\n", a.which(), b.which(), c.which(), d.which(),
           e.which());
    return 0;
}
