// [dcl.init.aggr]/1: a class that writes an initialiser on a member is not an
// aggregate in C++11, and is one from C++14. So this is ill-formed here and
// legal there, which makes it the one place where having written member
// initialisers changes what an older program means.
struct S { int i = 1; int j = 2; };
int main() { S s = {5, 6}; return s.i - 5; }
