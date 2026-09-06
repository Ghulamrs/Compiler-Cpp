// A reference to a multi-dimensional array binds without decay, and only the
// innermost element may gain const - [dcl.init.ref]/5 with [conv.qual].
//
// The pointee of a `const double[2][3]` is a `const double[3]`, which no
// `double[3]` ever equals, so comparing one level matched nothing: the ranking
// said no candidate was viable, and once that was mended the binding refused
// what the ranking had just accepted. Both walk every dimension now, and the
// element under the last one decides. `describe()` also wrote the dimensions
// innermost first - `double [3] [2]` for a `double[2][3]` - which the refused
// neighbour, array-reference-multidim-const-refused.cpp, pins the right way.
extern "C" int printf(const char *, ...);

static int two(const double (&a)[2][3]) { return (int)(a[1][2] * 10); }
static int three(int (&a)[2][2][2]) { a[1][1][1] = 80; return a[0][1][0]; }
static int pick(const int (&a)[2][2][2]) { return a[1][1][1]; }

struct M {
    double cells[2][3];
    explicit M(const double (&a)[2][3]) {
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 3; j++) cells[i][j] = a[i][j];
    }
};

int main(void) {
    double r2[2][3] = {{1, 2, 3}, {4, 5, 6}};
    const double c2[2][3] = {{7, 8, 9}, {1, 2, 3}};
    int cube[2][2][2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
    M m(r2);
    // **The write is a statement of its own, and that is not tidiness.**
    // `three` stores through its parameter and `pick` reads what it stored;
    // written as two arguments of one call the order between them is
    // unspecified, so the Itanium targets printed 80 and x86_64-windows
    // printed 8 and both were right. CLAUDE.md records the same trap from
    // lambda-capture-this, and the Windows box is what caught it again.
    const int wrote = three(cube);
    const int read = pick(cube);
    printf("%d %d %d %d %d\n", two(r2), two(c2), wrote, read,
           (int)m.cells[1][0]);
    return 0;
}
