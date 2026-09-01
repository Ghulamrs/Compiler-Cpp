// **The two ABIs allocate bitfields differently, and one walk served both.**
// Itanium packs them end to end and lets one allocation unit hold fields of
// different declared types; the Microsoft ABI gives each declared type its own
// unit, starts a new one whenever the type changes or the current one is full,
// and charges for the whole unit however little of it is used. So
// `{int a:3; char b:2;}` is 4 bytes on Itanium and 8 on Windows, where cxx1
// said 4 everywhere.
//
// **And a zero-width bitfield matched neither.** `{char a; int :0; char b;}`
// is 5 bytes aligned 1 on Itanium and 2 aligned 1 on Windows; cxx1 made it 8
// aligned 4, because the `int` was allowed to widen the class the way a real
// member would.
//
// Every number here was measured with clang for the ABI it sits under. `EA`
// stands in for alignof, which is not implemented yet: a char in front of E
// shows what E's alignment forces.
extern "C" int printf(const char *, ...);

struct A { int a:3; char b:2; };
struct B { char a:7; int b:25; };
struct C { long long a:33; int b:31; };
struct D { char a; int b:24; };
struct E { char a; int :0; char b; };
struct F { int a:3; int b:5; };
struct G { int a:16; short b:8; };
struct EA { char c; E e; };

#ifdef _WIN32
static_assert(sizeof(A)  ==  8, "a char bitfield starts its own unit");
static_assert(sizeof(B)  ==  8, "and so does an int after a char");
static_assert(sizeof(C)  == 16, "an int unit after a long long one");
static_assert(sizeof(D)  ==  8, "an ordinary member closes nothing to reuse");
static_assert(sizeof(E)  ==  2, "':0' closes the unit and costs nothing");
static_assert(sizeof(EA) ==  3, "so E is aligned 1");
static_assert(sizeof(F)  ==  4, "two ints share one unit");
static_assert(sizeof(G)  ==  8, "a short does not join an int's unit");
#else
static_assert(sizeof(A)  ==  4, "different types share a unit here");
static_assert(sizeof(B)  ==  4, "and the int packs in behind the char");
static_assert(sizeof(C)  ==  8, "33 and 31 bits fit one long long");
static_assert(sizeof(D)  ==  4, "the int bitfield packs in behind the char");
static_assert(sizeof(E)  ==  5, "':0' moves the next field to the int boundary");
static_assert(sizeof(EA) ==  6, "and does not widen the class");
static_assert(sizeof(F)  ==  4, "");
static_assert(sizeof(G)  ==  4, "");
#endif

int main() {
    A a; a.a = 3; a.b = 1;
    G g; g.a = 1000; g.b = 100;
    printf("%d %d %d %d\n", (int)a.a, (int)a.b, (int)g.a, (int)g.b);
    return 0;
}
