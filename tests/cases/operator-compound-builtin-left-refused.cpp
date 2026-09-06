// `x += q` for an int x and a class q that converts to nothing: the built-in
// `+=` cannot take it, and an `operator+` written for the pair is not an
// `operator+=` - clang refuses it too.
struct Q { int v; };
int operator+(int a, const Q &b) { return a + b.v; }

int main(void) {
    Q q; q.v = 2;
    int x = 1;
    x += q;
    return x;
}
