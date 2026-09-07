// `T *volatile` - the qualifier on the pointer rather than on what it points at.
//
// This one is an ABI difference and is refused on all three targets anyway.
// **Itanium is right about it by accident**: [dcl.fct]/5 deletes a parameter's
// top-level cv when the function type is formed, so `void f(int *volatile)` is
// `_Z1fPi` and cxx1's dropped qualifier gives the same answer. cl keeps it and
// writes `?f@@YAXREAH@Z`, where P is a plain pointer, Q a const one, R a
// volatile one and S both - the same four letters the array-parameter work of
// 2026-09-06 met from the other side.
//
// Refused everywhere rather than for Windows alone, because the accident does
// not hold once the pointer is an object of its own rather than a parameter:
// `int *volatile p;` at namespace scope carries the qualifier into its own name
// on that ABI too.

void f(int *volatile p) { *p = 1; }

int main() { int v = 0; f(&v); return 0; }
