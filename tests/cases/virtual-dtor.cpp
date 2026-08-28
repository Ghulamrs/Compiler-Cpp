// Virtual destructors - and the one function in the vtable that no program
// writes.
//
// `delete p` through a Base * has to reach the destructor of whatever is
// actually there, and then free the memory. One slot has to do both, so the
// slot holds a DELETING destructor: destroy, then give the memory back. cxx1
// synthesizes it - there is no source for it anywhere - and emits it like any
// other function, so no backend knows it was invented.
//
// The two ABIs were measured and differ here more than anywhere else. Itanium
// takes two adjacent slots, D1 for the complete object and D0 for the deleting
// form, and `delete` calls the second. Microsoft takes one, ??_G, which also
// takes a flag and frees only when its low bit is set - so a non-heap object
// can reach the same slot safely - and returns `this`.
//
// The trace is the point: ~Derived then ~Base for the derived object, because
// a destructor still runs its base's last.
extern "C" { int printf(const char *, ...); }
class Base {
public:
    Base();
    virtual ~Base();
    virtual int who();
    int b;
};
class Derived : public Base {
public:
    Derived();
    virtual ~Derived();
    virtual int who();
    int d;
};
Base::Base() { b = 1; }
Base::~Base() { printf("~Base\n"); }
int Base::who() { return 1; }
Derived::Derived() { d = 2; }
Derived::~Derived() { printf("~Derived\n"); }
int Derived::who() { return 2; }
int main(void) {
    Base *p = new Derived;
    printf("who %d\n", p->who());
    delete p;
    Base *q = new Base;
    delete q;
    return 0;
}
