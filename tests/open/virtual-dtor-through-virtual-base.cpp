// **Deleting through a pointer to a *virtual* base, whose destructor is
// virtual.** `L : virtual B` and `M : L`; `delete p` where `p` is an `L *`
// holding an M has to reach M's destructor through the vtable. Both cxx1
// targets reach L's instead and never run `~M`, where clang prints `-M -L -B6`
// and cxx1 prints `-L -B6` (x86_64-windows) or `-L -B0` (the Itanium targets,
// which also read the wrong `n`).
//
// Not the Microsoft virtual-base round's doing - it fails the same way on both
// ABIs, which is what says the fault is in how the destructor slot is reached
// for a class whose polymorphism comes down a virtual base, and not in either
// ABI's layout. Found 2026-09-10 by that round's own probe.
extern "C" int printf(const char *, ...);

struct B {
    int n;
    B(int v) : n(v) { printf("+B%d ", v); }
    virtual ~B() { printf("-B%d ", n); }
};
struct L : virtual B { L(int v) : B(v) { printf("+L "); } ~L() { printf("-L "); } };
struct M : L { M(int v) : L(v), B(v + 1) { printf("+M "); } ~M() { printf("-M "); } };

int main() {
    L *p = new M(5);
    printf("| %d\n", p->n);
    delete p;
    printf("\n");
    return 0;
}
