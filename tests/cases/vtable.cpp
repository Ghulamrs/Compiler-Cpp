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


// **The size below depends on the ABI, so it is pinned at compile time
// instead of printed.** Itanium lets a derived class into a base's tail
// padding when the base is not a POD; the Microsoft ABI never does. One
// `.expected` cannot hold both answers, and a static_assert is the better
// home anyway: emit.sh checks it for all three targets from whichever box is
// running, where a printed number is only ever checked on the host.
static_assert(sizeof(Base) == 16, "vptr and an int, padded to the vptr");
#ifdef _WIN32
static_assert(sizeof(Derived) == 24, "cl gives Derived::d its own word");
#else
static_assert(sizeof(Derived) == 16, "Itanium puts Derived::d in Base's padding");
#endif

int main(void) {
    Derived x;
    Base y;
    printf("%d %d\n", x.b, x.d);
    printf("%d\n", y.b);
    return 0;
}
