// [dcl.spec.auto]/7: every declarator in one `auto` declaration must deduce
// the same type. cxx1 accepts int and double in one and takes the first.
extern "C" int printf(const char *, ...);
int main() { auto a = 1, b = 2.0; printf("%d\n", (int)(a + b)); return 0; }
