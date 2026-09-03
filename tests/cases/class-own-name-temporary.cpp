// `S(3)` written inside a member of `S` is a temporary, not a call.
//
// **A class's own name inside it is not a member function.** [class.qual]/2:
// the injected-class-name names the class, and `S(3)` is a temporary built from
// it. But a constructor's entry in this compiler's function table is keyed
// `S::S`, which is exactly what an unqualified name looked up inside the class
// finds - so `return S(3);` was dispatched as a member call on `this` and
// reported that the constructor "is not a const member function", or, in a
// non-const member, that the function returned void.
//
// It is worth a case rather than a comment because nothing in the corpus wrote
// it: a class that builds one of itself inside its own members is the shape of
// every `substr`, and there was no such class until a string was written.

extern "C" int printf(const char *, ...);

struct S {
    int v;
    S() : v(0) {}
    S(int x) : v(x) {}
    S plain(void) { return S(3); }                       // non-const member
    S constly(void) const { return S(v + 4); }           // const member
    S nested(void) const { return S(constly().v * 2); }  // and one inside another
};

struct Base {
    int v;
    Base() : v(0) {}
    Base(int x) : v(x) {}
};

struct Derived : Base {
    Derived() {}
    // The base's name from inside the derived class, which is the same lookup
    // reaching one rung further up.
    Base fromDerived(void) const { return Base(9); }
    Derived ownName(void) const { return Derived(); }
};

int main(void) {
    S s;
    s.v = 1;
    Derived d;
    printf("%d %d %d\n", s.plain().v, s.constly().v, s.nested().v);
    printf("%d %d\n", d.fromDerived().v, d.ownName().v);
    return 0;
}
