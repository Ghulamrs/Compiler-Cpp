// [over.oper]/6: an operator function needs a parameter of class or
// enumeration type. cxx1 accepts one declared over two ints.
extern "C" int printf(const char *, ...);
int operator+(int a, int b);
int main() { printf("x\n"); return 0; }
