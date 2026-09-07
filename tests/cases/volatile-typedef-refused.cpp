// A typedef of a `volatile` type, refused because it would launder the
// qualifier past the refusal a written `volatile T *` meets.
//
// `typedef volatile int VI;` then `VI *p` is a pointer to volatile int, and the
// sweep measured it as exactly that: `_Z1fPVi` and `?f@@YAHPECH@Z` from clang,
// the plain names from cxx1. The refusal on the written form is at the
// specifiers and looks at the token after them, so the typedef's own `*` is a
// star after a name that carries no volatile - nothing left to see.
//
// So the typedef is refused where it is written. That is stricter than the
// standard, which allows the name; it is not stricter than the compiler, which
// cannot spell what the name would stand for.

typedef volatile int VI;

int main() { VI v = 3; return v - 3; }
