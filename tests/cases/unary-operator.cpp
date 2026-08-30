// The unary operators, which until now could be named and not reached.
//
// Two things here are worth more than the arithmetic. **`&` is only an
// overload when the class declared one** - a class that did not still has an
// address, and `&w` below is the ordinary address-of it has always been, which
// is why the dispatch answers "carry on with the built-in" rather than
// refusing when it finds no candidate.
//
// And **the postfix increment's dummy `int` is a real parameter**. [over.inc]
// gives the postfix form an extra int and passes 0 in it, which is the whole
// of how the two forms are told apart - so `v++` is resolved with two operands
// and `++v` with one, and the same machinery answers both.
extern "C" { int printf(const char *, ...); }

class V {
public:
    int x;
    V(int n);
    int operator-() const;
    int operator+() const;
    int operator!() const;
    int operator~() const;
    int operator*() const;
    int operator&() const;
    int operator++();
    int operator++(int d);
    int operator--();
    int operator--(int d);
};

V::V(int n) { x = n; }
int V::operator-() const { return 1 + 0 * x; }
int V::operator+() const { return 2 + 0 * x; }
int V::operator!() const { return 3 + 0 * x; }
int V::operator~() const { return 4 + 0 * x; }
int V::operator*() const { return 5 + 0 * x; }
int V::operator&() const { return 6 + 0 * x; }
int V::operator++()      { return 7 + 0 * x; }
int V::operator++(int d) { return 8 + 0 * (x + d); }
int V::operator--()      { return 9 + 0 * x; }
int V::operator--(int d) { return 10 + 0 * (x + d); }

// No operator& here, so &w is an address.
class W {
public:
    int y;
};

int main(void) {
    V v(1);
    W w;
    W *p;
    w.y = 11;
    p = &w;
    printf("%d %d %d %d %d %d\n", -v, +v, !v, ~v, *v, &v);
    printf("%d %d %d %d %d\n", ++v, v++, --v, v--, p->y);
    return 0;
}
