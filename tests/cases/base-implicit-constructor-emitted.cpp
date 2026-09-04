// **A base's implicit default constructor, called by every derived
// constructor, was never defined.** It compiles and assembles and then does not
// link: `Undefined symbols: cc::Decl::Decl()`, which is what stopped
// Compiler++'s sixteen objects from becoming a program.
//
// `Decl` is `struct Decl : Node { virtual ~Decl() {} };` - no constructor of
// its own, so the implicit one is what runs, and it is *not* trivial: something
// has to store the vptr. A user-written derived constructor that names no base
// calls it, and that path took the signature **by value** where the branch
// beside it goes through `resolveOverload`, which marks the table's entry used.
// The copy was marked and the table's own entry was not, so
// `defineImplicitFunctions` never gave it a body.
//
// It links until something derives from a class that has a vptr and no
// constructor of its own, which is why a suite of single-file cases never saw
// it: every case that could have was written with a constructor.
extern "C" int printf(const char *, ...);

struct Node { virtual ~Node() {} };
// No constructor written: the implicit one stores the vptr and must be emitted.
struct Decl : Node { virtual ~Decl() {} };
struct VarDecl : Decl { int n; VarDecl(int v); };
VarDecl::VarDecl(int v) : n(v) {}
// A second derived class, so the base's constructor is called from two places.
struct FnDecl : Decl { int m; FnDecl(int v); };
FnDecl::FnDecl(int v) : m(v * 2) {}

int main() {
    VarDecl a(7);
    FnDecl b(7);
    // Built directly as well as as subobjects: without this, clang has no use
    // for their complete-object constructors and emits none, which reads as a
    // name difference and is a difference in what the program asks for.
    Node bare;
    Decl alone;
    (void)bare; (void)alone;
    Decl *p = &a;
    Node *q = &b;
    (void)p; (void)q;
    printf("%d %d\n", a.n, b.m);
    return 0;
}
