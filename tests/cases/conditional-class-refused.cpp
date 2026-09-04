// **A class-typed `?:` works only where both arms are lvalues of one type**,
// which is what `b ? s : t` is - `tests/cases/user-conversion.cpp` and others
// rely on that and it is unchanged. Anything else needs the result built into
// storage of its own, because a `Conditional` yields a value the backends move
// as a scalar and a class has nowhere to be moved to.
//
// Converting the arms and leaving that alone was tried: the three Compiler++
// sources that write this compiled and then aborted at run time, which is
// worse than the refusal. So the refusal stays until the lowering exists, and
// it names the reason rather than saying "incompatible types" - which pointed
// at the arms when the problem is where the answer would live.
extern "C" int printf(const char *, ...);

struct Text { int n; Text(const char *s); Text(const Text &o); ~Text(); };
Text::Text(const char *s) { n = 0; while (s[n] != 0) n++; }
Text::Text(const Text &o) : n(o.n) {}
Text::~Text() {}

int main() {
    Text held("abc");
    // One arm is a class and the other a `const char *`, so the result would
    // be a temporary this expression has nowhere to put.
    Text r = true ? "_" : held;
    return r.n;
}
