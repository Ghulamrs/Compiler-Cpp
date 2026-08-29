// A non-type parameter is not deduced from a call - there is nothing in an
// argument's type to read a value out of. Refused by name, and by the name of
// the parameter that cannot be worked out.
template <class T, int N> T scaled(T x) { return x * N; }
int main() { return scaled(2); }
