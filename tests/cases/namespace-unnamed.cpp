// `namespace { ... }` - the unnamed namespace.
//
// **It is a named namespace that nothing outside can name**, so the machinery
// already here does all of it: a prefix, a directive, and internal linkage on
// what it holds. [namespace.unnamed] says there is one per translation unit,
// that every `namespace {` in the file reopens that same one, and that its
// names answer an unqualified lookup from the closing brace to the end of the
// file - which is why the directive it leaves behind is not undone.
//
// The part a single program cannot see is the part that matters when several
// are linked: nothing in here is `.globl`, so two files may each have a
// `helper` and the linker will not have to choose between them. `static` says
// the same thing about one name, which is what this compiler said instead.

extern "C" int printf(const char *, ...);

namespace {
    int helper(int a) { return a * 3; }
    int counter = 4;
    struct Box { int v; };
    Box make(int v) { Box b; b.v = v; return b; }
}

namespace {                                // the same namespace, reopened
    int second(void) { return helper(2) + counter; }
}

int fromOutside(void) {
    Box b = make(7);                       // found unqualified, after the brace
    return b.v + helper(1);
}

namespace N {
    namespace {                            // and one inside a named namespace
        int inner(void) { return 11; }
    }
    int useInner(void) { return inner(); }
}

int main(void) {
    printf("%d %d %d\n", helper(1), second(), fromOutside());
    printf("%d %d\n", counter, N::useInner());
    return 0;
}
