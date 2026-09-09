// A scalar takes one initialiser. `int b(1, 2);` reaches the same parenthesised
// form direct-init-scalar.cpp is about, and there is nothing for the second
// value to initialise - clang says "excess elements in scalar initializer" and
// this says which type took one. The parenthesised list is for a class with a
// constructor that takes them, which is the case beside this one.
extern "C" int printf(const char *, ...);

int main() {
    int b(1, 2);
    printf("%d\n", b);
    return 0;
}
