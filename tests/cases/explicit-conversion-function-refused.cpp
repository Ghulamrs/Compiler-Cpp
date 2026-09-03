// `explicit operator bool()`, refused - and not because of `explicit`.
//
// A conversion function is the other place C++11 allows the keyword, and this
// compiler has no conversion functions at all yet, explicit or otherwise. The
// message says so rather than reporting that `explicit` applies to a
// constructor, which would send the reader to fix the wrong half.
struct Flag {
    int v;
    explicit operator bool(void) const { return v != 0; }
};

int main(void) {
    Flag f;
    f.v = 1;
    return f ? 1 : 0;
}

// **What this case is about changed under it, which is worth recording.** It
// used to say that a conversion function was not supported *at all*, so
// `explicit` had nothing to apply to. Conversion functions work now, in both
// directions and on all three targets, and what is refused is the keyword
// alone: an explicit one has to be refused everywhere except a `static_cast`
// and a condition, and accepting the word while ignoring that rule would be a
// claim the compiler cannot support - the defect docs/EXCLUSIONS.md exists for.
