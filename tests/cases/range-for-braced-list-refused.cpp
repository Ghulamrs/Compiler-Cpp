// [stmt.ranged] binds the range to `auto &&`, and for braces that makes an
// `std::initializer_list`. The header exists here and the class it declares is
// not something a range-for can take begin() and end() from, so the refusal
// names the list rather than leaving "expected an expression" at the brace.
#include <initializer_list>

int main() {
    int t = 0;
    for (int x : {0, 0}) t += x;
    return t;
}
