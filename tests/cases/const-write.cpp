// The object here is not const - what it points at is. Only the type records
// that, which is why const has to live in the type system rather than on the
// declaration.
struct Point { int x; int y; };

int main(void) {
    struct Point p;
    p.x = 1; p.y = 2;
    const struct Point *view = &p;
    view->x = 3;
    return p.x;
}
