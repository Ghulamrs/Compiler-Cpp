// A lambda inside a lambda, whose `[=]` wants the outer one's capture. By the
// time the inner lambda is read, that name is a *member* of the outer closure
// and not a local, so the scan that finds what `[=]` takes has nothing to copy.
// Refused by name rather than left to fail further in with "'k' is a member and
// there is no object here to read it from", which is true and says nothing
// about the lambda that was written. Naming it in the inner list works.
int main(void) {
    int k = 2;
    auto outer = [=]() {
        auto inner = [=]() { return k; };
        return inner() * 10;
    };
    return outer();
}
