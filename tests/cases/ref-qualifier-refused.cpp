// A ref-qualifier picks the overload by the object's own value category,
// which is a rank the implicit object parameter does not carry here.
struct S { int f() & { return 0; } };
int main() { S s; return s.f(); }
