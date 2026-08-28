// Virtual functions, first slice: the table is emitted and the vptr is set.
// **Nothing dispatches through it yet** - a call to a virtual function is
// refused by name until the next step, because a static call would be right
// whenever the static type happened to be the dynamic one and silently wrong
// otherwise.
//
// What this case pins is the layout, which is the half that is easy to get
// wrong and impossible to see: a polymorphic object carries a vptr at offset 0
// and its members start after it, and a derived class puts its own members in
// the base's TAIL PADDING rather than after its sizeof. Base is {vptr, int} -
// twelve bytes of data padded to sixteen - so Derived's int lands at twelve
// and the whole thing is sixteen, not twenty-four.
extern "C" { int printf(const char *, ...); }

class Base {
public:
    Base();
    virtual int who();
    virtual int how();
    int b;
};
class Derived : public Base {
public:
    Derived();
    virtual int who();
    int d;
};

Base::Base() { b = 1; }
int Base::who() { return 1; }
int Base::how() { return 2; }
Derived::Derived() { d = 3; }
int Derived::who() { return 4; }

int main(void) {
    Derived x;
    Base y;
    printf("%d %d\n", (int)sizeof(Base), (int)sizeof(Derived));
    printf("%d %d\n", x.b, x.d);
    printf("%d\n", y.b);
    return 0;
}
