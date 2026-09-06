// The neighbour that is still refused, and by name. A static data member of a
// class template whose type has a *constructor* needs that constructor run
// before main - [basic.start.init]/2's dynamic initialisation - which this
// compiler does not do for any namespace-scope object, static local or static
// member. It is refused in those three places already and this is the fourth.
//
// Worth pinning because the refusal used to be a different and wrong one: the
// definition below was reported as "declared here and not defined", which sent
// the reader looking for a definition that is written on the line above. It
// now says which feature is missing.
struct R { int v; R() : v(7) {} };

template <class Tag> struct Consts {
    static const R Identity;
};
template <class Tag> const R Consts<Tag>::Identity = R();

struct Tag1 {};

int main(void) { return Consts<Tag1>::Identity.v; }
