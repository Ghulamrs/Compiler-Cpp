// [expr.prim.lambda]/18: a captured `this` has the type it has in the enclosing
// function. In a const member function that is `const S *`, so a lambda written
// there reaches the object exactly as the function itself does - and no further.
//
// cxx1 captured the unqualified class, so a const member function could mutate
// its own object through a lambda with no diagnostic - V-09 of
// docs/audit-2026-09-06.html, the one finding in that register with no root
// beside it. The two halves are the capture's type and the const the member
// access has to inherit from it; either alone leaves the hole open.
//
// What this case pins is the four shapes that must keep working, the two that
// must be refused being their own cases: reading a member and calling a const
// member from a const function's lambda, writing a `mutable` member from one,
// and writing an ordinary member from a lambda in a non-const function.
extern "C" int printf(const char *, ...);

struct S {
    int n;
    mutable int hits;
    // Out of line, which is the suite's own habit rather than the feature:
    // clang emits only the C2 form of a constructor defined inside its class
    // on x86_64-linux, and names.sh would report that as a difference.
    S();

    int twice() const { return n * 2; }

    // A const member function's lambda: reads reach the object, and a const
    // member function of it can be called through the captured pointer.
    int readsThrough() const {
        auto g = [this]() { return n + twice(); };
        return g();
    }

    // `mutable` is the exception the ordinary member paths already make, and
    // the captured pointer has to make it too.
    int countsThrough() const {
        auto g = [this]() { hits = hits + 1; return n; };
        int v = g();
        return v + hits;
    }

    // The same lambda in a non-const member function writes as it always did.
    int writesThrough() {
        auto g = [this]() { n = 9; };
        g();
        return n;
    }
};

S::S() : n(5), hits(0) {}

int main() {
    S s;
    printf("%d ", s.readsThrough());
    printf("%d ", s.countsThrough());
    printf("%d\n", s.writesThrough());
    return 0;
}
