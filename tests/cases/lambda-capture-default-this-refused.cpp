// The other side of lambda-capture-default-this.cpp: an implicitly captured
// `this` carries the constness of the function that captured it, so a const
// member function's `[=]` cannot write the object any more than the function
// itself can. clang refuses this, naming the function; cxx1 refuses it through
// the ordinary const rule, the captured pointer being `const S *` and the
// member reached through it `const int`.
//
// Written as its own case because the accepting one cannot hold it, and
// because it is the rule that would go quietly missing if the implicit capture
// were ever built from `currentClass_` - which is unqualified - rather than
// from the enclosing `this`.
extern "C" int printf(const char *, ...);

struct S {
    int n;
    S() : n(4) {}
    int f() const { auto g = [=]() { n = 9; }; g(); return n; }
};

int main() {
    S s;
    printf("%d\n", s.f());
    return 0;
}
