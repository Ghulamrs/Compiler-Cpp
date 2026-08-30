// `nullptr` against an integer parameter and a pointer one.
//
// This is the whole reason the type exists. `f(0)` picks `f(int)`, because 0
// is an int that happens to convert; `f(nullptr)` picks the pointer, because
// std::nullptr_t is not an integer at all and `f(int)` is not viable for it.
extern "C" { int printf(const char *, ...); }

int f(int a)   { return 1 + 0 * a; }
int f(char *a) { return 2 + 0 * (a != 0); }

int main(void) {
    printf("%d %d\n", f(0), f(nullptr));
    return 0;
}
