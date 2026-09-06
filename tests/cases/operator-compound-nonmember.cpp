// A compound assignment declared as a non-member - `operator*=(Q &, const Q &)`.
//
// [over.ass]/1 restricts plain `=` to a member and says nothing about the ten
// `@=`; [over.binary]/1 lets any binary operator be a non-member of two
// parameters. So a class may write its `*=` outside itself, which is what a
// header does when the product is already a non-member and `a *= b` is
// `a = a * b`. It was refused at the declaration as unreachable.
//
// [over.match.oper]/3 builds one candidate set of the left class's members
// and the non-members and ranks them together: `S::operator+=(int)` beats
// `operator+=(S &, double)` for an int, and the double one wins for a double.
// A class on the *right* of a built-in target reaches the built-in `@=`
// through its conversion function - [over.match.oper]/9 - and never through a
// rewrite into `operator+`. operator-compound-refused.cpp keeps that half.
extern "C" int printf(const char *, ...);

struct Q { int v; };
Q operator*(const Q &a, const Q &b) { Q r; r.v = a.v * b.v; return r; }
Q &operator*=(Q &a, const Q &b) { a = a * b; return a; }
Q &operator-=(Q &a, int n) { a.v -= n; return a; }

struct S {
    int v;
    S &operator+=(int n) { v += n; return *this; }
};
S &operator+=(S &s, double d) { s.v += (int)(d * 100); return s; }

struct N {
    int v;
    operator int() const { return v; }
};

namespace ns {
    struct T { int v; };
    T &operator<<=(T &t, int n) { t.v = t.v << n; return t; }
}

int main(void) {
    Q a; a.v = 3;
    Q b; b.v = 4;
    a *= b;
    (a *= b) -= 1;
    S s; s.v = 1;
    s += 2;
    s += 0.5;
    N n; n.v = 7;
    int x = 10;
    x -= n;
    ns::T t; t.v = 1;
    t <<= 3;
    printf("%d %d %d %d\n", a.v, s.v, x, t.v);
    return 0;
}
