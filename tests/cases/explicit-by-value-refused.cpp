// A by-value parameter of a class whose copy constructor is explicit, refused.
//
// **The least obvious of the three copy-initializations**, because nothing at
// the call site is written with an '=' in it. [dcl.init]/17 makes a by-value
// parameter initialised from the argument, and that is copy-initialization
// like any other. `const Guarded &` takes it without copying and compiles -
// see explicit.cpp.
struct Guarded {
    int v;
    Guarded(int a) { v = a; }
    explicit Guarded(const Guarded &o) { v = o.v; }
};

int read(Guarded g) { return g.v; }

int main(void) {
    Guarded a(2);
    return read(a);
}
