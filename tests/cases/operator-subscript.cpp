// `operator[]` - subscripting a class.
//
// **A class is not an array, so there is no built-in meaning to fall back on**:
// [over.sub] gives subscripting no non-member form, which makes the member the
// only candidate and makes a class subscripted without one an error rather than
// pointer arithmetic on its first byte.
//
// What the operator answers is a reference, and that is the whole of why it is
// worth having: `v[0] = 3` and `v[0] + 1` are the same call, differing only in
// what is done with the reference afterwards. The const overload is chosen on
// the object's constness like any other member, which is what lets a const
// container be read and not written.

extern "C" int printf(const char *, ...);

struct Arr {
    int a[4];
    int &operator[](int i) { return a[i]; }
    const int &operator[](int i) const { return a[i]; }
};

int readOnly(const Arr &r) { return r[2]; }          // the const overload

struct Chars {
    char text[8];
    char &operator[](unsigned long i) { return text[i]; }   // and another index type
};

int main(void) {
    Arr v;
    v[0] = 3;
    v[1] = 5;
    v[2] = 7;
    v[1] = v[1] + 10;                                // read and written in one line
    Chars c;
    c[0] = 'h';
    c[1] = 'i';
    c[2] = 0;
    printf("%d %d %d %d %s\n", v[0], v[1], v[2], readOnly(v), c.text);
    return 0;
}
