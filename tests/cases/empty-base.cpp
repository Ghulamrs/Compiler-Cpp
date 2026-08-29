// The empty base optimisation. An empty class has sizeof 1 - so that two
// objects of it have different addresses - but a *data size* of 0, and those
// are deliberately different numbers. The 0 is what says how far into an
// object a base's data reaches, so a class derived from an empty one puts its
// own members at offset 0 and the base costs nothing.
//
// The Itanium ABI requires this and the Microsoft ABI does the same, so
// getting it wrong is not a matter of taste: `struct D : E { int x; };` was 8
// bytes here where clang and cl both say 4, and every class with an empty
// base had a layout that agreed with no other compiler.
//
// **No `long` anywhere, because this prints sizeof.** `long` is 8 bytes on the
// two Itanium targets and 4 on Windows - LP64 against LLP64 - so a case that
// measures a layout containing one measures the data model instead, and the
// same .expected cannot serve all three. `double` is 8 everywhere and says
// what was meant.
extern "C" int printf(const char *, ...);

struct Empty { };
struct One : Empty { int x; };
struct Two : One { char c; };
struct Behaviour { int twice(int n) { return n * 2; } };
struct Uses : Behaviour { double v; };

struct Plain { int a; };
struct Derived : Plain { int b; };

int main() {
    One o; o.x = 1;
    Two t; t.x = 2; t.c = 'z';
    Uses u; u.v = 21;
    Derived d; d.a = 3; d.b = 4;

    printf("%d %d %d %d %d %d %d\n", (int)sizeof(Empty), (int)sizeof(One),
           (int)sizeof(Two), (int)sizeof(Behaviour), (int)sizeof(Uses),
           (int)sizeof(Plain), (int)sizeof(Derived));
    printf("%d %d %c %.0f %d %d\n", o.x, t.x, t.c, u.v, d.a, d.b);
    printf("%d\n", u.twice(21));
    return 0;
}
