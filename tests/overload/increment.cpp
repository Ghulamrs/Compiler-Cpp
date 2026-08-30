// Pre and post increment, which are told apart by a dummy int nobody passes -
// so the postfix form resolves with two operands and the prefix form with one,
// and the overload set contains both at once.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator++()      { return 1 + 0 * x; }
    int operator++(int d) { return 2 + 0 * (x + d); }
    int operator--()      { return 3 + 0 * x; }
    int operator--(int d) { return 4 + 0 * (x + d); }
};

class W {
public:
    int y;
    W(int n) { y = n; }
};

int operator++(W &w)        { return 5 + 0 * w.y; }
int operator++(W &w, int d) { return 6 + 0 * (w.y + d); }

int main(void) {
    V a(1);
    W b(2);
    printf("%d %d %d %d\n", ++a, a++, --a, a--);
    printf("%d %d\n", ++b, b++);
    return 0;
}
