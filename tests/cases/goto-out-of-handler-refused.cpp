// The one way out of a handler that is refused rather than answered.
//
// [except.handle]/16 asks for `__cxa_end_catch` on the way out, and a `goto`
// is the exit whose target this compiler may not have read yet: a forward
// label is resolved afterwards, by resolveGotos, filling a block that was left
// empty where the jump was written - so at the point the decision is made
// there is nowhere to put the call. Refused by name rather than left to leak
// the exception object silently, which is what every exit did until
// handler-exit-end-catch.cpp was written.
//
// A `goto` to a label *inside* the handler leaves no handler and is not
// refused; `stays()` in that case is the one that proves it.
extern "C" int printf(const char *, ...);

int leaves() {
    int n = 0;
    try { throw 3; }
    catch (int e) { n += e; goto done; }
done:
    return n;
}

int main() {
    printf("%d\n", leaves());
    return 0;
}
