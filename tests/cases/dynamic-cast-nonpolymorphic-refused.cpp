// `dynamic_cast` on a class with no virtual function, refused by name.
//
// [expr.dynamic.cast]/6. The answer is read out of the object, and what it is
// read from is the vtable pointer - so a class without one carries nothing that
// says what it really is, and there is no question to ask. The refusal names
// the class rather than the cast, because that is the half the reader can fix.
struct Plain { int v; };
struct Derived : Plain { int w; };

int main(void) {
    Derived d;
    Plain *p = &d;
    return dynamic_cast<Derived *>(p) != 0;
}
