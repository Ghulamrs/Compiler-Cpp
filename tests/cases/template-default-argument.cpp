// Refused by name rather than misread. A default template argument is its own
// step and is not written yet, and the reader is told that at the `=` rather
// than twenty tokens later.
template <class T = int> T identity(T x) { return x; }
int main() { return 0; }
