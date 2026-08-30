// `a += b` on a class is `operator+=` and nothing else.
//
// This is the case for a bug that existed for about ten minutes and would
// have been invisible without it. A compound assignment is built by reading
// the target back, combining, and storing - `a = a + b` - which is the right
// rewrite for a built-in operand. The moment `+` learned to find a class's
// `operator+`, that rewrite started finding it too, and `a += b` compiled
// into a call the standard does not sanction, for a class that never declared
// `operator+=`. clang refuses the same program.
struct V {
    int x;
    V operator+(const V &o) const;
};

V V::operator+(const V &o) const { V r; r.x = x + o.x; return r; }

int main(void) {
    V a; a.x = 1;
    V b; b.x = 2;
    a += b;
    return a.x;
}
