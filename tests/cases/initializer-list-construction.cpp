// **List-initialisation of a class with an initializer_list constructor**,
// [over.match.list]/1 and [dcl.init.list]/5. A brace-init-list becomes a
// backing array of the elements and a std::initializer_list referring to it -
// a pointer and a count, the two members <initializer_list> lays out - and that
// one argument selects the constructor. The array is a local of the enclosing
// scope, so it outlives the list.
//
// Refused by name until now: the type existed but the language step that builds
// one did not. Both shapes measured against clang - a flat list, and a nested
// one where each inner list builds an element the outer list then holds.
#include <initializer_list>
extern "C" int printf(const char *, ...);

struct Row {
    int a[4];
    int n;
    Row() : n(0) { for (int i = 0; i < 4; ++i) a[i] = 0; }
    Row(std::initializer_list<int> l) {
        n = 0;
        for (std::initializer_list<int>::const_iterator it = l.begin();
             it != l.end(); ++it)
            a[n++] = *it;
    }
    int sum() const { int s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }
};

struct Grid {
    Row rows[4];
    int m;
    Grid(std::initializer_list<Row> l) {
        m = 0;
        for (std::initializer_list<Row>::const_iterator it = l.begin();
             it != l.end(); ++it)
            rows[m++] = *it;
    }
};

int main() {
    Row r = {10, 20, 30};
    printf("flat: n=%d sum=%d\n", r.n, r.sum());

    Row empty = {};
    printf("empty: n=%d\n", empty.n);

    Grid g = { {1, 2, 3}, {4, 5, 6, 7}, {8} };
    printf("nested: m=%d | %d %d %d\n", g.m, g.rows[0].sum(), g.rows[1].sum(),
           g.rows[2].sum());
    return 0;
}
