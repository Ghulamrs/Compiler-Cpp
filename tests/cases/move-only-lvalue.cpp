// [class.copy]/7: declaring a move constructor deletes the implicit copy, so
// this class cannot be built from an lvalue at all. cxx1 never declared the
// copy either, which is a different thing that reads the same from the
// tables - so this compiled and copied the bytes.
struct S { int v; S() { v = 0; } S(S &&o) { v = o.v; o.v = 0; } };
int main() { S a; S b = a; return b.v; }
