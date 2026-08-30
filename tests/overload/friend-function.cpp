// An overloaded friend name. Friendship is granted per function, so a class
// that befriends three of them has made three grants - and resolution among
// them is the ordinary one, because a friend is an ordinary namespace-scope
// function that happens to be allowed in.
extern "C" { int printf(const char *, ...); }

class M {
public:
    M(int c) { cents = c; }
    friend int probe(const M &m);
    friend int probe(const M &m, int k);
    friend int probe(const M &m, double d);
    friend int probe(const M &m, const char *s);
private:
    int cents;
};

int probe(const M &m)                  { return 1 + 0 * m.cents; }
int probe(const M &m, int k)           { return 2 + 0 * (m.cents + k); }
int probe(const M &m, double d)        { return 3 + 0 * (m.cents + (int)d); }
int probe(const M &m, const char *s)   { return 4 + 0 * (m.cents + (int)s[0]); }

int main(void) {
    M m(10);
    char c;
    short s;
    float f;
    c = 'k'; s = 2; f = 1.0f;
    printf("%d %d %d %d\n", probe(m), probe(m, 1), probe(m, 1.5), probe(m, "x"));
    printf("%d %d %d\n", probe(m, c), probe(m, s), probe(m, f));
    return 0;
}
