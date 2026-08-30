// `explicit operator bool()`, refused - and not because of `explicit`.
//
// A conversion function is the other place C++11 allows the keyword, and this
// compiler has no conversion functions at all yet, explicit or otherwise. The
// message says so rather than reporting that `explicit` applies to a
// constructor, which would send the reader to fix the wrong half.
struct Flag {
    int v;
    explicit operator bool(void) const { return v != 0; }
};

int main(void) {
    Flag f;
    f.v = 1;
    return f ? 1 : 0;
}
