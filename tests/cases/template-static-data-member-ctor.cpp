// A static data member of a class template whose type has a *constructor*.
// It needs that constructor run before main - [basic.start.init]/2 - and was
// refused by name until the init function existed. Two things about it are
// the template's own: it is defined once per translation unit that
// instantiates it, so the object and its guard are weak and the guard keeps a
// second copy from building it again; and its initialisation is *unordered*
// relative to the file's other objects, so nothing here depends on the order.
//
// Worth pinning because the refusal used to be a different and wrong one: the
// definition below was reported as "declared here and not defined", which sent
// the reader looking for a definition that is written on the line above.
extern "C" int printf(const char *, ...);
struct R { int v; R(); R(int x); };
R::R() : v(7) {}
R::R(int x) : v(x) {}

template <class Tag> struct Consts {
    static const R Identity;
    static R Named;
};
template <class Tag> const R Consts<Tag>::Identity = R();
template <class Tag> R Consts<Tag>::Named(21);

struct Tag1 {};
struct Tag2 {};

int main(void) {
    printf("%d %d %d\n", Consts<Tag1>::Identity.v, Consts<Tag2>::Identity.v,
           Consts<Tag1>::Named.v);
    // Reached through a reference: a statement that *begins* with a
    // template-id is read as a declaration here, which is its own gap.
    R &named = Consts<Tag1>::Named;
    named.v = 1;
    printf("%d %d\n", Consts<Tag1>::Named.v, Consts<Tag2>::Named.v);
    return Consts<Tag1>::Identity.v - 7;
}
