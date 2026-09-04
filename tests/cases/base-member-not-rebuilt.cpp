// **A base's members are in the derived class's list too**, the layout having
// copied them down - and the base's own constructor has already built them by
// the time the derived constructor's body runs. Building them again default-
// constructs *over* what the base set.
//
// So a base whose constructor does work through a member with a constructor
// was correct when built alone and wrong the moment anything derived from it:
//
//     Base b;      // cur holds what Base's constructor put there
//     Derived d;   // cur holds its default-constructed state
//
// The destructor walk has skipped base members since implicit destructors
// landed - `memberFromBase` is that test, and the comment beside it says a
// base's own destructor deals with what it brought. Neither constructor walk
// asked, and there are two: the one that synthesises an implicit default
// constructor and the one that fills in what a user-written constructor's
// initialiser list left out.
//
// It is what made cxx1's build of Compiler++ reject every program: its parser
// derives from a base whose constructor reads the first token into a member,
// and that member was default-constructed again straight afterwards.
extern "C" int printf(const char *, ...);

int built = 0;

// A member with a constructor of its own, which is what makes the difference:
// a plain `int` has nothing to rebuild and hid this for a long time.
struct Cell {
    int v;
    Cell();
};
Cell::Cell() : v(-1) { built++; }

struct Base {
    Cell cur;
    int plain;
    virtual ~Base();
    Base();
    void fill();
};
Base::Base() : plain(0) { fill(); }
Base::~Base() {}
void Base::fill() { cur.v = 7; plain = 8; }

// A user-written constructor that names neither the base nor `cur`.
struct Derived : Base {
    int extra;
    Derived();
};
Derived::Derived() : extra(5) {}

// And one with no constructor of its own, which takes the other walk.
struct Implicit : Base {
    Cell own;
};

int main() {
    Base b;
    printf("base     %d %d\n", b.cur.v, b.plain);
    Derived d;
    printf("derived  %d %d %d\n", d.cur.v, d.plain, d.extra);
    Implicit i;
    printf("implicit %d %d %d\n", i.cur.v, i.plain, i.own.v);
    // Each `Cell` is built exactly once: three in Base's three subobjects and
    // one more for Implicit's own.
    printf("cells    %d\n", built);
    return 0;
}
