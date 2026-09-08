// **A virtual base is built by the most-derived constructor, with the arguments
// that constructor's own mem-initialiser list gives it** - [class.base.init]/7.
// Both halves were missing and both failed silently.
//
// C2, the base-subobject form, skips virtual bases because they are not its to
// build; C1, the complete form, built them - and only ever asked for a *default*
// constructor. So `L() : B(7)` ran `B()` and dropped the 7, and where `B` had no
// default constructor at all it built nothing and left the subobject holding
// whatever the stack did. `virtual-base-diamond` did not catch it because its
// classes have no data: it counts constructor calls, and the call it counted was
// the wrong one.
//
// The arguments are parsed in C2's scope and emitted in C1's body, so C1's frame
// is laid out to match C2's slot for slot - arguments before `this`, which is the
// order topLevel declares them in and not the order the calling convention lists
// them in.
//
// The last line is the rule itself: `Dia` names `V(v)`, and A's and C's
// `V(v + 10)` and `V(v + 20)` do not run, however loudly they are written.
extern "C" int printf(const char *, ...);

struct B {
    int n;
    B(int v) : n(v) { printf("B%d ", v); }
    ~B() { printf("~B%d ", n); }
};

struct L : virtual B { L() : B(1) {} };
struct D : L        { D() : B(3) {} };

struct Two {
    int n;
    Two() : n(99) { printf("Two() "); }
    Two(int v) : n(v) { printf("Two%d ", v); }
};
struct HasTwo : virtual Two { HasTwo() : Two(7) {} };

struct V {
    int n;
    V(int v) : n(v) { printf("V%d ", v); }
    ~V() { printf("~V%d ", n); }
};
struct A   : virtual V { A(int v) : V(v + 10) {} };
struct C   : virtual V { C(int v) : V(v + 20) {} };
struct Dia : A, C      { Dia(int v) : V(v), A(v), C(v) {} };

int main() {
    // A direct virtual base whose only constructor takes an argument.
    { L l; printf("| %d\n", l.n); }
    // The same base reached through a non-virtual one: D lays V down, not L.
    { D d; printf("| %d\n", d.n); }
    // A base that *has* a default constructor still takes the one named.
    { HasTwo h; printf("| %d\n", h.n); }
    // The most-derived class wins, and the base is built exactly once.
    { Dia d(5); printf("| %d ", d.n); }
    printf("\n");
    // And the parameter reaches it: A alone is most-derived here, so V(11).
    { A a(1); printf("| %d ", a.n); }
    printf("\n");
    return 0;
}
