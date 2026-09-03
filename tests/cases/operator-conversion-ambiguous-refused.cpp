// Two conversion functions, and a built-in operator offered both.
//
// **A condition and an arithmetic operator do not ask the same question.**
// [conv]/3 says a condition wants `bool`, so a class with `operator bool` and
// `operator int` converts unambiguously there - one of the two *is* bool. But
// [over.match.oper]/9 offers the built-in operators every conversion the class
// has, and `+` can be had two ways, so this is ambiguous and clang refuses it.
//
// The case exists because answering the two alike is a silent wrong answer, not
// a diagnostic: this program printed a value for a whole afternoon.
struct S {
    int v;
    operator int() const { return v; }
    operator bool() const { return v != 0; }
};

int main(void) {
    S a;
    a.v = 1;
    return a + 1;
}
