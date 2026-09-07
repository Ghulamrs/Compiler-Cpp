// A range-based `for` whose iterator is a class, refused.
//
// Every container in `include/` returns a **pointer** from `begin()`, and the
// loop cxx1 builds is the pointer loop: `__b != __e` is a pointer comparison,
// `++__b` is pointer arithmetic and `*__b` is a load. A class iterator needs
// all three resolved as overloaded operators instead - each of which this
// compiler has, and none of which this loop has been measured against.
//
// clang compiles this and prints `1 2`. Refused by name, and the message names
// the three operators, because that is what the next step has to do.

extern "C" int printf(const char *, ...);

struct Cursor {
    int *p;
    int &operator*() { return *p; }
    bool operator!=(const Cursor &o) const { return p != o.p; }
    Cursor &operator++() { ++p; return *this; }
};

struct Bag {
    int d[2];
    Cursor begin() { Cursor c; c.p = d; return c; }
    Cursor end() { Cursor c; c.p = d + 2; return c; }
};

int main(void) {
    Bag b;
    b.d[0] = 1; b.d[1] = 2;
    for (int n : b) printf("%d ", n);
    printf("\n");
    return 0;
}
