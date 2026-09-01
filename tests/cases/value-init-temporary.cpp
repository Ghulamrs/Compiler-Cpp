// **[dcl.init]/8: `T()` value-initialises**, and for a class with no
// user-provided constructor that means zero-initialising it. cxx1 handed back
// the frame slot as it stood - "an object with nothing to set" was the wrong
// reading of a class that has no constructor to run, which is exactly why the
// zeroing is the compiler's job.
//
// The frame is dirtied first, because the fault was invisible on a clean
// stack: it read whatever happened to be there, reproducibly and differently
// per call site. The last field checks the dirtying itself still happened, so
// a compiler that optimised the array away could not pass by accident.
//
// A class that *does* have a constructor is here for the other half of the
// rule: it runs, and nothing is zeroed behind it.
extern "C" int printf(const char *, ...);

struct P { int a; char b; double d; };
struct Nested { P p; int arr[3]; };
struct WithCtor { int v; WithCtor() { v = 9; } };

int f(P p)      { return p.a + p.b + (int)p.d; }
int g(Nested n) { return n.p.a + n.arr[0] + n.arr[1] + n.arr[2]; }

int main() {
    char junk[64];
    for (int i = 0; i < 64; i++) junk[i] = (char)0x5A;

    printf("%d %d %d %d\n", f(P()), g(Nested()), WithCtor().v,
           (int)(junk[0] == 0x5A));
    return 0;
}
