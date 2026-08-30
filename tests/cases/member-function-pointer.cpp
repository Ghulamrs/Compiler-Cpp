// `int (S::*f)() = &S::get;` and `(s.*f)()`.
//
// **It wears the shape of a struct, and that is the whole trick.** The ABI
// keeps a pair on Itanium - a code address and a `this` adjustment, 16 bytes -
// and a single code pointer on Microsoft. Giving it a new type kind would have
// meant teaching three backends to copy, pass and return one, at each of the
// dozen places they ask `isStructOrUnion()`. Giving it the *shape* of a struct
// meant teaching them nothing: `kind_` is Struct and a flag is what tells
// `describe()` and the manglers what they are really looking at.
//
// **The object and the code pointer have to reach the call together**, and no
// expression here holds a pair - so `o.*p` answers with the code pointer and
// leaves the address in the parser for the `(` to pick up as the first
// argument. The window is one token wide, because `(o.*p)` is only ever
// written in order to be called.
//
// Both manglings measured: Itanium writes `M` and then the class and the
// function type, `M1SFivE`, the same `M` a data member pointer uses; Microsoft
// writes `P8` and then the class and an ordinary member signature,
// `P8S@@EAAHXZ`, where a data member pointer writes `PEQ`.
extern "C" { int printf(const char *, ...); }

struct S {
    int x;
    int get();
    int add(int a);
    int twice();
};

int S::get() { return x; }
int S::add(int a) { return x + a; }
int S::twice() { return x * 2; }

int callIt(S s, int (S::*f)()) { return (s.*f)(); }

int main(void) {
    S s;
    S *ps;
    s.x = 5;
    ps = &s;

    int (S::*f)() = &S::get;
    int (S::*g)(int) = &S::add;

    int direct = (s.*f)();
    int viaPointer = (ps->*f)();
    int withArgument = (s.*g)(4);
    int passed = callIt(s, &S::get);
    f = &S::twice;                     // the same variable, another member
    int reassigned = (s.*f)();

    // **`sizeof` is deliberately not printed.** It is 16 on the Itanium
    // targets and 8 on x86_64-windows, both correct - a pair against a single
    // code pointer - so a case that prints it can only pass on two boxes out
    // of three. This one printed it and the Windows box said so, which is the
    // second time in this session a target-dependent value was baked into an
    // expected output and only the third machine could catch it.
    printf("%d %d %d %d %d\n", direct, viaPointer, withArgument, passed,
           reassigned);
    return 0;
}
