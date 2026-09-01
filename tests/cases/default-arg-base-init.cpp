// A base named in a mem-initialiser with fewer arguments than its constructor
// declares relies on the defaults - and this call was built by hand, walking
// one argument per declared parameter without ever applying them. `: Base(2)`
// against `Base(int, int = 6)` read past the end of the argument vector and
// the shipped compiler died on three lines of ordinary C++11. ASan names the
// read; the mend is the same `applyDefaults` every other call already goes
// through.
//
// `Ordinary` is that crash. `Empty` spells `: Base()`, which was refused for
// its arity before the defaults were counted. `Silent` names no base at all,
// where [class.ctor]/5 makes a constructor whose every parameter has a
// default a default constructor - default-building a base is overload
// resolution with no arguments, not a search for an empty parameter list.
// The template in the second default is there because a default argument is
// re-parsed at every call that uses it, which is the audit's C-01: this case
// watches both doors at once.
extern "C" int printf(const char *, ...);
template <class T> T id(T v) { return v; }
struct Base {
    int total;
    Base(int a = 1, int b = id(6)) : total(a + b) {}
};
struct Ordinary : Base { Ordinary() : Base(2) {} };
struct Empty    : Base { Empty()    : Base()  {} };
struct Silent   : Base { Silent()   {} };
int main() {
    Ordinary o; Empty e; Silent s;
    printf("%d %d %d\n", o.total, e.total, s.total);
    return 0;
}
