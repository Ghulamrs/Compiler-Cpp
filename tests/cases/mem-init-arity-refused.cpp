// The third of the three rules mem-init-two-rules.cpp separates: an entry
// that is neither constructed nor value-initialised carries one value and
// assigns it, and more than one is refused. The check used to sit in the loop
// that reads the list, where it was asked of every entry alike and would have
// refused `: m(1, 2)` on a class; it sits with the construction now, after
// the member's own type has had its say. This is what pins that it still
// fires - for a scalar, which has no constructor to reach.
struct S { int k; S() : k(1, 2) {} };
int main() { S s; return s.k; }
