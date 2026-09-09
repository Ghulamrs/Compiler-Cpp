// The half of [expr.prim.lambda]/18 that has to be refused: a const member
// function cannot write its object, and a lambda it writes cannot either.
// cxx1 accepted this and printed 9 - a const member function mutating itself,
// silently, which is V-09 of docs/audit-2026-09-06.html. clang refuses it.
//
// The refusal comes from the ordinary const rule rather than from a rule about
// lambdas: the captured pointer is `const S *`, so the member reached through
// it is `const int`, and assigning to one is already an error everywhere else.
extern "C" int printf(const char *, ...);

struct S {
    int n;
    S() : n(5) {}
    int f() const {
        auto g = [this]() { n = 9; };
        g();
        return n;
    }
};

int main() {
    S s;
    printf("%d\n", s.f());
    return 0;
}
