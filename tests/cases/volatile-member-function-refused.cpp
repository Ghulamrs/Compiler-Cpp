// `f() volatile`, refused by name where it used to be `expected ';'`.
//
// The cv on `this` is half of a member function's identity: both ABIs write it
// into the name, both let it choose between overloads, and neither has anywhere
// to put a qualifier this compiler does not model. cxx1 reads a trailing
// `const` and has `constThis` for it; `volatile` had no rule at all, so the
// parser fell off the end of the declaration and blamed the semicolon.
//
// That is the third bucket of the 2026-09-06 sweep - a refusal whose message
// names no feature, which `tools/exclusions` cannot list and nobody can find.
// `const volatile` landed in the same place and is named by the same line.

struct S { int v; int m() volatile { return v; } };

int main() { S s; s.v = 1; return s.m() - 1; }
