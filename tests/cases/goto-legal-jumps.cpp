// What [stmt.dcl]/3 still allows, so that the rule refusing a jump past an
// initialisation is known not to refuse these: an uninitialised scalar, a POD
// declared without an initialiser, a static, a jump out of a block, a jump
// backwards within one scope, and a switch whose declaration has no
// initialiser. Every shape checked against clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);

struct Pair { int a; int b; };

int forward(int n) {
    if (n) goto done;
    int x;               // no initialiser: a jump may pass it
    Pair p;              // trivial and uninitialised: the same
    static int s = 40;   // not automatic
    x = 1; p.a = 2; p.b = 3;
    n = x + p.a + p.b + s;
done:
    return n;
}

int outward(int n) {
    int x = 1;
    { if (n) goto out; x = 2; }
out:
    return x;
}

int backward(int n) {
    int x = 0;
again:
    x++;
    if (x < n) goto again;
    return x;
}

int chooser(int n) {
    switch (n) {
    case 0:
        n++;
        int z;
        z = 10;
        n += z;
    case 1:
        return n;
    }
    return -1;
}

int main() {
    printf("%d %d\n", forward(0), forward(5));
    printf("%d %d\n", outward(0), outward(1));
    printf("%d\n", backward(3));
    printf("%d %d %d\n", chooser(0), chooser(1), chooser(2));
    return 0;
}
