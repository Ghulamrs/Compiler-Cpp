// The other direction of the same rule. The class has a member `operator+`,
// but it takes an int and cannot take a V - so for `a + b` the only viable
// candidate is the non-member, and it has to be found.
//
// Asking "does the class declare operator+" first and stopping there refuses
// this program, because the member set is non-empty and nothing in it fits.
// Ranking both halves together is what finds the function that does.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n) { x = n; }
    int operator+(int n) const { return 1 + 0 * n; }
};

int operator+(const V &a, const V &b) { return 2 + 0 * (a.x + b.x); }

int main(void) {
    V a(1);
    V b(2);
    printf("%d %d\n", a + 3, a + b);
    return 0;
}
