// [stmt.dcl]/3: a jump may not enter the scope of an object past its
// initialisation. This compiled, and the end of f then ran ~Probe on a `p`
// whose constructor had never been called - the audit's finding, and a silent
// wrong answer rather than a refusal. clang under -std=c++11 -pedantic-errors:
// "cannot jump from this goto statement to its label ... jump bypasses
// variable initialization".
struct Probe { Probe(); ~Probe(); int v; };
Probe::Probe() { v = 1; }
Probe::~Probe() { v = 0; }

int f(int n) {
    if (n) goto done;
    Probe p;
    n = p.v;
done:
    return n;
}
int main() { return f(0); }
