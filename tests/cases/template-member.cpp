// A member template - a template written inside a class. Its own step, and
// refused by name until then; without this the `template` keyword would reach
// the member loop as something that is not a type.
struct Holder {
    int n;
    template <class T> T get(T x) { return x; }
};
int main() { return 0; }
