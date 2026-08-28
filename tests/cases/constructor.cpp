// Constructors - the third step of rung 3.
//
// A constructor is a member function whose name is its class and whose return
// type is nothing, so it is keyed as "Point::Point" and every piece of the
// overload machinery applies to it unchanged: Point() and Point(int,int) are
// two entries that a construction chooses between by its arguments.
//
// The object exists before the call - it is a frame slot like any other local
// - and the constructor's job is to give its members values.
extern "C" { int printf(const char *, ...); }

class Point {
public:
    Point();
    Point(int a, int b);
    Point(int both);
    int sum() const;
private:
    int x;
    int y;
};

Point::Point() { x = 0; y = 0; }
Point::Point(int a, int b) { x = a; y = b; }
Point::Point(int both) { x = both; y = both; }
int Point::sum() const { return x + y; }

// A class may keep a constructor to itself and hand out objects through a
// member function, which is the usual reason for a private one.
class Counted {
public:
    int value() const;
    int build(int n);
private:
    int held;
};
int Counted::value() const { return held; }
int Counted::build(int n) { held = n; return held; }

int main(void) {
    Point origin;
    Point p(3, 4);
    Point both(5);
    Counted c;
    c.build(9);
    printf("%d %d %d %d\n", origin.sum(), p.sum(), both.sum(), c.value());
    return 0;
}
