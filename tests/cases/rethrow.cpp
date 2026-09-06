// **A rethrow hands back the exception the handler is holding**, and the
// runtime is the one that knows which - so there is nothing to name and
// nothing to allocate. It is `__cxa_rethrow()` and no more.
//
// **It is not refused outside a handler**, though the first version of this
// refused it: [except.throw]/8 makes `throw;` with nothing being handled
// well-formed and terminating, and clang compiles it - so a compile-time
// check would have refused a conforming program. `__cxa_rethrow` calls
// std::terminate by itself, which is exactly the standard's answer, so there
// is nothing here to check.
extern "C" int printf(const char *, ...);

int inner() {
    try { throw 7; }
    catch (int e) { printf("inner %d ", e); throw; }
    return -1;
}

// Rethrown twice, through two frames.
int middle() {
    try { inner(); }
    catch (int e) { printf("middle %d ", e); throw; }
    return -1;
}

int main() {
    try { middle(); }
    catch (int e) { printf("outer %d\n", e); }
    return 0;
}
