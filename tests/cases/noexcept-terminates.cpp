// [except.spec]/9: an exception that escapes a function whose specification
// does not allow it calls std::terminate. cxx1 recorded the promise and did not
// enforce it, so the exception unwound past the function and the handler below
// caught it - the program printed "caught" and returned 0 where clang aborts.
//
// Enforced here for the case where the escape is certain at compile time: a
// `throw` inside a `noexcept` function with no `try` in that function to catch
// anything. An exception arriving from a function this one *calls* still
// propagates, and docs/CONFORMANCE.md carries that half.
//
// The output stops at "before", which is the whole point: nothing after the
// throw runs, and the handler never reports. stdout is flushed first because a
// process that aborts does not flush it.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);

void escapes() noexcept { throw 1; }

// A `try` inside the function still catches its own throw - the promise is kept,
// so nothing terminates and this must print.
void keepsIt() noexcept { try { throw 2; } catch (int e) { printf("kept%d ", e); } }

int main() {
    keepsIt();
    printf("before\n");
    fflush(0);
    try { escapes(); } catch (int) { printf("caught - and should not have\n"); }
    printf("returned - and should not have\n");
    return 0;
}
