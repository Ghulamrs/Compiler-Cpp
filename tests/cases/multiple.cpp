// Multiple inheritance - and the step where "the base is at offset 0" stops
// being true, which every step before this leaned on.
//
// A sits at 0 and B at 4, so `B *pb = &x` is not `&x` - it is `&x + 4`, which
// the "adjust" line shows. The same four is what B's member functions expect
// as `this`, so calling one through a C converts the pointer on the way in.
// A null pointer stays null through that conversion: [conv.ptr] says so, and
// (char *)0 + 4 is not null.
//
// Bases are constructed in the order written and destroyed in reverse - A up,
// B up, C up, then C down, B down, A down.
//
// Still refused: a second base with virtual functions. Its vptr cannot be at
// offset 0 as well, so it needs a second vtable and thunks that adjust `this`
// on the way into an override - which is the next step, not this one.
extern "C" { int printf(const char *, ...); }
class A { public: A(); ~A(); int a; int fromA(); };
class B { public: B(); ~B(); int b; int fromB(); };
class C : public A, public B { public: C(); ~C(); int c; };
A::A() { a = 1; printf("A up\n"); }  A::~A() { printf("A down\n"); }
B::B() { b = 2; printf("B up\n"); }  B::~B() { printf("B down\n"); }
C::C() { c = 3; printf("C up\n"); }  C::~C() { printf("C down\n"); }
int A::fromA() { return a * 10; }
int B::fromB() { return b * 100; }
int main(void) {
    C x;
    A *pa = &x;
    B *pb = &x;
    printf("sizes %d %d %d\n", (int)sizeof(A), (int)sizeof(B), (int)sizeof(C));
    printf("offs %d %d %d\n", (int)((char*)&x.a-(char*)&x), (int)((char*)&x.b-(char*)&x), (int)((char*)&x.c-(char*)&x));
    printf("adjust %d %d\n", (int)((char*)pa-(char*)&x), (int)((char*)pb-(char*)&x));
    printf("calls %d %d\n", x.fromA(), x.fromB());
    return 0;
}
