// `const_cast<char *>(p)` where p is a `const int *`, refused.
//
// [expr.const.cast] wants the two types to be *similar*: the same shape,
// ending at the same type, differing only in their qualifiers. Taking the
// const off is allowed; changing what is pointed at is not, and doing both at
// once in one word would make const_cast a reinterpretation with a misleading
// name. Two casts, written out, do it.
int main(void) {
    const int n = 3;
    return *const_cast<char *>(&n);
}
