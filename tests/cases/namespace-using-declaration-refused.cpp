// `using N::f;`, the using-*declaration*, refused by name.
//
// It is a different rule from the using-*directive* beside it, and a larger
// one: it brings a single name into this scope as a declaration of its own,
// which means it takes part in overload resolution alongside whatever is
// already here, and it can be written inside a class to change a base
// member's access. The directive - `using namespace N;` - is what this
// compiler has, and it opens the whole namespace for lookup without
// declaring anything.
namespace N {
    int f(void) { return 3; }
}

using N::f;

int main(void) {
    return f();
}
