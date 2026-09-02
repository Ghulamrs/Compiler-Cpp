// A floating literal is read once, with `strtod` - `strtof` for an `f`
// suffix - so that its value cannot depend on which machine built the
// compiler. Read through the host's `long double` it was rounded twice:
// 64 bits and then 53 on the Linux box, 53 straight away on the Mac, and
// there are decimal strings the two answers disagree on. These two sit
// astride that boundary on purpose. The double is 1 + 2^-53 + 2^-70, whose
// one correct rounding is *up* to 0x1.0000000000001p+0 where the two-step
// answer is down to 1.0; the float is 1 + 2^-24 + 2^-55, up to
// 0x1.000002p+0 where a 53-bit read answers 1.0f. Each comparison holds
// only for the literal rounded once - measured: the same source made
// `.quad ...409` on one build machine and `...408` on the other.
//
// The third value is an exact `long double` written with trailing zeros:
// 2.5 with nineteen zeros is the same number as 2.5, and the digit test
// defers zeros to the exponent instead of overflowing its accumulator on
// them and calling the literal inexact.
extern "C" int printf(const char *, ...);
double d = 1.0000000000000001110231494954629083427022351315827108919620513916015625;
float f = 1.0000000596046448031462006156289135105907917022705078125f;
long double z = 2.5000000000000000000L;
int main() {
    printf("%d %d %d\n", d > 1.0 ? 1 : 0, f > 1.0f ? 1 : 0, (int)(z * 2));
    return 0;
}
