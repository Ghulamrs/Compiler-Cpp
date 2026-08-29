// Non-type template arguments, and the `>` that is not an operator.
//
// [temp.names]: inside a template argument list a `>` closes the list, which
// is exactly why C++ makes a comparison there need parentheses. The
// parentheses are where the rule is switched off again - so `(3 > 2 ? 5 : 1)`
// below is a conditional and the `>` after it is the end of the list, and
// getting that the other way round would either misread this line or refuse
// every ordinary comparison in the file.
//
// A negative argument is worth its own line: Itanium spells it `Lin2E` and
// the Microsoft ABI `$0?1`, and both were measured rather than guessed.
extern "C" { int printf(const char *, ...); }

template <class T, int N> T scaled(T x) { return x * N; }
template <int N> int constant() { return N; }

int main() {
    printf("%d\n", scaled<int, 3>(4));
    printf("%d\n", scaled<int, (3 > 2 ? 5 : 1)>(2));
    printf("%d\n", scaled<int, (1 << 3)>(2));
    printf("%d\n", scaled<int, -2>(3));
    printf("%d %d %d\n", constant<0>(), constant<11>(), constant<-11>());
    return 0;
}
