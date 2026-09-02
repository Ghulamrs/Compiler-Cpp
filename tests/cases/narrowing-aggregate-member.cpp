// The neighbouring shape: the braces belong to an aggregate and the 300 is
// one of its members. Aggregate initialisation is list-initialisation of each
// member, so the same rule applies to each value - clang refuses this with
// the same message as the scalar case.
struct S { int a; char c; };
int main() { S s = {1, 300}; return s.c; }
