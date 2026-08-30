// The same value-category ranking, on an operator rather than a function -
// the path a move constructor is reached through when an operator returns.
extern "C" { int printf(const char *, ...); }

class S {
public:
    int x;
    S(int n) { x = n; }
};

int operator+(S &a, int n)        { return 1 + 0 * (a.x + n); }
int operator+(const S &a, int n)  { return 2 + 0 * (a.x + n); }

int operator-(const S &a, int n)  { return 3 + 0 * (a.x + n); }
int operator-(S &&a, int n)       { return 4 + 0 * (a.x + n); }

int main(void) {
    S a(1);
    const S b(2);
    printf("%d %d\n", a + 1, b + 1);
    printf("%d %d\n", a - 1, static_cast<S &&>(a) - 1);
    return 0;
}
