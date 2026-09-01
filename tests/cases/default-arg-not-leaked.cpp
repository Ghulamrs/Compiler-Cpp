// The other half of default-arg-redeclared: h never had a default, and must
// not inherit the one g declared on its redeclaration. This call was accepted
// and ran as h(5).
int g(int a);
int g(int a = 5) { return a; }
int h(int b) { return b * 10; }
int main() { return h(); }
