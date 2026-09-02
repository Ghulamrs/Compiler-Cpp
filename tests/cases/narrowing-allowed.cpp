// The conversions [dcl.init.list]/7 permits inside braces, so that the rule
// refusing a narrowing one is known not to refuse these: a constant that fits
// its integer target, a source type every value of which the target holds, an
// integer constant that survives a floating round trip, a floating constant
// stepping down within range, a named constant, and each of those inside an
// aggregate and at file scope. Every line checked against clang under
// -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

struct S { char c; double d; };
char g = {66};
const int k = 65;

int main() {
    char c = {65};
    char m = {-1};
    unsigned char u = {255};
    short s = {c};           // short holds every char
    int i = {c};
    long l = {i};
    double d = {1};          // 1 comes back as 1
    float f = {1.0};         // within float's range
    float big = {16777216};  // 2^24, the last integer a float holds exactly
    double dd = {f};         // a step up in rank never narrows
    bool b = {1};
    char kc = {k};           // a const int is a constant expression
    char e = {'a' + 1};
    int a[3] = {1, 2, 'c'};
    S t = {k, 2.5};
    long double ld = {d};
    printf("%d %d %d %d %d %ld\n", c, m, u, s, i, l);
    printf("%g %g %g %g %d\n", d, f, big, dd, b);
    printf("%d %d %d %d %g %d %g\n", kc, e, a[2], t.c, t.d, g, (double)ld);
    // k's address is taken so that it exists in the object: clang folds
    // every read of a const and emits no symbol otherwise, and the name
    // comparison would then call the two translation units different.
    const int *kp = &k;
    printf("%d\n", *kp);
    return 0;
}
