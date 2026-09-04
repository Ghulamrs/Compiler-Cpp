// **A forward declaration carries its keyword, and Microsoft spells it.**
// [dcl.type.elab]: `class D;` and `struct D;` declare the same entity, and the
// standard lets a program mix them - but the Microsoft ABI writes V for a class
// and U for a struct, so the two spellings are two symbols. cxx1 recorded the
// keyword only at the *definition*, which is right for a diagnostic and wrong
// for a name: a translation unit seeing only `class D;` wrote U where one
// seeing `class D { };` wrote V, and the two did not link.
//
// It cost Compiler++ its link on this target and nothing else could have shown
// it - the suite compiles one file at a time, where both spellings agree with
// themselves. `?f@@YAHAEAVD@@@Z` against clang is what tests/names.sh checks.
//
// The definition still wins where a program mixes the two, which is what makes
// `class X;` followed by `struct X { };` one type rather than two.
extern "C" int printf(const char *, ...);

// Declared and never defined here, and named in a signature.
class Opaque;
int through(Opaque *p);

// Declared with one keyword, defined with the other. The definition decides.
struct Mixed;
class Mixed { public: int v; };

int take(Mixed &m) { return m.v; }

// A class the file both declares and defines, for the ordinary case.
class Plain;
class Plain { public: int w; };

int hold(Plain &p) { return p.w; }

int through(Opaque *p) { return p != 0; }

int main() {
    Mixed m; m.v = 4;
    Plain q; q.w = 5;
    printf("%d %d %d\n", through(0), take(m), hold(q));
    return 0;
}
