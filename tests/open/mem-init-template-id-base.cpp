// A mem-initialiser naming a template-id base - `D(int x) : P<int>(x) {}` -
// does not parse: cxx1 answers "expected '('" at the `<`, so a class deriving
// from a specialization cannot pass its base an argument. The same shape with
// a plain base works, and so does deriving from P<int> without a
// mem-initialiser where the base has a default constructor.
extern "C" int printf(const char *, ...);
template <class T> struct P { T v; P(T x) : v(x) {} };
struct D : P<int> { D(int x) : P<int>(x) {} };
int main() { D d(7); printf("%d\n", d.v); return 0; }
