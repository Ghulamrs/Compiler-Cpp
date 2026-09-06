// A class with a constructor at file scope. This path used to lay the object
// out as bytes and never run the constructor - `s.i` read 0 where the
// constructor had written 7 - and was then refused by name until dynamic
// initialisation landed. [basic.start.init]/2: the constructor runs before
// main, in the file's init function, for every spelling of the initialiser.
extern "C" int printf(const char *, ...);
// Defined out of line: written inside the class they are inline, and clang
// on x86_64-linux then emits only the C2 form where cxx1 emits both.
struct S { int i; S(); S(int v); };
S::S() { i = 7; }
S::S(int v) { i = v; }
S s;
S t = S(3);
S u(4);
S v = 5;
S w{};
int main() {
    printf("%d %d %d %d %d\n", s.i, t.i, u.i, v.i, w.i);
    return s.i - 7;
}
