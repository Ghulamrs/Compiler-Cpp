// A parameter of the type itself beats every pointer.
//
// `decltype(nullptr)` is an identity conversion for `nullptr`, and a pointer
// parameter is a pointer conversion, so there is nothing to weigh up. Written
// with `decltype` because naming the type `std::nullptr_t` needs <cstddef>,
// and this compiler has no headers.
extern "C" { int printf(const char *, ...); }

int f(decltype(nullptr) a) { return 1 + 0 * (a != 0); }
int f(char *a)             { return 2 + 0 * (a != 0); }
int f(int a)               { return 3 + 0 * a; }

int main(void) {
    printf("%d\n", f(nullptr));
    return 0;
}
