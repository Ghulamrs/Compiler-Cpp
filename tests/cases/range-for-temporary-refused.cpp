// A range-based `for` over a temporary, refused.
//
// [stmt.ranged] binds the range to `auto &&__range`, and that reference
// **extends the temporary's lifetime** to the end of the loop. cxx1 holds the
// range as an `R *` pointing at it, which is the same object for a named range
// and a dangling pointer for this one - so it is refused rather than walked.
// clang compiles this and it is correct there; the refusal is cxx1's own gap
// and says so.
//
// The remedy is in the message and costs one line: name it, then loop over the
// name.

struct Bag {
    int d[2];
    int *begin() { return d; }
    int *end() { return d + 2; }
};

Bag make(void) { Bag b; b.d[0] = 1; b.d[1] = 2; return b; }

int main(void) {
    int total = 0;
    for (int n : make()) total += n;
    return total;
}
