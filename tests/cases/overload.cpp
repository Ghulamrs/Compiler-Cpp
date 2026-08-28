// Overload resolution: the same name, several functions, and the argument
// list deciding which one a call means.
//
// Every line here is a different rule in [over.ics.rank], and the expected
// output is clang's own - `clang++ -x c++ -std=c++11 -pedantic-errors`.
extern "C" {
int printf(const char *, ...);
}

int which(int n)         { return 1 + 0 * n; }
int which(double x)      { return 2 + 0 * (int)x; }
int which(const char *s) { return 3 + 0 * (int)s[0]; }
int which(int a, int b)  { return 4 + 0 * (a + b); }

// The pair the mangler was built for. A `char *` argument matches the first
// exactly and reaches the second only through a qualification conversion, so
// the two do not tie - the identity conversion wins.
int qual(char *s)        { return 5 + 0 * (int)s[0]; }
int qual(const char *s)  { return 6 + 0 * (int)s[0]; }

// Declared twice with the same parameters is one function, not two.
int again(int n);
int again(int n)         { return 7 + 0 * n; }

int main(void) {
    char buf[4];
    char c;
    float f;
    buf[0] = 0;
    c = 'x';
    f = 1.0f;
    // int, double, const char *, and arity.
    printf("%d %d %d %d\n", which(1), which(1.5), which("hi"), which(1, 2));
    // A promotion beats a conversion: char promotes to int, float to double.
    printf("%d %d\n", which(c), which(f));
    printf("%d %d\n", qual(buf), qual("lit"));
    printf("%d\n", again(0));
    return 0;
}
