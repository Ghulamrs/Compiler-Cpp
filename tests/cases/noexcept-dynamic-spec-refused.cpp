// `throw(int)` - the dynamic exception specification, refused by name.
//
// This is a different feature from `noexcept`, not a spelling of it: it says
// *which* types may escape, which needs a run-time check of the thrown type
// against a list, where `noexcept` is a promise the compiler only has to
// record. Reading it as `throw()` would be worse than refusing it - that is a
// promise the program did not make, and it is the promise this one is most
// likely to break.
//
// `throw()` with nothing in it *is* `noexcept` and works; see noexcept.cpp.
int narrow(void) throw(int) {
    return 3;
}

int main(void) {
    return narrow();
}
