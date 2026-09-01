// **`cvttsd2si` is a signed conversion, and x86 has no unsigned one.** For
// anything narrower than 64 bits that costs nothing - convert to a signed 64
// and truncate - but a double at or above 2^63 has no signed answer, and the
// instruction returns the integer indefinite value, 0x8000000000000000. So
// `(unsigned long long)12000000000000000000.0` came out 9223372036854775808
// on both x86 targets, where arm64 has `fcvtzu` and was right all along: one
// target correct and two not, which is this project's own bug class.
//
// The values straddle the boundary on purpose - below it, exactly on it, and
// above - and the float column is here because the same instruction pair has
// a single-precision half.
extern "C" int printf(const char *, ...);

unsigned long long fromD(double d) { return (unsigned long long)d; }
unsigned long long fromF(float f)  { return (unsigned long long)f; }
unsigned int       narrow(double d) { return (unsigned int)d; }

int main() {
    printf("%llu %llu %llu %llu %llu %u\n",
           fromD(1.5),                       // 1
           fromD(9223372036854775808.0),     // exactly 2^63
           fromD(12000000000000000000.0),    // above it
           fromD(0.0),
           fromF(1.0e19f),
           narrow(4000000000.0));            // the narrow path, unaffected
    return 0;
}
