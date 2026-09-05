// **`<cassert>` is re-readable, and its `extern "C"` must balance on every
// pass.** NDEBUG is consulted at each include, so the header cannot guard its
// whole self - only the declarations. The `extern "C" {` that brackets those
// declarations therefore has to close inside the same guard, or a second
// include skips the open and emits an orphan `}`. A program that includes
// <cassert> directly and through another header is exactly that second
// include, and this pins that it compiles.
#include <cassert>
#include <cassert>
extern "C" int printf(const char *, ...);
int main() {
    assert(1 + 1 == 2);
    printf("ok\n");
    return 0;
}
