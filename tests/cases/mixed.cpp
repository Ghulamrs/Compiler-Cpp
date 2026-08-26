// The C library is named the way C names it, so its declarations go
// inside a linkage specification. Without it these are C++ names and
// the linker is asked for symbols libc has never had.
extern "C" {
int printf(const char *, ...);
}

int f() { return 3; }

int main(void) {
    int a = 1;
    a = a + 1;
    int b = a * 2;

    for (int i = 0; i < 3; i++) b = b + i;

    int i = 100;

    printf("%d %d %d %d\n", a, b, i, f());
    printf("%d %d\n", (int)sizeof('a'), (int)sizeof("abc"));
    return 0;
}
