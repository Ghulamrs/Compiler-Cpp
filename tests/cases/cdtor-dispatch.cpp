// **[class.cdtor]/4: a virtual call from a constructor or a destructor
// reaches the final overrider in *that* function's class.** The object is
// what the level currently running built, and no more than that.
//
// Constructors stored the vptr as each level ran and destructors never did,
// so on the way down the object still claimed to be the most derived thing it
// had been: during `~A` a virtual call ran C's override, against subobjects C
// had already finished destroying. The store goes in front of the destructor
// body now, and the base's destructor is called after it - which is the order
// the standard fixes and the mirror of what constructors already did.
extern "C" int printf(const char *, ...);

struct A {
    virtual const char *who() { return "A"; }
    A()          { printf("+%s ", who()); }
    virtual ~A() { printf("-%s ", who()); }
};
struct B : A {
    const char *who() { return "B"; }
    B()  { printf("+%s ", who()); }
    ~B() { printf("-%s ", who()); }
};
struct C : B {
    const char *who() { return "C"; }
    C()  { printf("+%s ", who()); }
    ~C() { printf("-%s ", who()); }
};

int main() {
    { C c; }
    printf("| ");
    { A *p = new C; delete p; }      // and through a base pointer
    printf("\n");
    return 0;
}
