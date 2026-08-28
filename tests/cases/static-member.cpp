// A static data member: one object shared by the class rather than one per
// object of it.
//
// It takes no room in the layout - the class below is four bytes whatever the
// static members are - and it is a global that the class gave its name to.
// Both ABIs spell that name, and **only Microsoft spells in the access**, as a
// digit where a member function writes a letter: 2 public, 1 protected, 0
// private, measured with cl. Itanium writes _ZN1C3pubE whether it is public or
// private, exactly as it declines to record the access of a member function.
//
// `static const int` with its value written inside the class needs no
// definition at all: cl emits no symbol for one and folds the value in, so the
// `cap` below has no `int Bank::cap = ...` line anywhere and is still readable.
//
// A static member is searched *up* through the bases, the way a member
// function is - it lives under a name and not at an offset, so nothing is
// copied down into the derived class.
extern "C" { int printf(const char *, ...); }

struct Base {
    static int shared;
    int b;
};
int Base::shared = 10;

struct Derived : Base {
    static int mine;
    int d;
};
int Derived::mine = 20;

class Bank {
public:
    static int rate;
    static double factor;
    static const int cap = 7;      // folded: no definition, no symbol
    static int table[3];
    static Bank *last;
    int id;

    int bump();
    int viaThis();
};
int    Bank::rate = 1;
double Bank::factor = 2.5;
int    Bank::table[3] = { 4, 5, 6 };
Bank  *Bank::last = 0;

// Unqualified inside a member function: a static member needs no object, so
// it is found by name rather than through `this`.
int Bank::bump()    { rate = rate + 1; return rate + cap + table[2]; }
int Bank::viaThis() { return id + rate; }

int main() {
    printf("%d\n", (int)sizeof(Bank));

    Derived d;
    d.d = 1;
    printf("%d %d %d\n", Base::shared, Derived::shared, Derived::mine);

    Bank b;
    b.id = 3;
    Bank *p = &b;

    printf("%d %d %d %d\n", Bank::rate, (int)Bank::factor, Bank::cap,
                            Bank::table[1]);

    // The same object, named three ways.
    printf("%d %d %d\n", Bank::rate, b.rate, p->rate);
    b.rate = 41;
    printf("%d %d %d\n", Bank::rate, b.rate, p->rate);

    printf("%d\n", b.bump());
    printf("%d\n", b.viaThis());

    // Its address is one address, whichever way it was reached.
    int *viaClass  = &Bank::rate;
    int *viaObject = &b.rate;
    printf("%d\n", viaClass == viaObject ? 1 : 0);

    Bank::last = &b;
    printf("%d\n", Bank::last->id);

    // Shared: writing through one object is visible from another.
    Bank other;
    other.rate = 99;
    printf("%d %d\n", b.rate, Bank::rate);
    return 0;
}
