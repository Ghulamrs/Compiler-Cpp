// A class with a move constructor and no copy constructor. Declaring one is
// how a class says it owns something, and `copyConstructorOf` answering null
// for it looks exactly like a class that is trivially copyable - which is how
// the byte path came to answer here, leaving the move constructor unrun and
// two objects holding one resource. The moved-from value is what proves the
// move actually happened: a byte copy leaves it at 5.
extern "C" int printf(const char *, ...);

int made = 0;
int gone = 0;

struct S {
    int v;
    S(int n) { v = n; made = made + 1; }
    S(S &&o) { v = o.v; o.v = 0; made = made + 1; }
    ~S() { gone = gone + 1; }
};

int main() {
    {
        S a(5);
        S b(static_cast<S &&>(a));
        printf("%d %d ", b.v, a.v);
    }
    printf("%s\n", made == gone ? "balanced" : "unbalanced");
    return 0;
}
