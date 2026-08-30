// An array of a class with a destructor, refused by name.
//
// [class.dtor] destroys the elements in reverse when the scope ends, and the
// place that emits a scope's destructors is shared with the exception paths on
// all three targets - it knows one object per entry, and teaching it a count
// belongs with that machinery rather than beside a declaration. Refused here
// so that nothing is silently left undestroyed, which is the failure the
// construction beside it exists to stop. An array of a class with only
// constructors works: see array-of-class.
struct S {
    int n;
    S();
    ~S();
};

S::S() { n = 1; }
S::~S() { n = 0; }

int main(void) {
    S a[3];
    return a[0].n;
}
