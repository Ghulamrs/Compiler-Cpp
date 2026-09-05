// **A const floating object is read back when a later one is folded.**
// `const double b = (1 - f) * a;` at namespace scope is dynamic initialisation
// by the letter of [basic.start.init] - a const object of *floating* type is
// not a constant expression in C++11, only an integral one is - and every
// compiler folds it instead of running code before main. cxx1 requires a
// constant initialiser at file scope, so without the read-back the ordinary way
// of defining physical constants in terms of each other did not compile.
//
// The twin of the integral read-back [expr.const]/3 already had. The object
// still has an address, which is what taking one below checks.
extern "C" int printf(const char *, ...);

const double semiMajor = 6378137.0;
const double flattening = 1.0 / 298.257223563;
const double semiMinor  = (1 - flattening) * semiMajor;   // folded from both
const double ratio      = semiMinor / semiMajor;

int main() {
    printf("%.4f\n", semiMinor);
    printf("%.9f\n", ratio);
    printf("%d\n", &semiMinor != &semiMajor ? 1 : 0);      // both still objects
    return 0;
}
