// A goto out of a block that holds a live object. Legal C++ - clang runs
// ~Probe on the way out - and refused here by name, because this compiler
// emits a scope's destructors at its end and a jump that leaves early goes
// over them. Until this check the jump was accepted and the destructor simply
// did not run: the conservative test CLAUDE.md described sat on a line the
// goto branch had already returned from. The honest fix is a jump that
// destroys what it leaves, which needs jumps to know their scopes; until then
// a refusal, and not a missing call.
extern "C" int printf(const char *, ...);

struct Probe { Probe(); ~Probe(); };
Probe::Probe() { printf("built\n"); }
Probe::~Probe() { printf("destroyed\n"); }

int f(int n) {
    {
        Probe p;
        if (n) goto out;
        printf("fell through\n");
    }
out:
    return n;
}
int main() { return f(1); }
