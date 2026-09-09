// [class.ctor]/5: a class with a const non-static data member and no
// initialiser for it has its default constructor deleted. cxx1 builds one and
// leaves the const member holding the frame.
extern "C" int printf(const char *, ...);
struct S { const int a; };
int main() { S s; printf("%d\n", s.a); return 0; }
