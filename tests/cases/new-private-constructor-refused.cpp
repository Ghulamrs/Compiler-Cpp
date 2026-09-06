// [class.access]/1 applies to the constructor a new-expression calls, exactly
// as it does to every other way of building an object. Seven sites checked a
// constructor's access and this one did not, so `new S(1)` reached a private
// constructor that `S s(1);` on the line above is refused for.
struct S { int v; private: S(int n) : v(n) {} };

int main() { S *p = new S(1); return p->v; }
