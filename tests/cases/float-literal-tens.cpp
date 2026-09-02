// A power of ten is exact in a double as far as 1e22, because 10^k is
// 5^k * 2^k and the twos are free: what has to fit in 53 bits is 5^k, and
// 5^22 does where 5^23 does not. The digit test used to multiply the
// accumulator by *ten*, which overflows 64 bits at 1e20 - so
// `100000000000000000000.0L`, an exact value, was refused for the x87 target
// as if it needed more than a double, while 2^70 written out was refused for
// the same reason it still is: twenty-two significant digits overflow the
// accumulator before the exponent is reached. Fives instead of tens, and an
// overflow there means the odd part of the value is past 2^64 - which is the
// true answer, not the safe one. 1e23L is refused for x86_64-linux on
// purpose, so it does not appear here; the double beside it is rounded once
// by the language's own rule and compares equal to what clang folds.
extern "C" int printf(const char *, ...);
long double a = 100000000000000000000.0L;
long double b = 1e22L;
double c = 1e23;
int main() {
    printf("%d %d %d\n", (int)(a / 1e19), b == 1e22 ? 1 : 0,
           c == 99999999999999991611392.0 ? 1 : 0);
    return 0;
}
