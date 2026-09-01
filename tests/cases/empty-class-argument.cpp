// **Apple's arm64 ABI ignores an empty class in the parameter list**, and
// `sizeof` being 1 is not the question. cxx1 gave one a register and shifted
// every argument after it along - consistent with itself, and wrong against
// anything clang compiled, so only a mixed link showed it.
//
// The three shapes are here because the rule is about the class and not its
// size: Empty and an empty *base* are ignored, and Wrap - whose only member
// is an empty class - is not. Measured with clang: `take(Empty, int x, int y)`
// puts x in w0 and `take(Wrap, int x, int y)` puts x in w1.
//
// The last one puts an empty class in the middle of the list, where getting
// the rule wrong shifts one argument and not the others.
extern "C" int printf(const char *, ...);

struct Empty { };
struct Wrap { Empty e; };
struct Derived : Empty { };

int viaEmpty(Empty a, int x, int y)     { return x * 10 + y; }
int viaWrap(Wrap a, int x, int y)       { return x * 10 + y; }
int viaDerived(Derived a, int x, int y) { return x * 10 + y; }
int mixed(int p, Empty a, int q)        { return p * 100 + q; }

int main() {
    Empty e;
    Wrap w;
    Derived d;
    printf("%d %d %d %d\n", viaEmpty(e, 1, 2), viaWrap(w, 3, 4),
           viaDerived(d, 5, 6), mixed(7, e, 8));
    return 0;
}
