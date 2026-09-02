// **`T()` for a class whose default constructor nobody wrote zeroes first,
// then runs it.** [dcl.init]/8: a class without a user-provided constructor
// is zero-initialised, and if its implicit default constructor is
// non-trivial that constructor then runs. Three things make it non-trivial
// here - a virtual function, a member with an initialiser, a member with a
// constructor of its own - and each got the constructor and none got the
// zeroing: `V()` on a dirty stack printed the stack. The neighbour
// class-temporary.cpp and new-value-init.cpp stopped short of, on both the
// stack and the heap, and `T t = T();`, `f(T())` and `new T()` all say it.
// `new T` without the parens is default-initialisation and stays as it was.
extern "C" int printf(const char *, ...);

struct V { int a; int b; virtual int f() { return a + b; } };
struct M { int x = 5; int y; };
struct Q { int q; Q() : q(3) {} };
struct H { int a; Q q; int b; };

static void dirty() { volatile char j[512]; for (int i = 0; i < 512; i++) j[i] = 0x55; }
static int g(V v) { return v.f(); }
static int sum(H h) { return h.a + h.q.q + h.b; }

int main() {
    dirty();
    V v = V();
    dirty();
    M m = M();
    dirty();
    H h = H();
    printf("%d %d %d | %d %d | %d %d %d\n", v.a, v.b, v.f(), m.x, m.y,
           h.a, h.q.q, h.b);
    dirty();
    printf("%d %d\n", g(V()), sum(H()));
    V *pv = new V();
    M *pm = new M();
    H *ph = new H();
    printf("%d %d %d | %d %d | %d %d %d\n", pv->a, pv->b, pv->f(), pm->x, pm->y,
           ph->a, ph->q.q, ph->b);
    return 0;
}
