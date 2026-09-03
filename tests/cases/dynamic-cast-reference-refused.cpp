// `dynamic_cast<T &>(r)`, refused by name.
//
// The pointer form has a null to answer with when the object is not a T. A
// reference has no such value, so [expr.dynamic.cast]/9 says a failure throws
// `std::bad_cast` - and that is a class from the C++ standard library, of which
// this compiler has none. The refusal says so, and points at the form that
// works: the whole of the Compiler++ tree's 354 sites are the pointer one.
struct Base { virtual ~Base(); virtual int who(); };
struct Leaf : Base { int who(); };
Base::~Base() {}
int Base::who() { return 0; }
int Leaf::who() { return 1; }

int main(void) {
    Leaf l;
    // Through a pointer, because binding `Base &b = l;` is a separate gap in
    // this compiler and would stop this case before it reached the cast.
    Base *p = &l;
    return dynamic_cast<Leaf &>(*p).who();
}
