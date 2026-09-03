// **[dcl.init]/7: a const object has to be initialised where it is declared**,
// and this compiler used to leave one holding whatever the stack or the image
// had - `const S s;` on a plain struct compiled, and reading it gave a
// different answer per call site.
//
// The line is CWG 253's rather than the paragraph's letter, which is what clang
// applies and what this was measured against: a class with a constructor of its
// own is fine, and so is one whose every base and member is fine or carries an
// initialiser. Thirteen shapes were compared; const-uninitialised-ok.cpp runs
// the ones both compilers accept and this is one both refuse.
struct S { int a; int b; };
int main() { const S s; return s.a; }
