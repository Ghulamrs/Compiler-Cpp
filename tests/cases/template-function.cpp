// Function templates with their arguments written out - rung 5.2.
//
// A specialization is made by replaying the template's own tokens with the
// parameters bound: a type parameter becomes a type name and a non-type one
// an enumerator, so the body is read by the ordinary parser with no second
// lookup path. The body cannot be written where the call is, because the call
// is in the middle of another function, so the request is recorded and the
// definitions are replayed afterwards - to a fixed point, since `quad` here
// asks for `twice` while it is itself being instantiated.
//
// `twice<int>` is asked for three times and must be one function: the key is
// the template's name and its arguments, and a repeat finds the entry that is
// already there.
extern "C" { int printf(const char *, ...); }

struct Point { int x; int y; };

template <class T> T twice(T x) { return x + x; }
template <class T> T bigger(T a, T b) { return a > b ? a : b; }
template <class T> T quad(T x) { return twice<T>(twice<T>(x)); }
template <class T> int size() { return sizeof(T); }
template <class T> T byValue(T p) { return p; }
template <class T> const T &pick(const T &a) { return a; }

int main() {
    printf("%d\n", twice<int>(21));
    printf("%.1f\n", twice<double>(1.5));
    printf("%d\n", bigger<int>(3, 7));
    printf("%d\n", quad<int>(5));
    printf("%d %d %d\n", size<char>(), size<int>(), size<double>());

    Point p;
    p.x = 4;
    p.y = 9;
    Point q = byValue<Point>(p);
    printf("%d %d\n", q.x, q.y);
    printf("%d\n", pick<int>(11));

    // One specialization, asked for twice more.
    printf("%d\n", twice<int>(1) + twice<int>(2));
    return 0;
}
