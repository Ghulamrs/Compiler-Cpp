// [over.oper]/6: an operator function needs a class or an enumeration among its
// parameters, or a reference to one. `int operator+(int, int)` is a second
// meaning for an operator over built-in types, which the language lets nobody
// give: cxx1 declared it and then never called it, resolution on `1 + 2` taking
// the built-in, so the function sat in the object as a symbol nothing referred
// to. A pointer to a class does not count, which is the rule's own edge.
extern "C" int printf(const char *, ...);
int operator+(int a, int b);
int main() { printf("x\n"); return 0; }
