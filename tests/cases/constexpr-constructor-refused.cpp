// The constant evaluator folds a call to a function and has no object to
// build, so a `constexpr` object of a class type cannot be made here.
struct P { int x; constexpr P(int v) : x(v) {} };
int main() { constexpr P p(3); return p.x - 3; }
