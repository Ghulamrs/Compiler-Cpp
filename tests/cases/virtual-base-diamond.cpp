// A virtual base is built once, by the most-derived class, and by nobody else.
//
// `D1` and `D2` each name `V` virtually, so `Dia` holds one `V` rather than
// two. [class.base.init]/7 says the most-derived class builds every virtual
// base and that the base subobject constructors it calls must not - which is
// why a class with a virtual base needs two constructor bodies where every
// other class needs one. C1 builds the virtual bases and then calls C2; C2
// skips them. A derived class calls C2, so `V` is reached exactly once
// however deep the lattice goes.
//
// Emitting C2 as a label on C1's body - which is right for every class
// without a virtual base, and is what `Function::setAlias` is for - builds V
// once per class that names it: `+V +D1 +V +D2 +V +Dia`. Three constructions
// of one object, and three destructions to match.
//
// The split runs backwards on the way out: D2 destroys what C2 built, and D1
// destroys the virtual bases after it.
extern "C" { int printf(const char *, ...); }
struct V   { V();   ~V();   };
struct D1  : virtual public V { D1();  ~D1();  };
struct D2  : virtual public V { D2();  ~D2();  };
struct Dia : public D1, public D2 { Dia(); ~Dia(); };
V::V()     { printf("+V "); }    V::~V()     { printf("-V "); }
D1::D1()   { printf("+D1 "); }   D1::~D1()   { printf("-D1 "); }
D2::D2()   { printf("+D2 "); }   D2::~D2()   { printf("-D2 "); }
Dia::Dia() { printf("+Dia "); }  Dia::~Dia() { printf("-Dia "); }
int main(void) { { Dia d; } printf("\n"); return 0; }
