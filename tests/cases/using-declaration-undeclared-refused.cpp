// A using-declaration that names nothing, refused where it is written.
//
// The alias is recorded against a name, so a name that is not there has to be
// caught here rather than at the use: this position is the one that says which
// name was meant, and the use would only say that some other name is unknown.
namespace N {
    int f(void) { return 1; }
}

using N::nope;

int main(void) {
    return N::f();
}
