// **What a Microsoft `:0` does depends on what it interrupts.** One that
// terminates an open bitfield unit charges the unit for whole, aligns the
// next member to its own declared type, and gives the class that alignment
// too; one that follows a plain member, or nothing at all, does none of
// that. bitfield-layout.cpp pins the second half - `{char a; int :0; char
// b;}` is 2 bytes aligned 1 - and cxx1 took it for the whole rule, so a
// `:0` after an open unit still answered as Itanium: `{short a:5; int :0;
// short b:5;}` came out 4 bytes aligned 2 where cl says 8 aligned 4, and
// `{long long a:33; char :0; char b;}` put b at 5 where cl puts it at 8,
// past the whole unit the `:0` closed.
//
// On Itanium a `:0` only rounds the cursor to its own type's next unit and
// never touches the class's alignment, whichever member came before - that
// was right already, and every number here holds it down too. Every number
// on both sides was measured with clang for the ABI it sits under.
extern "C" int printf(const char *, ...);

struct K { long long a:33; char :0; char b; };      // closes a long long unit
struct L { short a:5; int :0; short b:5; };         // the :0 wider than both
struct O { short a:5; int :0; };                    // trailing, still aligns
struct Q { char a:3; short :0; char b:3; };         // wider than the unit
struct Z2 { long long a:33; char :0; long long b:10; }; // next field re-opens
struct Z4 { char a:3; char :0; char b:3; };         // same type as its unit
struct Z6 { short a:5; int :0; int :0; char b; };   // the second :0 is idle
struct Z7 { char a; long long :0; char b; };        // after a plain member
struct PK { char p; K x; }; struct PL { char p; L x; };
struct PO { char p; O x; }; struct PQ { char p; Q x; };

#ifdef _WIN32
static_assert(sizeof(K)  == 16, "b sits at 8, past the whole closed unit");
static_assert(sizeof(L)  ==  8, "b sits at 4, where the int:0 aligned it");
static_assert(sizeof(O)  ==  4, "a trailing :0 still pads and aligns");
static_assert(sizeof(Q)  ==  4, "b at 2: the short:0 aligned it");
static_assert(sizeof(Z2) == 16, "b re-opens a long long unit at 8");
static_assert(sizeof(Z4) ==  2, "char:0 closes a char unit at byte 1");
static_assert(sizeof(Z6) ==  8, "a :0 with no open unit does nothing");
static_assert(sizeof(Z7) ==  2, "after a plain member it does nothing at all");
static_assert(sizeof(PK) == 24, "K is aligned 8");
static_assert(sizeof(PL) == 12, "the int:0 is why L is aligned 4");
static_assert(sizeof(PO) ==  8, "and O");
static_assert(sizeof(PQ) ==  6, "the short:0 is why Q is aligned 2");
#else
static_assert(sizeof(K)  ==  8, "b sits at 5, the next char boundary");
static_assert(sizeof(L)  ==  6, "b at 4, and the int:0 adds no alignment");
static_assert(sizeof(O)  ==  4, "the cursor still moves to the int boundary");
static_assert(sizeof(Q)  ==  3, "b at 2, aligned by the short:0's rounding");
static_assert(sizeof(Z2) ==  8, "b packs in at bit 40 of the same unit");
static_assert(sizeof(Z4) ==  2, "b at the next char boundary, byte 1");
static_assert(sizeof(Z6) ==  6, "b at 4; the second :0 has nothing to round");
static_assert(sizeof(Z7) ==  9, "b at 8: Itanium rounds after a plain member too");
static_assert(sizeof(PK) == 16, "K is aligned 8 by its long long");
static_assert(sizeof(PL) ==  8, "L stays aligned 2 - no alignment from the :0");
static_assert(sizeof(PO) ==  6, "and O");
static_assert(sizeof(PQ) ==  4, "Q stays aligned 1");
#endif

K k; L l; Q q; Z2 z2;

int main() {
    // Stores land where the layout says, and a full unit on one side of a
    // `:0` never reaches the field on the other side.
    k.b = 42; k.a = -1;
    l.b = 13; l.a = -1; l.a = 5;
    q.a = -1; q.b = 3;
    z2.a = -1; z2.b = 200;
    printf("%d %lld %d %d %d %d %lld %lld\n",
           (int)k.b, k.a, (int)l.a, (int)l.b, (int)q.a, (int)q.b,
           (long long)z2.a, (long long)z2.b);
    return 0;
}
