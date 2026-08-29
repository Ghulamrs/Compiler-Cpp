// Expanding a pack into another template's argument list, `Tuple<Ts...>`,
// where the members are known. This is what makes a recursive variadic class
// possible: each step passes on everything but the head.
//
// What made it awkward is that a template argument list is read against a
// parameter list which decides what each argument *means*, so a pack in the
// middle changes how many arguments there are before that reading starts. It
// works because a pack argument is read last and takes everything left, so an
// expansion splices its members in where it stands and the closing angle
// settles the count rather than the parameter list doing it in advance.
//
// An expansion may contribute nothing, and `Wrapper<>` is that case - which
// is also why the comma between arguments cannot be decided by asking whether
// anything has been collected yet.
extern "C" int printf(const char *, ...);

template <class... Ts> struct Tuple { int n; };

template <class... Ts> struct Wrapper {
    Tuple<Ts...> held;
    int tag;
};

int main() {
    Wrapper<int, char> two;
    two.held.n = 5;
    two.tag = 1;

    Wrapper<> none;
    none.held.n = 6;
    none.tag = 2;

    printf("%d %d %d %d\n", two.held.n, two.tag, none.held.n, none.tag);
    return 0;
}
