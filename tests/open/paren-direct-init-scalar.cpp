// [dcl.init]/16: `int z(5);` is direct-initialisation of a scalar, and cxx1
// refuses it with "expected a type". Review C singled it out because the
// repository's own <utility> depends on it - see std-swap-two-ints.cpp.
extern "C" int printf(const char *, ...);
int main() { int z(5); printf("%d\n", z); return 0; }
