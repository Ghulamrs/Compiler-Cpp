// A real tuple: recursive variadic inheritance, which is the shape the
// standard library's own is built from. Four things had to exist for this to
// compile, and each is a separate case beside this one:
//
//   * a partial specialization that peels one type off a pack,
//     `Tuple<T, Rest...>` - template-variadic-peel.cpp
//   * a pack expanded into another template's argument list, `Tuple<Rest...>`
//     - template-variadic-into-args.cpp
//   * a dependent base, `: Tuple<Rest...>` - template-dependent-base.cpp
//   * the empty base optimisation, without which every level of the recursion
//     pays a byte for the empty Tuple<> at the bottom - empty-base.cpp
//
// And one rule of ordinary C++ that nothing had needed before: every level
// declares `head`, so the derived one has to hide the base's.
//
// **No `long` anywhere, because this prints sizeof.** `long` is 8 bytes on the
// two Itanium targets and 4 on Windows - LP64 against LLP64 - so a case that
// measures a layout containing one measures the data model instead, and the
// same .expected cannot serve all three. `double` is 8 everywhere and says
// what was meant.
extern "C" int printf(const char *, ...);

// **The size of the three-element tuple depends on the ABI, so it is pinned at compile time
// instead of printed.** Itanium lets a derived class into a base's tail
// padding when the base is not a POD; the Microsoft ABI never does. One
// `.expected` cannot hold both answers, and a static_assert is the better
// home anyway: emit.sh checks it for all three targets from whichever box is
// running, where a printed number is only ever checked on the host.


template <class... Ts> struct Tuple;
template <> struct Tuple<> { };

template <class T, class... Rest>
struct Tuple<T, Rest...> : Tuple<Rest...> {
    T head;
    Tuple<Rest...> *tail() { return this; }
};

#ifdef _WIN32
static_assert(sizeof(Tuple<int, char, double>) == 24, "cl reuses no tail padding");
#else
static_assert(sizeof(Tuple<int, char, double>) == 16, "each level sits in the one below's padding");
#endif

int main() {
    Tuple<int, char, double> t;
    t.head = 7;
    t.tail()->head = 'x';
    t.tail()->tail()->head = 99;
    printf("%d %c %.0f\n", t.head, t.tail()->head,
           t.tail()->tail()->head);

    Tuple<int> one;
    one.head = 3;
    printf("one=%d size=%d\n", one.head, (int)sizeof one);

    Tuple<char, char, char, char> four;
    four.head = 'a';
    four.tail()->head = 'b';
    four.tail()->tail()->head = 'c';
    four.tail()->tail()->tail()->head = 'd';
    printf("%c%c%c%c size=%d\n", four.head, four.tail()->head,
           four.tail()->tail()->head, four.tail()->tail()->tail()->head,
           (int)sizeof four);

    printf("empty=%d\n", (int)sizeof(Tuple<>));
    return 0;
}
