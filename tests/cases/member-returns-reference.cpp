// `int &get() { return x; }` - a member *function* returning a reference.
//
// It was reported as a reference *data member*, which is refused because
// binding one needs a constructor. At the point that check is made the
// declarator has been read but the '(' has not, so the type in hand is still
// the *return* type - and asking whether a function follows is the difference
// between an ordinary accessor and something this compiler cannot build.
extern "C" int printf(const char *, ...);

struct Counter {
    int n;
    int &value() { return n; }
    const int &frozen() { return n; }
    int *address();
};
int *Counter::address() { return &n; }

int main() {
    Counter c;
    c.n = 1;
    c.value() = 5;
    printf("%d %d %d\n", c.n, c.frozen(), *c.address());
    return 0;
}
