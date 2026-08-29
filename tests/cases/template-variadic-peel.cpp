// A partial specialization that peels one type off a pack.
//
// `template <class... Ts> struct L;` has one parameter and
// `template <class T, class... R> struct L<T, R...>` gives it two, because a
// pack stands for a list rather than for one type. Checking the count of
// written arguments against the count of parameters therefore rejected every
// recursive variadic class; what says where the pattern stops is the closing
// angle.
//
// Matching one is the other half: the arguments arrive as a single pack, so
// they are flattened and the fixed arguments matched in front, with `R...`
// taking everything left - which may be nothing, and `L<int>` is that case.
extern "C" int printf(const char *, ...);

template <class... Ts> struct L;
template <> struct L<> { int depth; };
template <class T, class... R> struct L<T, R...> { T head; int depth; };

// Peeling two, to show the fixed part is not limited to one.
template <class... Ts> struct Two;
template <class A, class B, class... R> struct Two<A, B, R...> { A a; B b; };

int main() {
    L<int, char, long> three;
    three.head = 5; three.depth = 3;

    L<int> one;
    one.head = 6; one.depth = 1;

    L<> none;
    none.depth = 0;

    Two<int, char, long, long> t;
    t.a = 7; t.b = 'k';

    printf("%d %d %d %d %d\n", three.head, three.depth, one.head, one.depth,
           none.depth);
    printf("%d %c\n", t.a, t.b);
    return 0;
}
