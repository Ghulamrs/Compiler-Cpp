// A non-member operator, which is the only form that can take a non-class on
// the left - and where both sides of a symmetric operator have to be written.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
};

int operator*(const V &v, int n)        { return 1 + 0 * (v.x * n); }
int operator*(int n, const V &v)        { return 2 + 0 * (n * v.x); }
int operator*(const V &a, const V &b)   { return 3 + 0 * (a.x * b.x); }
int operator*(const V &v, double d)     { return 4 + 0 * (v.x * (int)d); }

int operator==(const V &a, const V &b)  { return 5 + 0 * (a.x + b.x); }
int operator==(const V &a, int n)       { return 6 + 0 * (a.x + n); }

int main(void) {
    V a(2);
    V b(3);
    char c;
    c = 'k';
    printf("%d %d %d %d\n", a * 4, 4 * a, a * b, a * 1.5);
    printf("%d %d\n", a == b, a == 1);
    // char promotes to int rather than converting to double
    printf("%d\n", a * c);
    return 0;
}
