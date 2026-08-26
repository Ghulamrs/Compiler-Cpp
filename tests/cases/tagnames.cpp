// The C library is named the way C names it, so its declarations go
// inside a linkage specification. Without it these are C++ names and
// the linker is asked for symbols libc has never had.
extern "C" {
int printf(const char *, ...);
}

struct Point { int x; int y; };
typedef struct Point Point;          // redundant, and legal: same type
union Word { int i; char b[4]; };
enum Colour { Red, Green, Blue };

struct Point origin;

int sum(Point p) { return p.x + p.y; }

int main(void) {
    Point p;
    p.x = 3;
    p.y = 4;

    Point *q = &p;
    q->y = 10;

    Word w;
    w.i = 0;

    Colour c = Green;

    origin.x = 1;

    printf("%d %d %d %d\n", sum(p), (int)c, origin.x, (int)sizeof(Point));
    return 0;
}
