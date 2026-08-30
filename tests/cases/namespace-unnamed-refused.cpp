// An unnamed namespace, refused by name.
//
// `namespace { ... }` is not a namespace with no name so much as a namespace
// with a name nobody can write, plus a using-directive for it - and the point
// of it is the *linkage*: everything inside gets internal linkage, which is
// what makes it the C++ answer to `static` at file scope. Both halves are
// real work here. The generated name has to be stable across the whole
// translation unit and appear in every mangled name inside it, and the
// linkage has to reach the emitters. `static` already says the second half
// and is spelled in one word, so nothing is unreachable meanwhile.
namespace {
    int hidden(void) { return 3; }
}

int main(void) {
    return hidden();
}
