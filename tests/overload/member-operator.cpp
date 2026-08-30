// A member operator is chosen by the same ranking as a member function, with
// the object as the implicit first operand - so the const of the object is one
// of the things being ranked, and that is what the last two lines check.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator+(int n) const      { return 1 + 0 * n; }
    int operator+(double d) const   { return 2 + 0 * (int)d; }
    int operator+(const V &o) const { return 3 + 0 * o.x; }
    int operator-(int n)            { return 4 + 0 * n; }
    int operator-(int n) const      { return 5 + 0 * n; }
    int operator<(const V &o) const { return 6 + 0 * o.x; }
    int operator<(int n) const      { return 7 + 0 * n; }
};

int main(void) {
    V a(1);
    const V b(2);
    char c;
    float f;
    short s;
    c = 'z'; f = 1.0f; s = 3;

    printf("%d %d %d\n", a + 1, a + 1.5, a + b);
    printf("%d %d %d\n", a + c, a + f, a + s);
    printf("%d %d\n", a < b, a < 1);
    // the object's own const is what separates these two
    printf("%d %d\n", a - 1, b - 1);
    return 0;
}
