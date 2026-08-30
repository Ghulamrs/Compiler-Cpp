// The operator that is not a member, which is the form that exists because a
// member cannot have it: the left operand of `2 * v` is an int, and an int
// has no member functions for the call to go through.
//
// It also holds the rule that decides between the two. A member is looked for
// on the left operand's class and nowhere else, so `v * 2` and `2 * v` are
// answered by different functions here - the member and the free one - and
// both have to be written for a class to multiply on either side.
extern "C" { int printf(const char *, ...); }

struct V {
    int x;
    V operator*(int n) const;      /* v * 2 */
};

V V::operator*(int n) const { V r; r.x = x * n; return r; }

/* 2 * v, which no member could take */
V operator*(int n, const V &v) { V r; r.x = n * v.x; return r; }

/* and a free one on two classes, which a member could have taken */
bool operator==(const V &a, const V &b) { return a.x == b.x; }

int main(void) {
    V v; v.x = 6;
    V left = v * 2;
    V right = 3 * v;
    V same; same.x = 6;
    printf("%d %d %d %d\n", left.x, right.x, v == same, v == left);
    return 0;
}
