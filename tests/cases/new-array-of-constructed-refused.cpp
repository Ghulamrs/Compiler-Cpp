// `new Q[3]()` where Q has a constructor would have to run it on each
// element, and the new-expression has no per-element shape yet - the same
// refusal `new Q[3]` gets, reached now through the value-initialising form
// that new-array-value-init.cpp accepts for a class with nothing to run.
struct Q { int q; Q() : q(3) {} };
int main() {
    Q *q = new Q[3]();
    return q[0].q;
}
