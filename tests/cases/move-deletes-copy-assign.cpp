// [class.copy]/23: declaring a move constructor deletes the implicit copy
// assignment - the same sentence that deletes the implicit copy constructor,
// two rules on. cxx1 never declared the assignment either, and the bytewise
// struct assignment inherited from C answered in its place - so `c = a`
// compiled, copied the bytes, and two objects owned whatever v stood for.
// move-only-lvalue.cpp is the construction half of the same deletion.
struct S { int v; S() { v = 5; } S(S &&o) { v = o.v; o.v = 0; } };
int main() { S a; S c; c = a; return c.v; }
