// `explicit` on something that is not a constructor, refused.
//
// C++11 allows it on a constructor and on a conversion function, and nothing
// else. A conversion function is named separately where that is what was
// written - saying "a constructor" there would send the reader looking for the
// wrong mistake - but an ordinary member function gets this.
struct Reader {
    int v;
    explicit int get(void) { return v; }
};

int main(void) {
    Reader r;
    r.v = 1;
    return r.get();
}
