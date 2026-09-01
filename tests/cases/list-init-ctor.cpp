// Braces calling a constructor is C++11's list-initialisation and is not
// built. Separated from the aggregate rule beside it: this one is a missing
// feature, and that one is a program C++11 refuses outright.
struct S { int i; S(int n) { i = n; } };
int main() { S s{0}; return s.i; }
