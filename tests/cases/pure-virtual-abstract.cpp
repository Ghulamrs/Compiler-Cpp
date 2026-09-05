// An object of an abstract class cannot exist, and the refusal names the
// function that has no implementation - which is the thing the reader has to
// write. It is refused where the object would be made rather than where the
// call would happen, because by then there is nothing left to say.
//
// **`Shaded` is the case worth having**: it derives from an abstract class and
// overrides one of its two pure virtuals, so it is abstract in its turn. That
// is why the question is asked of the finished vtable rather than of what the
// class itself declared - `Shaded` declares no pure virtual at all and is
// abstract all the same.
class Shape {
public:
    virtual int area() = 0;
    virtual int sides() = 0;
    virtual ~Shape();
};
Shape::~Shape() {}

class Shaded : public Shape {
public:
    int sides();
};
int Shaded::sides() { return 0; }

int main() {
    Shaded s;
    return 0;
}
