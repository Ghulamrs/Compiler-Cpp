// `S{...}` is list-initialisation written as an expression - the same rule a
// declaration's braces meet, one syntax over. It used to fall through as an
// undeclared *object*, sending the reader after a variable that was never
// meant to exist.
struct S { int a; int b; };
S make() { return S{0, 0}; }
int main() { S s = make(); return s.a + s.b; }
