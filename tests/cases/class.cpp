// `class` and access control - the first step of rung 3.
//
// A class and a struct build the same type and differ in one thing: where
// access starts. Everything below is laid out identically to the struct
// beside it, which is what `sizeof` reports.
extern "C" { int printf(const char *, ...); }

class Point {
public:
    int x;
    int y;
private:
    int hidden;
public:
    int visible;
};

// The same members written as a struct, to show the layout is the same one.
struct Plain { int x; int y; int hidden; int visible; };

// A class may say public first and then it reads exactly like a struct.
class Pair {
public:
    int a;
    int b;
};

int main(void) {
    Point p;
    Plain q;
    Pair r;
    p.x = 3; p.y = 4; p.visible = 7;
    q.x = 1; q.y = 2; q.hidden = 5; q.visible = 6;
    r.a = 8; r.b = 9;
    printf("%d %d %d\n", p.x, p.y, p.visible);
    printf("%d %d %d %d\n", q.x, q.y, q.hidden, q.visible);
    printf("%d %d\n", r.a, r.b);
    printf("%d %d\n", (int)sizeof(Point), (int)sizeof(Plain));
    return 0;
}
