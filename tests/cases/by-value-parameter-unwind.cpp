// A constructor taking a class **by value**, where that class has a
// destructor - the shape that had a Microsoft unwind record naming a FuncInfo
// nothing wrote.
//
// The Microsoft ABI makes the *callee* destroy such a parameter, so the
// function owns something to unwind and cxx1 gives it a landing pad. The MASM
// spelling then decided "this function has exception tables" from the Itanium
// LSDA name it was handed - which says a pad exists, not that a Microsoft
// table will be written - and emitted `DD imagerel $cppxdata$??0G@@QEAA@US@@@Z`
// for a label that never appeared. ml64 stopped at
// `A2006: undefined symbol`, and no case in the tree had the shape, so nothing
// caught it: `by-value.cpp` passes classes by value but none of them has a
// destructor, which is what makes the callee owe one.
//
// Both spellings take the answer from one place now - the code generator, which
// knows whether it is about to write the tables.
extern "C" int printf(const char *, ...);

struct S {
    int v;
    S(int n);
    S(const S &o);
    ~S();
};

S::S(int n) : v(n) {}
S::S(const S &o) : v(o.v) {}
S::~S() {}

// The by-value parameter is this function's to destroy on that ABI.
struct G {
    S v;
    G(S x);
    int get() const;
};

G::G(S x) : v(x) {}
int G::get() const { return v.v; }

// And a plain function with one, which is the same question without a class
// around it.
int twice(S s) { return s.v * 2; }

int main() {
    S s(21);
    G g(s);
    printf("%d %d\n", g.get(), twice(s));
    return 0;
}
