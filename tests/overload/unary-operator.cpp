// Unary operators, member and non-member ranked together, and the const of the
// object taking part in that ranking exactly as it does for a binary one.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator-()       { return 1 + 0 * x; }
    int operator-() const { return 2 + 0 * x; }
    int operator!() const { return 3 + 0 * x; }
};

class W {
public:
    int y;
    W(int n) { y = n; }
};

// no member form at all, so the non-member is the whole candidate set
int operator-(const W &w) { return 4 + 0 * w.y; }
int operator!(W &w)       { return 5 + 0 * w.y; }
int operator!(const W &w) { return 6 + 0 * w.y; }

int main(void) {
    V a(1);
    const V b(2);
    W c(3);
    const W d(4);
    // the object's own const separates the two operator-
    printf("%d %d %d\n", -a, -b, !a);
    printf("%d %d %d %d\n", -c, -d, !c, !d);
    return 0;
}
