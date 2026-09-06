// The neighbour that is not built, refused by name rather than approximated.
//
// `Row({1, 2})` as an *expression* is a functional-cast temporary whose single
// argument is a braced-init-list - the same [over.match.list] pick that
// `Row r({1, 2})` makes, in a place with nowhere to put the setup. Building an
// initializer_list emits statements that declare a backing array and fill it,
// and a declaration has a statement list to receive them where an expression
// does not. Folding them into a comma is an AST change rather than a parser
// one: ExprStmt hands out its expression by const reference, so nothing can
// take it back out.
//
// Written down because the message used to be `expected an expression`, which
// says nothing about which feature is missing. C++ Vector Exercise's
// matrix_main.cpp is what needs it: `Mat2({ {1, 2}, {3, 4} }) | Mat2::identity()`.
#include <initializer_list>
extern "C" int printf(const char *, ...);

struct Row { int a; Row(std::initializer_list<int> l) { a = *l.begin(); } };

static int take(const Row &r) { return r.a; }

int main(void) { return take(Row({4})); }
