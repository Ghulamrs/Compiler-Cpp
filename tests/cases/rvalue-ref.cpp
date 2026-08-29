// Rvalue references - rung 7.4a, the type and the binding rule.
//
// **The same machine as an lvalue reference.** Both are a slot holding an
// address and both are read by dereferencing it, so `isReference()` answers
// for either and almost nothing outside binding and mangling had to learn
// this exists. What differs is what may bind: `T &&` takes a value with
// nowhere to live, `T &` takes an object that has somewhere.
//
// **Which reference will take an argument is a question about the argument,
// not about a conversion.** An rvalue reference is not viable for an object
// with an address; where both are viable - for a temporary - it is the better
// match. That is the whole of how a move gets chosen over a copy, and it is
// why `which(n)` and `which(7)` below go to different functions.
//
// Measured for the names: Itanium writes `O` where an lvalue reference is
// `R`, and the Microsoft ABI writes `$$Q` where it writes `A`.
extern "C" { int printf(const char *, ...); }

struct S { int a; };

int which(const int &r) { (void)r; return 1; }
int which(int &&r) { (void)r; return 2; }

int only(int &&r) { return r + 1; }

S make() { S s; s.a = 9; return s; }
int take(S &&s) { return s.a; }
int viaPointer(int *&&p) { return *p; }

int main() {
    int n = 5;
    printf("%d %d\n", which(n), which(7));
    printf("%d\n", only(41));

    // A temporary that a call produced is exactly what an rvalue reference
    // is for, class type included.
    printf("%d\n", take(make()));

    int v = 3;
    printf("%d\n", viaPointer(&v));

    // The binding outlives the full expression it was made in.
    int &&kept = n + 1;
    kept = kept + 1;
    printf("%d %d\n", n, kept);
    return 0;
}
