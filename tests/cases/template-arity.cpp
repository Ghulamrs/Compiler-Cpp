// The argument list is read against the parameter list it is for - which is
// what decides whether an argument is a type or a value - so a list of the
// wrong length is caught where it is written.
template <class T, int N> T scaled(T x) { return x * N; }
int main() { return scaled<int>(2); }
