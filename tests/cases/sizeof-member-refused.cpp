// [expr.sizeof]/2 lets C++11 name a non-static data member with no object.
// Told apart from a name the class does not have at all, which keeps its own
// message.
struct S { int m; };
int main() { return (int)sizeof(S::m) - (int)sizeof(int); }
