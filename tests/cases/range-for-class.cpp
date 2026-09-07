// A range-based `for` over a class - [stmt.ranged]'s other half.
//
// **The standard writes this loop as another loop**, and the rewrite is what
// the parser does:
//
//     auto &&__range = expr;
//     for (auto __b = __range.begin(), __e = __range.end(); __b != __e; ++__b)
//         { T x = *__b; body }
//
// **The reference is a pointer here**, because cxx1 cannot declare
// `auto &&__r`. `__r` is an `R *` holding `&expr` and every use is `*__r` -
// the same object, and evaluated **once**, which `counted()` below is what
// proves: a second evaluation would print 2 and would also mean a container
// with a side effect in its expression was walked twice.
//
// The class is written here rather than taken from `include/` so the case says
// what it depends on: a `begin()` and an `end()` returning a pointer. That the
// shipped containers satisfy it is `range-for-vector.cpp`'s business.
//
// [stmt.ranged]/1 looks `begin` and `end` up as **members** first and only
// falls back to free functions found by argument-dependent lookup; the member
// form is what is built, and the fallback is refused by name.

extern "C" int printf(const char *, ...);

struct Bag {
    int d[3];
    int *begin() { return d; }
    int *end() { return d + 3; }
};

struct Empty {
    int d[1];
    int *begin() { return d; }
    int *end() { return d; }
};

int evaluations = 0;
Bag shared;

Bag &counted() { evaluations++; return shared; }

int main(void) {
    Bag b;
    b.d[0] = 4; b.d[1] = 5; b.d[2] = 6;
    for (int n : b) printf("%d ", n);
    printf("\n");

    // `auto` deduces from the element, which is what `*__b` has.
    int total = 0;
    for (auto n : b) total += n;
    printf("total %d\n", total);

    // An empty range runs the body no times, and `begin() == end()` is the
    // whole of why - the loop never dereferences either.
    int ran = 0;
    Empty none;
    for (int n : none) ran += n + 1;
    printf("empty ran %d\n", ran);

    // The range expression is evaluated once, however many times the body runs.
    shared.d[0] = 7; shared.d[1] = 8; shared.d[2] = 9;
    for (int n : counted()) printf("%d ", n);
    printf("| evaluations %d\n", evaluations);

    // Nested, to show the two loops keep their own `__b` and `__e`.
    Bag two;
    two.d[0] = 1; two.d[1] = 2; two.d[2] = 3;
    for (int x : two)
        for (int y : two)
            if (x == y) printf("%d%d ", x, y);
    printf("\n");

    // An array in the same function, because the two paths share every line
    // after the two ends are computed and a change to one must not move the
    // other.
    int plain[3] = { 10, 20, 30 };
    for (int n : plain) printf("%d ", n);
    printf("\n");
    return 0;
}
