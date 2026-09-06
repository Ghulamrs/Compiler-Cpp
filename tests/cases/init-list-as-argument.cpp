// **A braced list written as the argument of a construction**, `Row r({1, 2})`.
// [dcl.init]/16 makes it a direct-initialisation whose single argument is a
// braced-init-list, and [over.match.list] then picks the initializer_list
// constructor - the same one `Row r{1, 2}` reaches, written the other way.
//
// cxx1 read the braces as an expression and refused them: an argument's target
// type is not known until a candidate is chosen, so parseArguments cannot build
// a list on its own. It is handled where the target type *is* known, which is
// the declaration, and the nested form falls out of it - the element type of
// `initializer_list<Row>` is known too, so `{ {1, 2}, {3, 4} }` builds a Row
// per inner brace.
//
// What is still refused, and named rather than approximated: the same braces in
// a functional-cast temporary, `Row({1, 2})` as an expression. Building the
// list emits statements that fill a backing array, and an expression has
// nowhere to put them - it would need them folded into a comma, which is an
// AST change rather than a parser one. init-list-as-argument-temporary-refused
// pins that.
#include <initializer_list>
extern "C" int printf(const char *, ...);

struct Row {
    int a, b;
    Row(std::initializer_list<int> l) {
        const int *p = l.begin();
        a = p[0];
        b = l.size() > 1 ? p[1] : 0;
    }
};

struct Grid {
    int total;
    Grid(std::initializer_list<Row> l) {
        total = 0;
        for (const Row *p = l.begin(); p != l.end(); ++p) total += p->a + p->b;
    }
};

int main(void) {
    Row one({7});                       // one element
    Row two({1, 2});                    // two
    Grid nested({ {1, 2}, {3, 4} });    // a list of lists
    Row braced{5, 6};                   // the spelling that always worked
    printf("%d %d %d %d %d\n", one.a, two.a, two.b, nested.total, braced.b);
    return 0;
}
