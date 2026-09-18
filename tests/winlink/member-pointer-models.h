extern "C" int printf(const char *, ...);
struct One { char c; };
struct Single { int x; int f(); };
struct Multi : One, Single { int y; int g(); };
struct V { int v; };
struct Virt : virtual V { int z; int h(); };
struct Plain { int p; };
struct Poly : Plain { virtual int v(); int k(); int q; };   // k plain: a pointer to a virtual member is refused here
int callMulti(Multi &m, int (Multi::*p)());
int readMulti(Multi &m, int Multi::*p);
int callVirt(Virt &o, int (Virt::*p)());
int readVirt(Virt &o, int Virt::*p);
int callPoly(Poly &o, int (Poly::*p)());
int sizes();
