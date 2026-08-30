// The same tie, between two non-member operators.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
};

int operator+(const V &v, double d) { return 1 + 0 * (v.x + (int)d); }
int operator+(const V &v, long l)   { return 2 + 0 * (v.x + (int)l); }

int main(void) {
    V a(1);
    printf("%d\n", a + 'c');
    return 0;
}
