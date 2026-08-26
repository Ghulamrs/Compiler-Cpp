// A reference is lowered to a pointer that every use goes through, so this
// case is mostly about proving the dereference is really there: writing
// through a reference has to reach the original object, and a reference
// parameter has to be the caller's variable rather than a copy of it.
int printf(const char *, ...);

struct Point { int x; int y; };

static void bump(int &n) { n = n + 1; }
static int total(const struct Point &p) { return p.x + p.y; }

static int &choose(struct Point &p, int which) {
    if (which == 0) return p.x;
    return p.y;
}

// A reference to an array keeps the length, which is the whole reason to
// pass one that way: the array does not decay on the way in.
static int len(int (&a)[3]) { return (int)(sizeof(a) / sizeof(a[0])); }

static void swap(int &a, int &b) {
    int t = a;
    a = b;
    b = t;
}

int main(void) {
    int x = 41;
    int &r = x;
    r = r + 1;
    printf("%d %d\n", x, r);

    bump(x);
    bump(r);
    printf("%d\n", x);

    int a = 1, b = 2;
    swap(a, b);
    printf("%d %d\n", a, b);

    struct Point p;
    p.x = 3;
    p.y = 4;
    printf("%d\n", total(p));

    struct Point &q = p;
    q.y = 20;
    printf("%d %d\n", p.x, p.y);

    // The call is an lvalue: what comes back is the address of p.x, and the
    // assignment goes through it into p.
    choose(p, 0) = 99;
    printf("%d\n", p.x);

    // A const reference binds to a value by taking a copy of it, converting
    // on the way if it has to.
    const int &copied = 2.75;
    printf("%d\n", copied);

    // A reference to a pointer, and a reference to an array: sizeof asks
    // about what is referred to, never about the pointer underneath.
    int *ptr = &x;
    int *&pr = ptr;
    *pr = 50;
    int arr[3];
    arr[0] = 7; arr[1] = 8; arr[2] = 9;
    int (&ar)[3] = arr;
    printf("%d %d %d %d\n", x, ar[0], ar[2], (int)sizeof(ar));
    printf("%d %d %d\n", len(arr), (int)sizeof(int &), (int)sizeof(r));
    return 0;
}
