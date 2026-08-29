// A class with no data members, passed by value.
//
// The SysV ABI calls an eightbyte that no member reaches NO_CLASS, and an
// empty class is entirely that: it takes **no register at all**. Measured
// against clang, which puts the 7 of `take2(E{}, 7)` in %edi - so the class
// consumed nothing and the int went in the first slot.
//
// cxx1's x86_64 lanes started out SSE and only a non-floating member cleared
// one, which said nothing about a lane no member covered. An empty class kept
// them all, went in xmm0 through a `movss` - four bytes read out of a
// one-byte object - and segfaulted on the Linux box. arm64 and Windows
// classify differently and were unaffected, which is the shape most bugs here
// take.
//
// `takeMany` is the case that pins it: with the empty class between other
// arguments, getting it wrong moves every argument after it into the wrong
// register. Settled by a link rather than a diff - objects cxx1 compiled link
// with g++'s in both directions and print what the all-g++ build prints.
extern "C" { int printf(const char *, ...); }

struct E { };
struct AlsoEmpty { typedef int type; };

int take2(E e, int n) { (void)e; return n * 10; }
int takeMany(int a, E e, double d, int b) { (void)e; return a + (int)d + b; }
int typedefOnly(AlsoEmpty e, int n) { (void)e; return n + 1; }

int main() {
    E x;
    AlsoEmpty y;
    printf("%d %d %d\n", take2(x, 7), takeMany(1, x, 2.0, 3), typedefOnly(y, 5));
    printf("%d %d\n", (int)sizeof(E), (int)sizeof(AlsoEmpty));
    return 0;
}
