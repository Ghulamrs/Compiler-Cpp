// **`= default` and `= delete` are C++11 and sit exactly where `= 0` does.**
// A constructor and a member function reach that position by different doors -
// declareConstructor and the member-function tail - so one helper answers for
// both, which is what this case and its neighbour hold in place.
struct S { S() = default; int v; };
int main() { S s; s.v = 0; return s.v; }
