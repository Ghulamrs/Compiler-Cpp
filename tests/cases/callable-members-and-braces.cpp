// Four small language steps a real program needs, each core C++11, and each
// found by compiling one:
//
//  - `p.H(i, j)` calls the *data member* H's operator(), where `p.f(i)` calls a
//    member function f. Which it is is a question about the class: a member
//    function of that name takes the `(`, and without one the parenthesis
//    belongs to the member's own operator().
//  - `x(i)` where x is a `V &` does the same through a reference. The reference
//    is not a second type to ask about, and asking it said "not a class" and
//    reported the name undeclared.
//  - `T{}` is value-initialisation written as an expression, [expr.type.conv]/2,
//    which is what `T()` already gives.
//  - `V(x)` inside V<T>'s own members means this specialization - the injected
//    class name, [temp.local] - not the template, so it needs no argument list.
extern "C" int printf(const char *, ...);

struct Fn { int operator()(int i, int j) const { return i * 10 + j; } };
struct Holder { Fn H; };

struct Vec {
    int a[4];
    int operator()(int i) const { return a[i]; }
    void set(int i, int v) { a[i] = v; }
};
static int through(Vec &v, int i) { return v(i); }      // operator() on a reference

template <class T> T zeroOf() { return T{}; }           // braced value-init

template <class T> struct Box {
    T v;
    Box(T x) : v(x) {}
    Box twice() const { return Box(v * 2); }            // injected class name
};

int main() {
    Holder h;
    printf("%d\n", h.H(2, 3));

    Vec v;
    v.set(2, 42);
    printf("%d\n", through(v, 2));

    printf("%d %d\n", zeroOf<int>(), int{});

    Box<int> b(7);
    printf("%d\n", b.twice().v);
    return 0;
}
