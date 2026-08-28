// The destructor a class does not write - the fourth of the special members,
// and the one that decides when the other three's work is undone.
//
// **It becomes a function exactly when a base or a member has one of its own
// to run**, measured with cl: `??1Has@@QEAA@XZ` for a class holding members
// with destructors, and no destructor symbol at all for a class of plain ones.
//
// **A virtual function does not make it non-trivial** - cl emits nothing for a
// class with a virtual `f()` and no destructor anywhere, which is the thing
// that would have been guessed wrong. What makes it *virtual* is a base whose
// destructor is virtual: then it takes over that slot, gets a deleting form
// beside it, and `delete` through a base pointer reaches it. Microsoft writes
// U where a non-virtual one is Q, the same as for a member function.
//
// The order is the reverse of construction throughout: members after the body,
// in the reverse of the order they were declared, and then the bases; and an
// array backwards.
extern "C" { int printf(const char *, ...); }

struct M {
    int v;
    M();
    ~M();
};
M::M()  { v = 0; }
M::~M() { printf("  ~M %d\n", v); }

struct Base {
    int b;
    ~Base();
};
Base::~Base() { printf("  ~Base %d\n", b); }

struct Plain { int a; int b; };            // no destructor anywhere
struct Poly  { virtual int f(); int n; };  // virtual, and still trivial
int Poly::f() { return 1; }

struct Own : Base { int k; };              // a base with a destructor
struct Two { M a; M b; int k; };           // two members, undone backwards
struct Arr { M e[3]; };                    // an array, undone backwards

struct VB { virtual ~VB(); int n; };
VB::~VB() { printf("  ~VB %d\n", n); }
struct DV : VB { M held; };                // implicit, and virtual

int main() {
    // Nothing to run for either of these, and no function was written for
    // them: they leave and say nothing.
    printf("trivial\n");
    { Plain p; p.a = 1; Poly q; q.n = 2; }

    printf("own\n");
    { Own o; o.b = 1; o.k = 2; }

    printf("two\n");
    { Two t; t.a.v = 10; t.b.v = 20; }

    printf("arr\n");
    { Arr a; a.e[0].v = 1; a.e[1].v = 2; a.e[2].v = 3; }

    printf("dv\n");
    { DV d; d.n = 7; d.held.v = 8; }

    // Through a base pointer, which is what the deleting form is for.
    printf("poly\n");
    { VB *p = new DV; p->n = 9; delete p; }

    printf("end\n");
    return 0;
}
