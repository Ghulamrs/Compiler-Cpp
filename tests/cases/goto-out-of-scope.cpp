// A goto out of a block that holds a live object. [stmt.jump]/2: the object
// is destroyed on the way out, as it would be at the block's end. Until this
// the jump was accepted and the destructor simply did not run - the
// conservative refusal CLAUDE.md described sat on a line the goto branch had
// already returned from - so this case ran "built" and stopped, where clang
// runs "built destroyed". jump-out-destroys.cpp holds the neighbours.
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
    printf("out %d\n", n);
    return n;
}
int main() { f(1); f(0); return 0; }
