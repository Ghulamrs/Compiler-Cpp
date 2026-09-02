// The neighbours jump-out-destroys.cpp does not reach: a jump whose target
// is chosen by one construct while the objects it leaves belong to another.
// `break` inside a switch inside a loop binds to the switch, so it destroys
// the switch body's objects and not the loop body's; `continue` in the same
// place binds to the loop and destroys both. What is being checked is the
// stack of marks - one per loop and one per switch - rather than a single
// "innermost thing you can leave", which would have given the same answer for
// every case in the other file and the wrong one for these.
//
// The third is a by-value class parameter, which is alive at the goto and
// alive at the label alike: the jump stays inside it, so it is left where it
// is and destroyed once, at the end. A jump that destroyed everything alive
// would take the parameter with it and the caller would see the destructor
// run twice.
extern "C" int printf(const char *, ...);

struct Probe {
    Probe(const char *n);
    Probe(const Probe &o);
    ~Probe();
    const char *name;
};
Probe::Probe(const char *n) { name = n; printf("+%s ", n); }
Probe::Probe(const Probe &o) { name = o.name; printf("+copy%s ", name); }
Probe::~Probe() { printf("-%s ", name); }

void switchInLoop() {
    for (int i = 0; i < 2; i++) {
        Probe outer("o");
        switch (i) {
        case 0: {
            Probe s("s");
            break;
        }
        default:
            printf("d ");
        }
        printf("%d ", i);
    }
    printf("| ");
}

void continueThroughSwitch() {
    for (int i = 0; i < 3; i++) {
        Probe outer("o");
        switch (i) {
        case 1: {
            Probe s("s");
            continue;
        }
        default:
            printf("d%d ", i);
        }
        printf("end%d ", i);
    }
    printf("| ");
}

void byValue(Probe p, int n) {
    (void)p;
    {
        Probe a("a");
        if (n) goto out;
        printf("fell ");
    }
out:
    printf("| ");
}

int main() {
    switchInLoop(); printf("\n");
    continueThroughSwitch(); printf("\n");
    { Probe arg("arg"); byValue(arg, 1); } printf("\n");
    return 0;
}
