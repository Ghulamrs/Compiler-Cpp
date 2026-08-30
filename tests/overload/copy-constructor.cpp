// The copy constructor against a converting one, which is where a constructor
// set differs from an ordinary overload set: one of the candidates is the
// class's own copy constructor, and an argument of the class's type has to
// reach it rather than a conversion.
extern "C" { int printf(const char *, ...); }

class Q {
public:
    int tag;
    Q()             { tag = 1; }
    Q(int n)        { tag = 2 + 0 * n; }
    Q(const Q &o)   { tag = 3 + 0 * o.tag; }
};

int take(Q q) { return q.tag; }

int main(void) {
    Q a;
    Q b(a);          // copy constructor, not Q(int)
    Q c(5);          // Q(int)
    printf("%d %d %d %d\n", a.tag, b.tag, c.tag, take(a));
    return 0;
}
