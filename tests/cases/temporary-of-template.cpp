// **`std::vector<T>().swap(v)`** - the shortest way to empty a vector and give
// its buffer back, and the last line of Compiler++ that cxx1 could not read.
// Three things were in the way, and only the first is about templates.
//
// **A statement may begin with a type.** `atDeclarationStart` saw a type name
// and claimed the line, so the declarator path then wanted a name where the
// `(` was. An **empty pair** after the type is what says otherwise: a
// declaration needs a name between the type and the `(`, so `T ();` could only
// be a function declaration with no name, which is not a thing. `int (*p)();`
// has a `*` there and is untouched.
//
// **The walk over a qualified name stops at the `<`**, because for its own
// purpose the *name* is what matters - so the empty pair was looked for at the
// wrong token. `qualifiedTypeEndPastArgs` steps over the argument list by
// depth, counting a `>>` as two, and both callers ask it now.
//
// **And a class template-id is a temporary where a plain class name already
// was.** `refuseTemplateId` answered for every template in an expression; a
// class template followed by `(` reaches `classTemporary` instead, which is
// the same function `P(1)` has always used.
#include <vector>
#include <string>

extern "C" int printf(const char *, ...);

struct P { int n; P(); int get() const; };
P::P() : n(3) {}
int P::get() const { return n; }

int main() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);

    // The line that wanted all three.
    std::vector<int>().swap(v);
    printf("swapped %d\n", (int)v.size());

    // A plain class temporary as a statement, which failed for the first
    // reason alone.
    P().get();
    // And in an expression, which always worked - both spellings here so the
    // statement form is not the only thing holding the branch up.
    printf("%d %d\n", P().get(), (int)std::string("abcd").size());

    // A declaration that still is one: the empty pair is the whole test, and
    // this has a declarator between the type and the parentheses.
    int (*fp)() = 0;
    printf("%d\n", fp == 0 ? 1 : 0);
    return 0;
}
