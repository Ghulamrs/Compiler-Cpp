// A declaration and its definition that disagree about `noexcept`, refused.
//
// In C++11 the exception specification is **not part of the function's type** -
// measured, `void f() noexcept` and `void f()` mangle identically on both
// ABIs. So nothing else holds these two together: without this check they
// would silently be one function carrying whichever promise was read last, and
// which one that is depends on the order the file happens to be written in.
//
// clang refuses it in both directions, and so does this.
int size(void) noexcept;

int size(void) { return 4; }

int main(void) {
    return size();
}
