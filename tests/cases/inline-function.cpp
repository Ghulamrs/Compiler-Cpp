// **`inline` on a function** - [dcl.inline]/6: its definition may appear in
// several translation units, so it is emitted as a weak/COMDAT definition the
// linker folds, the same treatment a template specialization already gets. On a
// variable `inline` is a C++17 inline variable, refused by name.
//
// The mangled name is unchanged - `inline` is about linkage, not the type - so
// names.sh checks it against clang, which spells the same symbol linkonce_odr.
// The weak emission itself is what a two-object link exercises, which run.sh
// cannot; here the case only shows the function is reached and called.
extern "C" int printf(const char *, ...);

inline int addup(int a, int b) { return a + b; }
inline double half(double x) { return x / 2.0; }
static inline int internalOnly(int x) { return x + 1; }   // internal, not weak

int main() {
    printf("%d %.1f %d\n", addup(3, 4), half(9.0), internalOnly(41));
    return 0;
}
