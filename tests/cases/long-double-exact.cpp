// A `long double` literal is read with the host's own `strtold`, and the host
// is not the target: on the Linux box a long double carries 64 bits of
// significand and on the Mac it carries 53. A literal that needs more than a
// double's 53 bits therefore became a different constant depending on which
// machine built the compiler - and three-box verification cannot see that,
// because each box agrees with itself. Those are refused now, for the one
// target whose long double is wider than a double.
//
// These are the ones that are exact in a double, which every host parses
// identically and all three targets emit the same way. Measured against
// clang for x86_64-linux: 1.5L is .quad 0xc000000000000000 with exponent
// 0x3fff, and 0.25L is .quad 0x8000000000000000 with 0x3ffd.
extern "C" int printf(const char *, ...);

long double a = 1.5L;
long double b = 0.25L;
long double c = 100.0L;
long double d = 2.5e3L;

int main() {
    long double sum = a + b + c + d;
    printf("%d\n", (int)(sum * 4));
    return 0;
}
