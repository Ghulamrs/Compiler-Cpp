// The third face of [class.copy]/7's deletion: a by-value parameter is
// copy-initialised from its argument, and for an lvalue of a class that
// declares a move constructor there is no copy to call. The caller's copy
// path reached for the constructor by name, found none, and fell through to
// the byte copy a trivially copyable class earns - the same absence-read-as-
// triviality that move-only-lvalue.cpp and move-deletes-copy-assign.cpp pin
// for initialisation and assignment.
struct S { int v; S() { v = 5; } S(S &&o) { v = o.v; o.v = 0; } };
int take(S s) { return s.v; }
int main() { S a; return take(a); }
