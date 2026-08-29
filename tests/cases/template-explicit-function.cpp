// An explicit specialization of a *function* template. Its own step and
// refused by name until then: a class specialization is a class definition,
// which the class path already reads, while a function one has to be given
// the primary's pattern to be mangled from and cannot be read as an ordinary
// definition.
template <class T> T twice(T x) { return x + x; }
template <> int twice<int>(int x) { return x * 3; }
int main() { return twice(1); }
