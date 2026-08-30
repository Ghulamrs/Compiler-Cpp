// The access checks a friend has to get past are five, not one, and the other
// friend cases only exercise two of them - a data member and a member
// function. This case reaches the other three: a private *constructor*, which
// is checked where a local is built, and a private *static* member, which is
// checked where it is named.
//
// It is worth having as its own case because the five checks are five places
// in the source with no funnel between them, so a sixth added later can
// silently forget friendship. What fails here first is whichever one was
// missed.
extern "C" { int printf(const char *, ...); }

class Secret {
    Secret(int n);
    int v;
    static int count;
public:
    friend int build(int n);
    friend int tally(void);
};

int Secret::count = 5;
Secret::Secret(int n) { v = n; }

int build(int n) { Secret s(n); return s.v; }
int tally(void) { return Secret::count; }

int main(void) {
    printf("%d %d\n", build(9), tally());
    return 0;
}
