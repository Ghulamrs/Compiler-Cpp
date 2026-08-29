// **A member declared in a derived class hides one of the same name in a
// base.** [class.member.lookup]: the name means the most derived declaration
// of it, and the base's is still there to be reached through the base.
//
// This compiler copies a base's members into the derived class's list at the
// offsets they already have - that flattening *is* the layout - so the list
// runs from most-base to most-derived and a lookup has to read it backwards.
// Forwards, every use of a shadowed name found the base's member: wrong
// values, right types, and no diagnostic at all.
extern "C" int printf(const char *, ...);

struct Base { int v; };
struct Middle : Base { int v; };
struct Leaf : Middle { int v; };

int main() {
    Leaf l;
    l.v = 3;
    Middle *m = &l;
    m->v = 2;
    Base *b = &l;
    b->v = 1;
    printf("%d %d %d\n", l.v, m->v, b->v);
    printf("%d %d %d\n", (int)sizeof(Base), (int)sizeof(Middle),
           (int)sizeof(Leaf));
    return 0;
}
