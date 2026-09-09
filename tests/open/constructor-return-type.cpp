// [class.ctor]/3: a constructor has no return type. cxx1 accepts one.
extern "C" int printf(const char *, ...);
struct S { int S(); };
int main() { printf("x\n"); return 0; }
