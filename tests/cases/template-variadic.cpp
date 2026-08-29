// Variadic templates - the last step of rung 5.
//
// A pack stands for a list, which no pattern type could say before: it is
// bound to the types it was given rather than to a type, and what reads it is
// `Ts...`, `rest...` and `sizeof...`. Only the last parameter may be one,
// because a pack takes every argument that is left and anything after it
// could never be given a value.
//
// **`Ts... rest` is one thing written and several parameters made.** In a
// pattern it is one parameter of type `Ts...`, which Itanium spells `DpT0_`
// and says at every size - that is what lets one pattern serve every
// specialization. In a real instantiation it is as many parameters as the
// pack has members, named `rest$0`, `rest$1`, and those names are what
// `rest...` expands to at a call. So expansion is a lookup rather than a
// substitution.
//
// Measured for the arguments themselves: Itanium writes a pack as `J...E` and
// an empty one as `JE` - `_Z7nothingIJicEEiv`, `_Z5totalIiJEEiT_DpT0_` -
// while Microsoft lists the members inline and writes `$$V` for an empty
// pack, `??$total@H$$V@@YAHH@Z`.
//
// `total` is the recursive idiom, which needs the empty-pack call to reach
// the ordinary overload with no arguments at all.
extern "C" { int printf(const char *, ...); }

int total() { return 0; }
template <class T, class... Ts> int total(T first, Ts... rest) {
    return (int)first + total(rest...);
}

template <class... Ts> int howMany(Ts... a) {
    return (int)sizeof...(a) * 10 + (int)sizeof...(Ts);
}
template <class... Ts> int fromArguments() { return (int)sizeof...(Ts); }

struct P { int v; };
int addUp() { return 0; }
template <class T, class... Ts> int addUp(T a, Ts... rest) {
    return a.v + addUp(rest...);
}

template <class... Ts> struct Tuple {
    int howLong() { return (int)sizeof...(Ts); }
};

int main() {
    printf("%d %d\n", total(1, 2, 3), total());
    printf("%d %d\n", howMany(1, 2.0, 'c'), howMany());
    printf("%d %d %d\n", fromArguments<int, char>(), fromArguments<>(),
           fromArguments<int, char, double>());

    P a; a.v = 4;
    P b; b.v = 5;
    printf("%d\n", addUp(a, b));

    Tuple<int, char> t;
    Tuple<> empty;
    printf("%d %d\n", t.howLong(), empty.howLong());
    return 0;
}
