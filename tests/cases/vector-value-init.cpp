// **`vector<T> v(n)` is n value-initialised elements** - zero for a scalar, the
// default constructor for a class - and `explicit`, as the standard makes it,
// so `vector<int> v = 5;` is refused rather than read as a vector of five.
// `resize(n)` is the same primitive and was here already, but wrong: it moved
// `size_` and left the new elements as whatever `calloc` gave, which is zero
// bytes rather than a constructed object. For a scalar the two are the same
// answer and for a class they are not.
//
// **And a zeroed slot is a real state for a `string`.** This library's `vector`
// assigns into raw storage - placement new is refused by this compiler, so
// there is no way to construct an element in place - so `string::reserve` sees
// a null buffer, zero length and zero capacity. It used to do nothing for
// `reserve(0)`, and the `buf_[len_] = 0` that follows every write then went to
// a null pointer: `std::vector<std::string> s(2);` crashed.
#include <vector>
#include <string>

extern "C" int printf(const char *, ...);

struct P { int n; P(); };
P::P() : n(7) {}

int main() {
    std::vector<int> a(4);
    printf("ints %d:", (int)a.size());
    for (std::size_t i = 0; i < a.size(); i++) printf(" %d", a[i]);
    printf("\n");

    std::vector<P> p(3);
    printf("class %d %d %d\n", (int)p.size(), p[0].n, p[2].n);

    // The one that crashed: an element whose default state owns a buffer.
    std::vector<std::string> s(2);
    printf("strings %d [%s] %d\n", (int)s.size(), s[0].c_str(), (int)s[1].size());

    std::vector<int> g;
    g.resize(3);
    printf("grow %d %d %d\n", (int)g.size(), g[0], g[2]);
    g.resize(1);
    printf("shrink %d\n", (int)g.size());

    std::vector<int> z(0);
    printf("zero %d\n", (int)z.size());
    return 0;
}
