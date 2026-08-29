// A base written as a template-id, `struct B : A<T>`.
//
// The base clause used to read a bare identifier and look it up as a typedef,
// which a template-id can never be: the class does not exist until it is
// instantiated. Reading the base as a *type* instead reaches the same code a
// declaration does, and that code already knows how to instantiate one.
//
// Inside a template this costs nothing extra, because a pattern is never
// parsed - instantiation replays the tokens with the parameters bound, so
// `A<T>` is read as `A<int>` at the point where T is known.
//
// **No `long` anywhere, because this prints sizeof.** `long` is 8 bytes on the
// two Itanium targets and 4 on Windows - LP64 against LLP64 - so a case that
// measures a layout containing one measures the data model instead, and the
// same .expected cannot serve all three. `double` is 8 everywhere and says
// what was meant.
extern "C" int printf(const char *, ...);

template <class T> struct Holder { T value; int tag; };
template <class T> struct Named : Holder<T> { int id; };
template <class T> struct Deeper : Named<T> { int extra; };

struct Concrete : Holder<char> { int mine; };

int main() {
    Deeper<double> d;
    d.value = 11; d.tag = 2; d.id = 3; d.extra = 4;
    printf("%.0f %d %d %d %d\n", d.value, d.tag, d.id, d.extra, (int)sizeof d);

    Named<char> n;
    n.value = 'q'; n.tag = 1; n.id = 9;
    printf("%c %d %d %d\n", n.value, n.tag, n.id, (int)sizeof n);

    Concrete c;
    c.value = 'w'; c.tag = 5; c.mine = 6;
    printf("%c %d %d %d\n", c.value, c.tag, c.mine, (int)sizeof c);
    return 0;
}
