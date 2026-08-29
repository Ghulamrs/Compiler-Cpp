// A range-based `for` over a class. Its own step and refused by name: the
// loop has to find `begin` and `end` on the class, resolve them like any
// other member call and build the calls, where an array's bounds are in its
// type and need looking up nowhere.
struct C {
    int *begin();
    int *end();
};
int main() { C c; for (auto v : c) { (void)v; } return 0; }
