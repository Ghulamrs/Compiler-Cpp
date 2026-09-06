// An array of a class with a constructor at file scope. Each element would
// need its constructor before main and its destructor at exit, and the
// destructor walk knows one object per entry - so it is refused by name
// where it used to be laid out as bytes with no constructor run at all.
struct S { int v; S() : v(3) {} };
S arr[4];
int main() { return arr[0].v; }
