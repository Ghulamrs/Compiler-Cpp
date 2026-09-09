// [expr.prim.lambda]/8: `[=]` and `[&]` capture `this` implicitly, so a lambda
// written in a member function may name the class's own members. Both defaults
// capture the *pointer* - `[=, *this]`, which copies the object, is C++17 - so
// what the two differ about is the locals beside it and nothing else.
//
// cxx1 found a capture-default's captures by scanning the body for names that
// are locals of the enclosing function, and a member is not one: the body was
// told "'n' is not a local of the function around this lambda". So the shape
// most lambdas in a class are written in did not compile at all, while `[this]`
// written out worked. It was the fourteenth entry of tests/open/.
//
// The const half is the same rule the written `[this]` follows - the captured
// pointer has the type `this` has in the enclosing function, so a const member
// function's lambda reaches the object exactly as far as the function does, and
// lambda-capture-default-this-refused.cpp is the other side of that.
extern "C" int printf(const char *, ...);

struct B {
    int b;
    // Out of line, which is the suite's own habit: clang emits only the C2 form
    // of a constructor defined inside its class on x86_64-linux, and names.sh
    // would report that as a difference.
    B();
    int twice() const { return b * 2; }
};

B::B() : b(3) {}

struct S : B {
    int n;
    S();

    // A data member of this class, through `[=]`.
    int reads() { auto g = [=]() { return n; }; return g(); }

    // A member *function*, and from a const member function: the captured
    // pointer is `const S *`, so `twice` is callable and a non-const member
    // would not be.
    int callsMember() const { auto g = [=]() { return twice(); }; return g(); }

    // `[&]` captures `this` the same way - by copying the pointer, which is
    // the only way `this` is ever captured before C++17.
    int writes() { auto g = [&]() { n = 9; }; g(); return n; }

    // A local of the enclosing function hides the member of the same name, so
    // what is captured here is the local: 7, not 4. Lookup order decides it,
    // and the scan asks for a local first for exactly this reason.
    int shadowed() { int n = 7; auto g = [=]() { return n; }; return g(); }

    // A base's member is this class's member too, and is reached through the
    // same pointer.
    int inherited() { auto g = [=]() { return b + n; }; return g(); }

    // A local, a member and a base's member in one body: one pointer for the
    // two members, and `k` copied beside it.
    int alsoLocal(int k) { auto g = [=]() { return n + k + b; }; return g(); }
};

S::S() : n(4) {}

int main() {
    S s;
    printf("%d %d %d %d %d\n", s.reads(), s.callsMember(), s.shadowed(),
           s.inherited(), s.alsoLocal(10));
    // Sequenced by hand: [expr.call]/8 leaves the order of a call's arguments
    // unspecified, and `writes` changes what `inherited` reads.
    const int w = s.writes();
    const int after = s.inherited();
    printf("%d %d\n", w, after);
    return 0;
}
