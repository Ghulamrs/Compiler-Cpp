// `decltype(e)` is C++11 and `decltype(auto)` is C++14, told apart by one
// token. The C++11 form is built here, which is why the C++14 one has to be
// named rather than left to fail as a missing expression.
int main() { int n = 0; decltype(auto) r = n; return r; }
