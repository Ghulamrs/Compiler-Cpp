// A static data member of a class template, defined out of line:
//
//     template <class Tag> const int Consts<Tag>::Origin = 7;
//
// [temp.static]/1 - the definition is written at namespace scope with the
// template header in front, and one is instantiated for each specialization
// that uses it. cxx1 refused it as "declared here and not defined", which was
// a diagnostic about the wrong thing: the definition is right there, and what
// could not see it was the skip that reads past a templated definition. It
// looked for a `{` body, and a data member's definition has an initialiser and
// ends at a `;` - so an `=` at depth zero is what says a definition was read.
//
// The second half is the replay. An out-of-line member is replayed when the
// specialization first uses it, and "uses" was asked of the *function* table -
// which a data member never enters, so its definition was recorded and never
// emitted, and the program failed at the link with the member undefined.
// A data member is replayed with its specialization instead.
extern "C" int printf(const char *, ...);

template <class Tag> struct Consts {
    static const int Origin;
    static const double Scale;
};
template <class Tag> const int Consts<Tag>::Origin = 7;
template <class Tag> const double Consts<Tag>::Scale = 2.5;

struct Metres {};
struct Feet {};

int main(void) {
    // Two specializations, each with its own object, and the address taken so
    // neither compiler folds the value and emits nothing.
    const int *a = &Consts<Metres>::Origin;
    const int *b = &Consts<Feet>::Origin;
    printf("%d %d %d\n", Consts<Metres>::Origin, Consts<Feet>::Origin,
           (int)(a != b || *a == *b));
    // Both specializations' Scale is read, and that is not incidental: cxx1
    // replays a data member's definition with its specialization, where clang
    // emits only what is odr-used, so a member left unused in one
    // specialization is a symbol cxx1 has and clang has not. Harmless - the
    // definitions are mergeable - and it would be reported as a naming
    // difference by a case that used only one of them.
    printf("%d %d\n", (int)(Consts<Metres>::Scale * 4),
                      (int)(Consts<Feet>::Scale * 4));
    return 0;
}
