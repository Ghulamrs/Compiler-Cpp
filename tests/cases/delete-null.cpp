// [expr.delete]/2: deleting a null pointer has no effect. cxx1 ran the
// destructor on it - and for a virtual one, loaded the vtable through it - so
// `delete p;` on a pointer that may be null, which is the reason `delete` is
// written at all, segfaulted.
//
// `operator delete` took null itself and always did; what needed the guard is
// the destructor call in front of it, and the virtual path's whole call,
// which frees as well as destroys. Both shapes are here, and so are the two
// that never had the fault - no destructor at all, and a fundamental type -
// because the guard must not cost them anything either.
extern "C" int printf(const char *, ...);

struct Plain { int v; ~Plain() { v = 0; } };
struct Poly  { int v; virtual ~Poly() { v = 0; } };
struct Bare  { int v; };

int main() {
    Plain *a = 0;  delete a;
    Poly  *b = 0;  delete b;
    Bare  *c = 0;  delete c;
    int   *d = 0;  delete d;

    // and a real one still runs its destructor
    Plain *e = new Plain;  e->v = 5;  delete e;
    Poly  *f = new Poly;   f->v = 6;  delete f;
    printf("survived\n");
    return 0;
}
