// A constructor of a class template written outside the class. Refused by
// name: the declarator reads a class *name* before the `::`, and a
// constructor has no return type in front of it for the ordinary qualified
// path to start from - so recognising a template-id there is its own step.
// A member function is different and works, because its return type gets the
// declarator started.
template <class T> struct Box {
    T v;
    Box();
};
template <class T> Box<T>::Box() { v = 0; }
int main() { Box<int> b; return b.v; }
