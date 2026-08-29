// Deduction - rung 5.3. The arguments are worked out from the call.
//
// [temp.deduct.call], and the shape of it is that a parameter looks *through*
// itself: `T *p` given an `int *` deduces T as int, and `const T &r` given an
// int deduces T as int with the const belonging to the parameter and not to
// T. Everything that is not a reference decays first - an array becomes a
// pointer and the top-level qualifier goes - which is not a rule deduction
// invented, it is what passing something already does.
//
// The last two lines are the same specialization reached both ways: written
// out and deduced. One function, because the key is the template's name and
// its arguments and does not record which way they were arrived at.
extern "C" { int printf(const char *, ...); }

template <class T> T twice(T x) { return x + x; }
template <class T> T bigger(T a, T b) { return a > b ? a : b; }
template <class T> int deref(T *p) { return (int)*p; }
template <class T> T viaRef(const T &r) { return r; }
template <class T> int elements(T *first) { return (int)*first; }

int main() {
    printf("%d\n", twice(21));
    printf("%.1f\n", twice(1.5));
    printf("%d\n", bigger(3, 7));
    printf("%.1f\n", bigger(2.5, 1.5));

    int k = 5;
    printf("%d\n", deref(&k));
    printf("%d\n", viaRef(9));

    const int c = 6;
    printf("%d\n", viaRef(c));

    int a[3];
    a[0] = 7;
    printf("%d\n", elements(a));

    printf("%d %d\n", twice(4), twice<int>(4));
    return 0;
}
