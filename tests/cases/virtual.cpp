// Virtual dispatch - the second slice, and what the table was for.
//
// `viaPointer` is handed three different classes through one Shape * and gets
// three different answers, which is the whole of what virtual means. Square
// overrides sides() and inherits corners(), so it answers 400: its own 4 from
// the slot it replaced, and Shape's 0 from the slot it did not.
//
// The call reads the slot rather than naming the function: the object's first
// word is the vptr, the slot is at a fixed index - the same index in every
// class in the chain, which is what the table's ordering bought - and from
// there it is an ordinary indirect call.
//
// It also needs Derived * to convert to Base *, which costs nothing at run
// time because the base subobject sits at offset 0.
extern "C" { int printf(const char *, ...); }
class Shape {
public:
    Shape();
    virtual int sides();
    virtual int corners();
    int name;
};
class Square : public Shape {
public:
    Square();
    virtual int sides();
};
class Triangle : public Shape {
public:
    Triangle();
    virtual int sides();
    virtual int corners();
};
Shape::Shape() { name = 0; }
int Shape::sides() { return 0; }
int Shape::corners() { return 0; }
Square::Square() { name = 4; }
int Square::sides() { return 4; }
Triangle::Triangle() { name = 3; }
int Triangle::sides() { return 3; }
int Triangle::corners() { return 3; }

int viaPointer(Shape *p) { return p->sides() * 100 + p->corners(); }

int main(void) {
    Shape s;
    Square q;
    Triangle t;
    printf("%d %d %d\n", viaPointer(&s), viaPointer(&q), viaPointer(&t));
    printf("%d %d\n", q.sides(), t.corners());
    return 0;
}
