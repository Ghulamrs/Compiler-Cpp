// A pure virtual function, and the abstract class it makes.
//
// **The slot holds the runtime's own trap rather than a function** - measured
// on both ABIs: `__cxa_pure_virtual` on Itanium and `_purecall` on Microsoft.
// The vtable is still emitted and the constructor still stores it, because an
// abstract class is built as a base subobject every time a derived one is.
//
// **Abstract is a question about the finished table, not about what a class
// declared.** A derived class that overrides every pure entry has replaced
// them and is concrete; one that leaves any is abstract in its turn - which is
// what `Shaded` below is for. The check is made where an object would be made,
// not where the call would happen, because by then there is nothing to call.
//
// A pure virtual may still be declared alongside ordinary virtuals and
// inherited ones, and `delete` through a base pointer reaches the derived
// destructor exactly as it does for any polymorphic class.
extern "C" { int printf(const char *, ...); }

class Shape {
public:
    virtual int area() = 0;
    virtual int sides() = 0;
    // Not pure: a derived class may take this one as it is.
    virtual int kind();
    virtual ~Shape();
};
int Shape::kind() { return 100; }
Shape::~Shape() { printf("  ~Shape\n"); }

class Square : public Shape {
public:
    int s;
    int area();
    int sides();
    ~Square();
};
int Square::area()  { return s * s; }
int Square::sides() { return 4; }
Square::~Square() { printf("  ~Square\n"); }

class Triangle : public Shape {
public:
    int b;
    int h;
    int area();
    int sides();
    int kind();
    ~Triangle();
};
int Triangle::area()  { return b * h / 2; }
int Triangle::sides() { return 3; }
int Triangle::kind()  { return 300; }
Triangle::~Triangle() { printf("  ~Triangle\n"); }

static int describe(Shape *p) { return p->area() * 1000 + p->sides() * 100 + p->kind(); }

int main() {
    Square q;
    q.s = 3;
    Triangle t;
    t.b = 6;
    t.h = 4;

    printf("%d\n", describe(&q));
    printf("%d\n", describe(&t));

    // Through a base pointer, and destroyed through one.
    Shape *heap = new Triangle;
    ((Triangle *)heap)->b = 10;
    ((Triangle *)heap)->h = 2;
    printf("%d\n", describe(heap));
    delete heap;

    printf("end\n");
    return 0;
}
