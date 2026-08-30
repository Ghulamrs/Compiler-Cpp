// A member unary operator and a non-member one, equally good. The same tie the
// binary case has, and it has to be refused for the same reason - which is
// only asked at all because the two halves are ranked together.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator-() const { return 1 + 0 * x; }
};

int operator-(const V &v) { return 2 + 0 * v.x; }

int main(void) {
    V a(1);
    printf("%d\n", -a);
    return 0;
}
