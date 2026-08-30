// An operator inherited from a base, and overloads ranked across a hierarchy.
// The member half of the candidate set is found up the base chain, exactly as
// a member function call is - so a Derived reaches Base::operator+ - while the
// non-member half ranks a Derived argument against Base and Derived
// parameters, where the derived one is the better match.
extern "C" { int printf(const char *, ...); }

class Base {
public:
    int b;
    Base(int n) { b = n; }
    int operator+(int n) const { return 1 + 0 * n; }
};

class Derived : public Base {
public:
    Derived(int n) : Base(n) { }
};

int take(const Base &x)    { return 2 + 0 * x.b; }
int take(const Derived &x) { return 3 + 0 * x.b; }

int main(void) {
    Derived d(1);
    Base bb(2);
    // the member operator comes down from Base
    printf("%d\n", d + 1);
    // a Derived argument prefers the Derived parameter
    printf("%d %d\n", take(d), take(bb));
    return 0;
}
