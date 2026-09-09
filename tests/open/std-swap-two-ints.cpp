// The same gap where a program meets it: std::swap in this tree's own
// <utility> writes `T c(a);`, so swapping two ints fails inside the header.
#include <utility>
extern "C" int printf(const char *, ...);
int main() { int a = 1, b = 2; std::swap(a, b); printf("%d %d\n", a, b); return 0; }
