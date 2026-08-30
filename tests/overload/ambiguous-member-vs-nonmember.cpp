// [over.match.oper] builds ONE candidate set out of the member operators and
// the non-member ones and ranks them together, so an equally good pair of them
// is an ambiguity like any other.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator+(const V &o) const { return 1 + 0 * o.x; }
};

int operator+(const V &a, const V &b) { return 2 + 0 * (a.x + b.x); }

int main(void) {
    V a(1);
    V b(2);
    printf("%d\n", a + b);
    return 0;
}
