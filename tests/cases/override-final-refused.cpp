// **`override` is a check, not a declaration.** An override is found by its
// base's slot here whether or not the word is written - [class.virtual]/2 -
// so what is missing is the diagnostic rather than the dispatch.
struct B { virtual int f() { return 0; } virtual ~B() {} };
struct D : B { int f() override { return 0; } };
int main() { D d; return d.f(); }
