// The C++11 restriction on a constexpr function body: one return statement
// and nothing else. This body declares a local first, which C++14 allows and
// C++11 does not - and this compiler is C++11, so it is refused by name
// rather than quietly accepted and then found not to be constant.
constexpr int twice(int x) { int doubled = x + x; return doubled; }
int main() { return twice(2); }
