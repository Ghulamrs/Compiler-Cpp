// `namespace N::M { }`, refused by name.
//
// This is a C++17 spelling - [namespace.def] gained the nested-namespace-
// definition there - and this compiler targets C++11, where the two braces
// have to be written out. Refused rather than quietly accepted, because
// accepting a later standard's syntax makes the compiler's own answer to
// "does this program conform" useless: a file that builds here would stop
// building on the C++11 compiler it was written for.
namespace N::M {
    int f(void) { return 3; }
}

int main(void) {
    return N::M::f();
}
