// **`new T[n]()` value-initialises every element** - [expr.new]/17 allows
// exactly the empty pair after an array new-type-id, and [dcl.init]/8 makes
// it n zeroed elements. n is a run-time value and this compiler's expression
// language has no loop, so the zeroing is a call to the platform's `memset`,
// the way the storage is a call to the platform's `operator new[]` - and it
// is what clang emits for the same line. A scalar, a class with no
// constructor, a constant count and a count read at run time. This was
// refused outright - "cannot initialise an array" - beside a `new int()` that
// zeroed.
extern "C" int printf(const char *, ...);

struct P { int a; int b; };

static int four() { return 4; }

int main() {
    int n = 3;
    int *a = new int[4]();
    P *p = new P[n]();
    double *d = new double[four()]();
    char *c = new char[1]();
    printf("%d %d %d %d | %d %d %d %d %d %d | %d %d %d %d | %d\n",
           a[0], a[1], a[2], a[3], p[0].a, p[0].b, p[1].a, p[1].b, p[2].a,
           p[2].b, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)c[0]);
    delete[] a;
    delete[] p;
    delete[] d;
    delete[] c;
    return 0;
}
