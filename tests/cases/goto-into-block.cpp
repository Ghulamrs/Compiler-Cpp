// A jump backwards into a block that has closed. The object was built and
// destroyed the first time through; the jump lands after its declaration, so
// the block's end would destroy it a second time. The same [stmt.dcl]/3 rule
// as the forward jump, found by the same comparison: `p` is in scope at the
// label and not at the goto. clang refuses it.
struct Probe { Probe(); ~Probe(); int v; };
Probe::Probe() { v = 1; }
Probe::~Probe() { v = 0; }

int f(int n) {
    {
        Probe p;
    again:
        n += p.v;
        if (n > 3) return n;
    }
    if (n < 10) goto again;
    return n;
}
int main() { return f(0); }
