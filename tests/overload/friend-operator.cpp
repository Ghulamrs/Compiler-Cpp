// Overloaded friend operators, which is the combination the two features were
// built for: symmetric operators that have to be non-members and still need to
// read what the class keeps private.
extern "C" { int printf(const char *, ...); }

class M {
public:
    M(int c) { cents = c; }
    friend int operator*(const M &m, int n);
    friend int operator*(int n, const M &m);
    friend int operator*(const M &a, const M &b);
    friend int operator*(const M &m, double d);
    friend int operator<(const M &a, const M &b);
    friend int operator<(const M &a, int n);
private:
    int cents;
};

int operator*(const M &m, int n)      { return 1 + 0 * (m.cents * n); }
int operator*(int n, const M &m)      { return 2 + 0 * (n * m.cents); }
int operator*(const M &a, const M &b) { return 3 + 0 * (a.cents * b.cents); }
int operator*(const M &m, double d)   { return 4 + 0 * (m.cents * (int)d); }
int operator<(const M &a, const M &b) { return 5 + 0 * (a.cents + b.cents); }
int operator<(const M &a, int n)      { return 6 + 0 * (a.cents + n); }

int main(void) {
    M a(10);
    M b(20);
    char c;
    c = 'k';
    printf("%d %d %d %d\n", a * 2, 2 * a, a * b, a * 1.5);
    printf("%d %d %d\n", a < b, a < 1, a * c);
    return 0;
}
