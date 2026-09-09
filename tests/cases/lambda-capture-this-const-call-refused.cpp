// The other half, one door over: through a `const S *` only a const member
// function is callable, so a lambda in a const member function cannot call a
// non-const member of its own class. cxx1 accepted this while the captured
// pointer was unqualified - V-09 of docs/audit-2026-09-06.html - and clang
// refuses it, naming the member.
//
// It is a separate case from the assignment because it is a separate rule:
// [over.match.funcs] ranks the implicit object parameter, and this is the
// candidate being non-viable rather than a const object being written.
extern "C" int printf(const char *, ...);

struct S {
    int n;
    S() : n(5) {}
    int bump() { return ++n; }
    int f() const {
        auto g = [this]() { return bump(); };
        return g();
    }
};

int main() {
    S s;
    printf("%d\n", s.f());
    return 0;
}
