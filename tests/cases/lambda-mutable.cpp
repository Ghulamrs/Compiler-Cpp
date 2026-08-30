// `mutable` is the whole of the difference between a const call operator and a
// non-const one, and that is all it is: [expr.prim.lambda] makes a closure's
// `operator()` const unless the lambda says otherwise, so a by-value capture
// cannot be written through without it. One flag on the declaration and one
// token in the synthesised body.
//
// **What is written is the closure's own copy.** `k` out here is untouched -
// that is what capturing by value means, and `[&]` is how you write through to
// the original, which `alsoWrites` does.
//
// **Each call is in a statement of its own on purpose.** `tick()` mutates what
// the next call reads, and the order arguments are evaluated in is unspecified
// - a case that put three of them in one `printf` printed one thing on the two
// Itanium targets and another on x86_64-windows, and both were right. That is
// how lambda-capture-this was caught, and it is a trap worth not re-laying.
extern "C" { int printf(const char *, ...); }

int main(void) {
    int k = 5;
    auto tick = [k]() mutable { k = k + 1; return k; };
    int first = tick();
    int second = tick();
    int third = tick();

    int m = 1;
    auto alsoWrites = [&m]() mutable { m = m + 9; };
    alsoWrites();

    int n = 2;
    auto viaDefault = [=]() mutable { n = n * 3; return n; };
    int scaled = viaDefault();

    printf("%d %d %d %d %d %d %d\n", first, second, third, k, m, scaled, n);
    return 0;
}
