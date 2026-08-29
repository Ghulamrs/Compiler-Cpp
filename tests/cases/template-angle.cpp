// The `<` ambiguity, which is what rung 5.1 is really about.
//
// `f<int>(x)` and `a<b>(c)` are the same shape, and the only thing that tells
// them apart is whether `f` names a template. So the table of template names
// has to exist before any `<` is read, and a `<` opens an argument list only
// for a name that is in it - never on shape alone. This file declares four
// templates and then writes `<`, `>` and `>>` where none of them is a
// template-id, so every one of these must still be an operator.
//
// Nothing here is instantiated: 5.1 records the templates and stops. What is
// being checked is that their presence changed the meaning of no expression.
extern "C" { int printf(const char *, ...); }

template <class T> T twice(T x) { return x + x; }
template <typename T, int N> struct Box { T slot[N]; };
template <class T> T later(T x);          // declared here
template <class T> T later(T x) { return x; }   // and defined here

int main() {
    int a = 1, b = 2, c = 3;
    printf("%d %d\n", (a < b), (c > b));
    printf("%d %d\n", (a <= b), (c >= b));
    printf("%d %d\n", 1 << 4, 256 >> 3);
    // A `>>` where an argument list would have ended, twice over.
    printf("%d\n", (c >> 1) << 1);
    // And a name that is a template used as nothing else - `twice` is not a
    // variable here, so a declaration may still use the identifier freely.
    int later2 = a << b >> a;
    printf("%d\n", later2);
    return 0;
}
