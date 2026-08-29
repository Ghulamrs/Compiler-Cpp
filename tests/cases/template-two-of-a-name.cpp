// Two templates of one name. The table holds one entry per name, so the
// second used to replace nothing and simply disappear - a silently missing
// overload, which is the outcome this compiler refuses loudest. Overloading
// function templates is its own step; until then the reader is told where it
// stopped rather than finding out from a call that went somewhere else.
template <class T> T f(T x) { return x; }
template <class T> T f(T x, T y) { (void)x; return y; }
int main() { return f(1); }
