// `[n = k]` introduces a name of the closure's own, which is C++14. Left to
// the capture lookup it would be reported as a name that does not exist -
// true, and no help to whoever wrote it.
int main() { int k = 0; auto f = [n = k]() { return n; }; return f(); }
