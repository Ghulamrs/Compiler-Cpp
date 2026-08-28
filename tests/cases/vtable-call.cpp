// Dispatch is the next step. Until it exists a call to a virtual function is
// refused, rather than compiled as a static call that would be right by
// accident whenever the static type matched the dynamic one.
class Shape {
public:
    Shape();
    virtual int sides();
};
Shape::Shape() {}
int Shape::sides() { return 0; }

int main(void) {
    Shape s;
    return s.sides();
}
