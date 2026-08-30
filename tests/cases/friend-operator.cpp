// The case the two features exist for together.
//
// A symmetric operator wants to be a non-member - `2 * v` cannot be a member
// of V, because the left operand is an int - and a non-member cannot see what
// the class keeps private. So an operator that is written outside a class and
// reads its state is written `friend`, and until both halves existed neither
// was much use on its own.
extern "C" { int printf(const char *, ...); }

class Money {
public:
    Money(int c);
    friend Money operator+(const Money &a, const Money &b);
    friend bool operator<(const Money &a, const Money &b);
    friend Money operator*(int n, const Money &m);
    friend int cents_of(const Money &m);
private:
    int cents;
};

Money::Money(int c) { cents = c; }

Money operator+(const Money &a, const Money &b) { Money r(a.cents + b.cents); return r; }
bool operator<(const Money &a, const Money &b) { return a.cents < b.cents; }
Money operator*(int n, const Money &m) { Money r(n * m.cents); return r; }
int cents_of(const Money &m) { return m.cents; }

int main(void) {
    Money a(150);
    Money b(275);
    Money sum = a + b;
    Money triple = 3 * a;
    printf("%d %d %d %d\n", cents_of(sum), cents_of(triple), a < b, b < a);
    return 0;
}
