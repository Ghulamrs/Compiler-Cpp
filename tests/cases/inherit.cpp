// Single inheritance - the first step of rung 4.
//
// The base subobject sits at offset 0, measured, so a derived object's address
// IS its base's address and no pointer adjustment happens anywhere. That is
// what multiple inheritance ends, and why it is a later step.
//
// The trace is the point of the second half: a constructor runs the base's
// first and a destructor runs it last. Those calls go to the base's C2 and D2 -
// the base-object forms - which is what those two names have been emitted for
// since constructors landed, and the first time anything calls them.
extern "C" { int printf(const char *, ...); }

class Base {
public:
    Base();
    ~Base();
    int fromBase();
    int b;
};
Base::Base() { b = 1; printf("Base up\n"); }
Base::~Base() { printf("Base down\n"); }
int Base::fromBase() { return b * 10; }

class Derived : public Base {
public:
    Derived();
    ~Derived();
    int sum();
    int d;
};
Derived::Derived() { d = 2; printf("Derived up\n"); }
Derived::~Derived() { printf("Derived down\n"); }
// An inherited member function is found by walking up, and an inherited data
// member was copied down by the layout - the two are asymmetric on purpose.
int Derived::sum() { return fromBase() + d + b; }

int main(void) {
    Derived x;
    printf("%d %d %d %d\n", x.b, x.d, x.fromBase(), x.sum());
    printf("%d %d\n", (int)sizeof(Base), (int)sizeof(Derived));
    return 0;
}
