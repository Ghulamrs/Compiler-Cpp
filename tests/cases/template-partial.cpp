// Partial specialization - rung 5.7. A second body for the argument lists
// that match a pattern.
//
// The pieces this needed already existed: the pattern read, which gives a
// template's arguments with Kind::TemplateParam still in them, and the type
// walk that matches a pattern against a real type. What is new is that the
// walk here is [temp.deduct.type] rather than [temp.deduct.call] - a pattern
// that is a pointer matches a pointer and nothing else, because there is no
// conversion here for a mismatch to be forgiven by.
//
// The tag never changes: `What<int *>` is that whether the body came from the
// template or from a pattern that matched it, which is what keeps the
// mangling and every lookup the same as rung 5.4 left them.
//
// `T **` against `T *` and `Sized<T, 4>` against the primary are the ordering
// rule at work - [temp.class.order], asked the standard's own way, by
// matching each pattern against the other.
extern "C" { int printf(const char *, ...); }

template <class T> struct Holder { T item; };

template <class T> struct What { T item; int which() { return 0; } };
template <class T> struct What<T *> { int which() { return 1; } };
template <class T> struct What<T **> { int which() { return 2; } };
template <class T> struct What<Holder<T> > { int which() { return 3; } };
template <class T> struct What<const T> { int which() { return 4; } };

template <class T, int N> struct Sized { int which() { return 0; } };
template <class T> struct Sized<T, 4> { int which() { return 4; } };
template <int N> struct Sized<char, N> { int which() { return 5; } };

int main() {
    What<int> a;
    What<int *> b;
    What<int **> c;
    What<Holder<int> > d;
    What<const int> e;
    printf("%d %d %d %d %d\n", a.which(), b.which(), c.which(), d.which(),
           e.which());

    Sized<int, 2> f;
    Sized<int, 4> g;
    Sized<char, 9> h;
    printf("%d %d %d\n", f.which(), g.which(), h.which());
    return 0;
}
