// A class template with a non-type parameter, and what a small numeric
// library is built out of.
//
// This is SparseMatrix/QPSolver reduced to the shapes a compiler has to get
// right for it: a class template whose second parameter is an `int`, arrays
// sized by that parameter, `operator()` taking two indices, out-of-line
// definitions of its members, and a static member function of a class
// template called with its arguments written out.
//
// **The infinities are the part worth pinning.** `numeric_limits<T>` is a
// specialization per type, and an infinity reaching an `ostream` is a
// formatting path no ordinary double takes - `inf` and `-inf` are printed by
// the stream rather than computed. A bound left open is exactly how a
// constraint set says "unbounded", which is where this came from.
//
// **No constructor, and that is deliberate twice over.** A constructor of a
// class template written outside the class is refused by name here, and one
// written inside is inline - where clang emits only C2 on x86_64-linux and
// names.sh would report an emission difference as though it were a mangling
// one. `reset` is an ordinary member, defined out of line, which is neither.
#include <iostream>
#include <limits>

template <class T, int CAP>
struct Sparse {
    int rows, cols, nnz;
    int at[CAP];
    T val[CAP];
    void reset(int r, int c);
    void set(int r, int c, T v);
    T operator()(int r, int c) const;
    int capacity() const { return CAP; }
};

template <class T, int CAP>
void Sparse<T, CAP>::reset(int r, int c) {
    rows = r; cols = c; nnz = 0;
    for (int k = 0; k < CAP; k++) { at[k] = -1; val[k] = T(); }
}

template <class T, int CAP>
void Sparse<T, CAP>::set(int r, int c, T v) {
    if (nnz < CAP) { at[nnz] = r * cols + c; val[nnz] = v; nnz++; }
}

template <class T, int CAP>
T Sparse<T, CAP>::operator()(int r, int c) const {
    for (int k = 0; k < nnz; k++) if (at[k] == r * cols + c) return val[k];
    return T();
}

// A static member of a class template, reached with its arguments written
// out rather than deduced - QPSolver<...>::solve is the shape.
template <class T, int CAP>
struct Reduce {
    static T trace(const Sparse<T, CAP> &m);
};

template <class T, int CAP>
T Reduce<T, CAP>::trace(const Sparse<T, CAP> &m) {
    T sum = T();
    for (int i = 0; i < m.rows && i < m.cols; i++) sum = sum + m(i, i);
    return sum;
}

int main() {
    Sparse<double, 8> m;
    m.reset(3, 3);
    m.set(0, 0, 1.5);
    m.set(1, 1, 2.25);
    m.set(2, 0, 4.0);

    std::cout << "capacity " << m.capacity() << " nnz " << m.nnz << "\n";
    for (int i = 0; i < m.rows; i++) {
        for (int j = 0; j < m.cols; j++) std::cout << m(i, j) << " ";
        std::cout << "\n";
    }
    std::cout << "trace " << Reduce<double, 8>::trace(m) << "\n";

    Sparse<int, 4> k;
    k.reset(2, 2);
    k.set(0, 1, 7);
    std::cout << "int capacity " << k.capacity() << " (0,1) " << k(0, 1)
              << " (1,0) " << k(1, 0) << "\n";

    const double lo = -std::numeric_limits<double>::infinity();
    const double hi = std::numeric_limits<double>::infinity();
    std::cout << "bounds " << lo << " " << hi << "\n";
    std::cout << "unbounded below " << (lo < -1e300 ? 1 : 0)
              << " above " << (hi > 1e300 ? 1 : 0) << "\n";
    return 0;
}
