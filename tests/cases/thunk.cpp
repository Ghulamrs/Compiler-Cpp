// Secondary vtables and thunks - the last piece of rung 4.
//
// C has two polymorphic bases. A is at 0 and B is at 16, so the object carries
// TWO vptrs, and _ZTV1C holds two tables back to back: the primary one for A,
// then a secondary one for B whose first word is -16, saying how far back the
// complete object is.
//
// `viaB(&c)` is the whole point. It arrives with `this` pointing at the B
// subobject and reads B's slot, which for an overridden function cannot be
// C::g directly - C::g expects the whole object. So the slot holds a THUNK
// that walks `this` back by sixteen and calls the real one. clang tail-jumps;
// this calls and returns, which costs a frame and behaves identically.
extern "C" { int printf(const char *, ...); }
class A { public: A(); virtual int f(); int a; };
class B { public: B(); virtual int g(); int b; };
class C : public A, public B { public: C(); virtual int f(); virtual int g(); int c; };
A::A() { a = 1; }  int A::f() { return 1; }
B::B() { b = 2; }  int B::g() { return 2; }
C::C() { c = 3; }  int C::f() { return 4; }  int C::g() { return 5; }
int viaA(A *p) { return p->f(); }
int viaB(B *p) { return p->g(); }
int main(void) {
    A a; B b; C c;
    printf("%d %d\n", viaA(&a), viaB(&b));
    printf("%d %d\n", viaA(&c), viaB(&c));
    return 0;
}
