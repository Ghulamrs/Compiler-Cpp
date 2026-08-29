// [temp.expl.spec]: a specialization has to be declared before the first use
// that would instantiate the template. If one already did, two different
// classes have been given one name - so this is an error about *where* the
// specialization is, not a redefinition, and the message says so.
template <class T> struct N { T v; };
int use() { N<int> a; a.v = 1; return a.v; }
template <> struct N<int> { int v; };
int main() { return use(); }
