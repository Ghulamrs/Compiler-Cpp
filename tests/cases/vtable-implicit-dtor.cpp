// **A vtable slot holding a function's address is a use of that function.**
// The `used` flag came only from calls, so a class whose virtual destructor is
// implicit - `struct D : B { };` where B's destructor is virtual - got a table
// pointing at `~D` and no `~D` emitted anywhere. Nothing in the program named
// it, and nothing had to: `delete p` through a `B *` reaches it through the
// slot. The link failed with a symbol not found, on a program rung 4 says
// works.
//
// Marked when the table is emitted, which is during the class's own
// completion and well before the implicit bodies are walked. E is here for
// the other half: an implicit destructor beside a written override, so the
// table has one of each and both have to arrive.
extern "C" int printf(const char *, ...);

struct B {
    int v;
    B() { v = 1; }
    virtual ~B();
    virtual int f() { return 1; }
};
B::~B() { printf("~B "); }

struct D : B { };                        // implicit ~D, inherits f
struct E : B { int f() { return 2; } };  // implicit ~E, own f

int main() {
    { B *p = new D; printf("%d ", p->f()); delete p; }
    { B *p = new E; printf("%d ", p->f()); delete p; }
    { D d; }                             // and as an automatic object
    printf("\n");
    return 0;
}
