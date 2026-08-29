// A function template named where a type was expected. Only a *class*
// template with its arguments is a type, so this reaches expression parsing,
// where naming a function template without calling it is refused by name.
// Either way the reader is told what `twice` is rather than being handed
// "expected a type" and left to work it out.
template <class T> T twice(T x) { return x + x; }
int main() { twice<int> x; (void)x; return 0; }
