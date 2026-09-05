// **A default template argument, [temp.param]/9.** A parameter may carry a
// default, and a use that omits it takes that default - replayed from the
// tokens the parameter kept, with the earlier parameters bound so a later
// default can name an earlier one. That last part is the whole reason it is
// kept as tokens and not folded once: `N = M` cannot be evaluated until `M`
// has a value, which is per-use.
//
// Refused by name until now - it was its own step past the rest of rung 5. The
// three shapes measured against clang: a non-type default, a non-type default
// that names an earlier parameter, and a type default that is actually used.
extern "C" int printf(const char *, ...);

template <int N = 3>
struct Dim {
    int value() const { return N; }
};

template <int Rows = 3, int Cols = Rows>
struct Shape {
    int rows() const { return Rows; }
    int cols() const { return Cols; }
};

template <class T = int>
struct Box {
    T v;
    Box(T x) : v(x) {}
    T get() const { return v; }
};

int main() {
    Dim<> a;          // 3, the default
    Dim<7> b;         // 7, given

    Shape<> p;        // 3 x 3, both defaults
    Shape<4> q;       // 4 x 4, Cols defaults to Rows
    Shape<4, 9> r;    // 4 x 9, both given

    Box<> bi(5);          // int, the default
    Box<double> bd(2.5);  // double, given

    printf("%d %d | %dx%d %dx%d %dx%d | %d %g\n",
           a.value(), b.value(),
           p.rows(), p.cols(), q.rows(), q.cols(), r.rows(), r.cols(),
           bi.get(), bd.get());
    return 0;
}
