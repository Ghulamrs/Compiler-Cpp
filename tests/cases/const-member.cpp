// A member of a const-qualified class object, reached from inside a member
// function - the regression case for a bug the class-story audit found.
//
// The trap is WHEN the const copy of X was interned. A member function's
// parameter types are read while the class is still open, so `const X` there
// was interned before X had members; a free function's afterwards. findMember
// read the copy's own (empty) member list instead of forwarding to the
// unqualified type, so `o.n` worked in the free function and failed in the
// member function - the exact split this case pins.
//
// The copy constructor is the loudest victim: X(const X &) could not read the
// object it was copying.
extern "C" { int printf(const char *, ...); }

class X {
public:
    X();
    X(const X &o);
    int viaMember(const X &o) const;
    int n;
};

X::X() { n = 7; }
X::X(const X &o) { n = o.n * 3; }
int X::viaMember(const X &o) const { return o.n * 10; }

int viaFree(const X &o) { return o.n; }

int main(void) {
    X a;
    X b(a);
    const X c;
    printf("%d %d %d %d\n", viaFree(a), a.viaMember(a), b.n, c.n);
    return 0;
}
