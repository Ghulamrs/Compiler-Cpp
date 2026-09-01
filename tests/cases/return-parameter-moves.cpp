// **`return t;` on a by-value parameter moves.** [class.copy]/32: where the
// elision criteria hold, or would hold save that the operand is a parameter,
// the constructor is selected as if the operand were an rvalue - so a class
// with a move constructor gives its parameter up rather than copying it, and
// a move-only class can be returned by value at all, which C++11 promises.
//
// Counting moves is what elision makes unpinnable - the temporary-to-object
// steps may or may not run - so what is pinned is what no permitted elision
// can change: the one copy is the call's own argument copy, the value arrives
// intact, and no copy constructor runs where the move must be chosen.
extern "C" int printf(const char *, ...);

int copies = 0;

struct M {
    int v;
    M(int n) { v = n; }
    M(const M &o) { v = o.v; copies = copies + 1; }
    M(M &&o) { v = o.v; o.v = 0; }
};
M pass(M m) { return m; }

struct Only {
    int v;
    Only(int n) { v = n; }
    Only(Only &&o) { v = o.v; o.v = 0; }
};
Only give(Only o) { return o; }

int main() {
    M a(7);
    M r = pass(a);
    printf("%d %d ", r.v, copies);
    Only x = give(Only(9));
    printf("%d\n", x.v);
    return 0;
}
