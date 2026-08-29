// Deducing a template's arguments from the call is rung 5.3 and is refused by
// name until then. Without this the name would reach expression parsing as an
// ordinary identifier and the reader would be told that `twice` was never
// declared, which is both false and unhelpful.
template <class T> T twice(T x) { return x + x; }
int main() { return twice(21); }
