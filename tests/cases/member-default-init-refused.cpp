// The other half of member-default-init.cpp: a member the list does not name
// and whose class has no default constructor cannot be built, and the
// constructor that leaves it out is refused where clang refuses it -
// "must explicitly initialize the member 'p'". Before members were built at
// all, this compiled and left `p` unbuilt without a word.
struct P { int v; P(int a) : v(a) {} };
struct S { P p; S() {} };
int main() { S s; return s.p.v; }
