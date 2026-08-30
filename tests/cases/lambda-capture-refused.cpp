// A capture is a member of the closure, and it has to be initialised where the
// lambda is written - which needs a class temporary this compiler does not
// have, the same gap that refuses `return P(1);`. Refused by name until it
// does, rather than compiled into a closure whose member holds whatever was on
// the stack.
extern "C" { int printf(const char *, ...); }

int main(void) {
    int k = 1;
    auto f = [k]() { return k; };
    printf("%d\n", f());
    return 0;
}
