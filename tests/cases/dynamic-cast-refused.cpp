// `dynamic_cast`, refused by name.
//
// It asks what an object *actually* is, which only a type_info object beside
// its vtable can answer - and this compiler emits none for a class, on any of
// the three targets. So this is a rung of its own rather than a missing branch
// beside the other three casts: the work is emitting the type_info, the
// inheritance graph it carries, and the `__cxa_...` call that walks it.
//
// The direction that needs none of that - casting *to* a base, where the
// answer is known while compiling - is `static_cast`, and it works.
struct Shape {
    virtual ~Shape(void) {}
    int tag;
};

struct Circle : Shape {
    int radius;
};

int main(void) {
    Circle c;
    c.radius = 3;
    Shape *s = &c;
    Circle *back = dynamic_cast<Circle *>(s);
    return back != nullptr ? back->radius : 0;
}
