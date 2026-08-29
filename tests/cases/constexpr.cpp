// `constexpr` on a variable, which is `const` plus a demand: the initialiser
// must be a constant expression, where a plain `const` may take whatever it
// is given and simply not be usable as a constant afterwards.
//
// A `constexpr` *function* is refused by name - see constexpr-function.cpp -
// so nothing here calls one.
//
// The names are chosen to avoid MASM's reserved words. `width` is one, and an
// identifier that collides with one is emitted as `$width` by the MASM
// backend - deliberately, and invisibly to a program, since the escape stops
// at the import boundary. It is visible to tests/names.sh, which would then
// report a difference that is about the assembler rather than about
// `constexpr`.
extern "C" int printf(const char *, ...);

constexpr int span = 4;
constexpr int area = span * span;
const int borrowed = area / 2;

struct Board { static constexpr int side = span + 1; };

int grid[area];
int edge[Board::side];

int main() {
    constexpr int local = area + borrowed;
    int scratch[local];
    printf("%d %d %d %d\n", span, area, borrowed, Board::side);
    printf("%d %d %d\n", (int)(sizeof grid / sizeof grid[0]),
                         (int)(sizeof edge / sizeof edge[0]),
                         (int)(sizeof scratch / sizeof scratch[0]));

    // A constexpr object is still an object - see named-constant.cpp, which
    // says the same thing about a plain const and for the same reason.
    const int *pw = &span;
    const int *pa = &area;
    const int *pb = &borrowed;
    printf("%d %d %d\n", *pw, *pa, *pb);
    return 0;
}
