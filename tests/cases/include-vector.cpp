// `<vector>` and `<algorithm>` - a growing array, and the iterator that is a
// pointer.
//
// **The iterator being `T *` is the simplification this header rests on**, and
// it is why `++i`, `*i`, `i->m` and `i != v.end()` are not written anywhere in
// it: they are the built-in operations on a pointer. A conforming library wraps
// the pointer in a class so that a `vector<int>` iterator cannot be compared
// against a `vector<char>` one; this does not, and the cost is written down in
// the header.
//
// Two faults were found by the shapes below and both are cases of their own now
// - `const-class-copy.cpp` for a `vector<Node>` where Node is four bytes, and
// `template-nested.cpp` for a vector inside a map. What is checked here is the
// container itself: growth past its initial capacity, copying, assignment, the
// iterator, a vector of a class, and a vector of vectors.
//
// The names are not compared against clang's for this case, and cannot be:
// clang reads its own `<vector>`. Behaviour is the oracle - see
// include-vector.nonames.

#include <vector>
#include <algorithm>
extern "C" int printf(const char *, ...);
struct Node { int id; int twice(void) const { return id * 2; } };
int main(void) {
    std::vector<int> v;
    for (int i = 0; i < 10; i++) v.push_back(i * i);
    printf("%d %d %d %d\n", (int)v.size(), v[0], v[9], v.back());
    v[3] = 99;
    v.pop_back();
    printf("%d %d %d\n", v[3], (int)v.size(), v.empty());

    std::vector<int> w = v;                       // copy construction
    std::vector<int> u;
    u = v;                                        // assignment
    u.push_back(1000);
    printf("%d %d %d\n", (int)w.size(), (int)u.size(), u.back());

    int total = 0;
    for (std::vector<int>::iterator i = v.begin(); i != v.end(); ++i) total += *i;
    printf("%d\n", total);

    std::vector<Node> nodes;
    Node n; n.id = 7;
    nodes.push_back(n);
    printf("%d %d\n", nodes[0].id, nodes.begin()->twice());

    std::vector<std::vector<int> > grid;
    std::vector<int> row;
    row.push_back(5);
    grid.push_back(row);
    printf("%d %d\n", (int)grid.size(), grid[0][0]);
    return 0;
}
