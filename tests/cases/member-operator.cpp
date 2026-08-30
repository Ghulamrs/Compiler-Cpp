// A member operator: declared inside the class, defined out of line, and
// reached by writing the operator rather than by naming the function.
//
// This case used to be the *refusal* - `operator` was recognised by the lexer
// and had no rule, so it was refused by name. What it holds now is the rule
// that replaced it, and the two halves of that rule that are easy to get
// wrong:
//
//   * a member operator takes the LEFT operand as its object, which is why
//     `a + b` finds V::operator+ and `2 * b` cannot;
//   * the operator is chosen by the *class* of an operand, so `<` on two V's
//     is a call and `<` on the two ints inside them is still the machine
//     instruction it always was.
extern "C" { int printf(const char *, ...); }

struct V {
    int x;
    V operator+(const V &o) const;
    V operator-(const V &o) const;
    bool operator<(const V &o) const;
    V operator<<(int n) const;
};

V V::operator+(const V &o) const { V r; r.x = x + o.x; return r; }
V V::operator-(const V &o) const { V r; r.x = x - o.x; return r; }
bool V::operator<(const V &o) const { return x < o.x; }
V V::operator<<(int n) const { V r; r.x = x << n; return r; }

int main(void) {
    V a; a.x = 10;
    V b; b.x = 4;
    V sum = a + b;
    V diff = a - b;
    // The built-in `<` on the members, beside the overloaded one on the
    // objects, in one expression.
    // `<<` on the class is the overload; `<<` on the ints inside it is the
    // shift instruction, in the same call.
    V up = a << b.x;
    printf("%d %d %d %d %d %d\n", sum.x, diff.x, a < b, b < a, a.x < b.x, up.x);
    return 0;
}
