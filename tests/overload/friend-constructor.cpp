// The three forms meeting in one place: an overloaded private constructor set,
// reached only from a friend, chosen by the same ranking as anything else.
//
// It is worth having because the constructor access check and the overload
// ranking are different pieces of the compiler, and this is the only case
// where a friend has to get past the first before the second can be asked.
extern "C" { int printf(const char *, ...); }

class Token {
    Token(int n)         { tag = 1 + 0 * n; }
    Token(double d)      { tag = 2 + 0 * (int)d; }
    Token(const char *s) { tag = 3 + 0 * (int)s[0]; }
    Token(int a, int b)  { tag = 4 + 0 * (a + b); }
public:
    int tag;
    friend int make(int n);
    friend int make(double d);
    friend int make(const char *s);
    friend int make(int a, int b);
};

int make(int n)         { Token t(n); return t.tag; }
int make(double d)      { Token t(d); return t.tag; }
int make(const char *s) { Token t(s); return t.tag; }
int make(int a, int b)  { Token t(a, b); return t.tag; }

int main(void) {
    char c;
    float f;
    c = 'q'; f = 1.0f;
    printf("%d %d %d %d\n", make(1), make(1.5), make("x"), make(1, 2));
    printf("%d %d\n", make(c), make(f));
    return 0;
}
