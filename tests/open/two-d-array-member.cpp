// [class.base.init]/8: every element of an array member is
// default-initialised. cxx1 constructs the first row of a two-dimensional
// array member and leaves the rest holding the frame.
extern "C" int printf(const char *, ...);
struct E { int v; E() : v(1) { printf("+ "); } };
struct H { E g[2][3]; };
int main() { H h; printf("| %d\n", h.g[1][2].v); return 0; }
