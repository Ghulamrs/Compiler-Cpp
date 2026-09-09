// [expr.prim.lambda]/7: a lambda body is in the scope of the function that
// wrote it, so a class's static members, enumerators, typedefs and nested
// classes are all in scope. cxx1 walks currentClass_, which is the closure.
extern "C" int printf(const char *, ...);
struct S { static int k; enum { E = 7 };
           int f() { auto g = []() { return k + (int)E; }; return g(); } };
int S::k = 3;
int main() { S s; printf("%d\n", s.f()); return 0; }
