// Explicit specialization - rung 5.6. A class written out for one argument
// list instead of made from the template.
//
// Nothing about the class path changes. The tag is `Name<int>` here exactly
// as it would be if the template had produced it, so every use finds this one
// through the same lookup, both manglers spell it the same way, and its
// members are keyed the same. What differs is only where the body came from -
// which is why a specialization may have members the template does not, like
// `extra` below.
//
// The argument list is read against the *primary's* parameters, which is what
// decides whether an argument is a type or a value. That is the same rule
// every other use follows, and it is why the primary has to come first.
extern "C" { int printf(const char *, ...); }

template <class T> struct Name {
    T item;
    int which() { return 0; }
};
template <> struct Name<int> {
    int item;
    int which() { return 1; }
    int extra() { return 42; }
};

template <class T, int N> struct Box {
    T slot[N];
    int size() { return N; }
};
// A non-type argument in a specialization, and a size that disagrees with
// the template's on purpose - so the program says which one it reached.
template <> struct Box<char, 4> {
    char slot[4];
    int size() { return -4; }
};

int main() {
    Name<double> d;
    d.item = 1.5;
    Name<int> i;
    i.item = 2;
    printf("%d %d %d\n", d.which(), i.which(), i.extra());

    Box<int, 3> b;
    Box<char, 4> c;
    printf("%d %d\n", b.size(), c.size());
    return 0;
}
