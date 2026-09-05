// `using N::f;` - the using-declaration, which names one thing.
//
// **It is an alias and not a table.** The using-*directive* beside it opens a
// whole namespace for lookup and declares nothing; this declares a name in the
// scope it is written in, and that name stands for the one it points at. So a
// namespace stays what it already was here - a prefix and a search - and the
// declaration is one qualified name recorded against another.
//
// Two shapes matter beyond the plain one. `using cc::Type;` inside another
// namespace is how a two-layer program spells a shared type unqualified, and
// `using ::width_t;` inside a namespace is how a C name is given one, which is
// what `<cstddef>`'s `namespace std { using ::size_t; }` is.

extern "C" int printf(const char *, ...);

typedef unsigned int width_t;              // a C name, at global scope

namespace cc {
    struct Type { int width; };
    int widthOf(Type t) { return t.width; }
    int shared = 7;
}

namespace lib {
    using ::width_t;                       // the <cstddef> shape
}

namespace cxx {
    using cc::Type;                        // the SAME type, not a copy
    using cc::widthOf;
    using cc::shared;

    int twice(Type t) { return widthOf(t) * 2; }   // both found unqualified
    int plusShared(Type t) { return t.width + shared; }
}

using cc::Type;                            // and one at file scope

int viaFileScope(void) {
    Type t;                                // cc::Type, through the line above
    t.width = 5;
    return cxx::twice(t);
}

int viaQualified(void) {
    cxx::Type t;                           // written qualified, still cc::Type
    t.width = 6;
    return cxx::widthOf(t);                // the alias, called qualified
}

lib::width_t counted = 3;                  // a typedef reached through its alias

int sameType(cc::Type a, cxx::Type b) {    // one type spelled two ways
    return a.width + b.width;
}

int main(void) {
    Type one;
    one.width = 4;
    printf("%d %d %d\n", viaFileScope(), viaQualified(), cxx::plusShared(one));
    printf("%d %d\n", (int)counted, sameType(one, one));
    return 0;
}
