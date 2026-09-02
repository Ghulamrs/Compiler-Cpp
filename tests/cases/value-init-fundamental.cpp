// **`int()` is a zero and `int(x)` is a cast** - [expr.type.conv]. A class
// name written as a function has made a temporary since rung 2, and a
// fundamental type written the same way was "expected an expression" - so
// `int i = int();` was refused beside a `P p = P();` that ran. The
// typedef of a scalar and an enumeration take the same road under their own
// names, and a
// pointer's zero is a null pointer. One keyword only: `unsigned long()` is
// ill-formed, and value-init-two-words-refused.cpp pins that.
extern "C" int printf(const char *, ...);

typedef int I;
typedef int *IP;
enum Color { Red, Green, Blue };

int main() {
    int i = int();
    double d = double();
    char c = char();
    bool b = bool();
    I t = I();
    IP p = IP();
    Color k = Color(2);
    int from = int(3.9);
    unsigned u = unsigned(-1) >> 28;
    printf("%d %d %d %d %d %d %d %d %d\n", i, (int)d, (int)c, (int)b, t,
           p == 0 ? 1 : 0, (int)k, from, (int)u);
    return 0;
}
