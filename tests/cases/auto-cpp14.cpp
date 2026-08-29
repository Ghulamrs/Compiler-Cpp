// `auto` in a parameter is C++14 and this compiler is C++11, so it is refused
// by name and says which standard it belongs to. A deduced *return* type is
// the same story and refuses the same way.
int f(auto x) { return 1; }
int main() { return f(1); }
