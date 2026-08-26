// What the linker is told. Every function here has C++ linkage and so a
// mangled name, except the ones inside the linkage specification, which keep
// the names C gave them - and main, which keeps its own by rule.
//
// tools/mangled-names is what checks those names against clang for all three
// ABIs. This case checks the other half: that the compiler agrees with itself
// well enough to link and run.
extern "C" {
int printf(const char *, ...);
}

struct Point { int x; int y; };

int plain(int a, int b) { return a + b; }
static int internal(int n) { return n * 2; }
int byPointer(const Point *p) { return p->x; }
int byReference(const Point &p) { return p.y; }
int counted(const char *text, int n) { return (int)text[0] + n; }
long widths(long a, unsigned long b, long long c) { return a + (long)b + (long)c; }

extern "C" int cLinkage(int n) { return n + 1; }

extern "C" {
int alsoC(int n) { return n + 2; }
}

int shared;

int main(void) {
    Point p;
    p.x = 3;
    p.y = 4;
    shared = plain(1, 2);
    printf("%d %d %d %d\n", shared, internal(shared), byPointer(&p), byReference(p));
    printf("%d %ld\n", counted("A", 1), widths(1, 2, 3));
    printf("%d %d\n", cLinkage(10), alsoC(10));
    return 0;
}
