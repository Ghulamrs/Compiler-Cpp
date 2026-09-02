// A switch jumps to each of its case labels, and [stmt.dcl]/3 names it beside
// goto: a label past a declaration with an initialiser, a constructor or a
// destructor is a jump into that object's scope. Here `case 1` lands after
// `Probe p`, and for n == 1 the end of the switch would destroy a `p` that was
// never built. clang: "cannot jump from switch statement to this case label".
struct Probe { Probe(); ~Probe(); int v; };
Probe::Probe() { v = 1; }
Probe::~Probe() { v = 0; }

int f(int n) {
    switch (n) {
    case 0:
        n = 7;
        Probe p;
        n += p.v;
    case 1:
        return n;
    }
    return 0;
}
int main() { return f(1); }
