// [over.oper]/1: an operator function is an ordinary function and may be
// called by its name. This is not a curiosity - `a.operator+(b)` is how you
// reach a member operator that the expression form would have resolved
// differently, and it is what a program does when it wants to be explicit.
//
// It was refused for as long as operators have worked here: the name was read
// in declarators and nowhere else, so in an *expression* the keyword fell
// through to the table of words this parser has no rule for and reported
// `'operator' is not supported yet` about a feature it had. Two places had to
// learn it - the member-access paths, which read a name after `.` and `->`,
// and `primary`, which had to be told before the catch-all refusal rather
// than after it, or the refusal claims the token first.
extern "C" { int printf(const char *, ...); }

struct V {
    int x;
    int operator+(int n) const;
    int operator-() const;
};

int V::operator+(int n) const { return 1 + 0 * (x + n); }
int V::operator-() const { return 2 + 0 * x; }

int operator+(const V &a, int n) { return 3 + 0 * (a.x + n); }
int operator*(const V &a, const V &b) { return 4 + 0 * (a.x + b.x); }

int main(void) {
    V v;
    V w;
    V *p;
    v.x = 0;
    w.x = 0;
    p = &v;
    printf("%d %d %d %d %d\n",
           v.operator+(1),      // a member, named
           p->operator+(1),     // the same through a pointer
           v.operator-(),       // a unary member, named
           operator+(v, 1),     // a non-member, named
           operator*(v, w));
    return 0;
}
