// `Point p();` declares a function taking nothing and returning a Point. That
// is what C++ reads it as, and a compiler that quietly built an object here
// would compile a program that means something else everywhere else.
class Point {
public:
    Point();
    int x;
};
Point::Point() { x = 1; }

int main(void) {
    Point p();
    return 0;
}
