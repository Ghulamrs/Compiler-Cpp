// A variable template is C++14. It is told from the C++11 declarations by a
// token scan: a class or function reaches a '(' or a class key first, and an
// out-of-line member writes a '::' before its '='.
template <class T> T zero = T();
int main() { return zero<int>; }
