// `[this]` - the closure holds a pointer to the enclosing object, and an
// unqualified name in the body is reached through it.
//
// **Three lookups had to learn it, and each is a different piece of the
// parser.** Inside the call operator `currentClass_` is the *closure*, so:
// the word `this` means the closure's own and not the one the reader wrote; an
// unqualified data member is searched for in the closure and not found; and an
// unqualified member *call* is searched for the same way, in the branch that
// runs before the free-function one - which is why `twice()` reported that no
// such function was declared rather than anything about a member.
//
// **A lambda has the access of the function it was written in**,
// [expr.prim.lambda]/7, so a private member is reachable from one. Both access
// checks ask a single helper for that now: they had already drifted apart
// once, and a private *field* was readable from a lambda while a private
// *method* was not.
extern "C" { int printf(const char *, ...); }

class Counter {
    int n;
    int hidden() const;
public:
    Counter(int a);
    int byName();
    int byThis();
    int byCall();
    int byPrivateCall();
    int written();
    int withLocal();
};

Counter::Counter(int a) { n = a; }
int Counter::hidden() const { return n * 3; }
int Counter::byName() { auto f = [this]() { return n; }; return f(); }
int Counter::byThis() { auto f = [this]() { return this->n; }; return f(); }
int Counter::byCall() { auto f = [this]() { return hidden(); }; return f(); }
int Counter::byPrivateCall() { auto f = [this]() { return hidden() + 1; }; return f(); }
int Counter::written() { auto f = [this]() { n = n + 1; }; f(); return n; }
int Counter::withLocal() {
    int k = 2;
    auto f = [this, k]() { return n * k; };     // captured together
    return f();
}

int main(void) {
    Counter c(5);
    Counter d(5);
    Counter e(5);
    // **`written()` gets a Counter to itself, and that is not fastidiousness.**
    // It mutates what the others read, and the order arguments are evaluated in
    // is unspecified - so with all of them on one object this printed
    // `5 5 15 16 6 10` on the two Itanium targets and `6 6 18 19 6 10` on
    // x86_64-windows, which evaluates the other way round. Both are right. The
    // case was wrong, and only the third box could say so.
    printf("%d %d %d %d ",
           c.byName(), c.byThis(), c.byCall(), c.byPrivateCall());
    printf("%d %d\n", e.written(), d.withLocal());
    return 0;
}
