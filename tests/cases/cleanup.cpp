// Destructors run as an exception passes them - rung 6.4, and what makes RAII
// mean anything.
//
// **The objects are the ones a `return` already unwinds.** `alive_` holds
// them and nothing new had to track them; the difference is only where the
// code runs from - a landing pad rather than the return path, ending in
// _Unwind_Resume rather than in a return.
//
// **One region per stretch, and the stretches do not overlap.** `a` and `b`
// below give two ranges: after `a` and after `b`, each with a pad that
// destroys exactly what exists by then. An exception thrown before `b` is
// built must not destroy it, and a call-site table holds sorted disjoint
// ranges - so they are split rather than nested. The statement that *does*
// the constructing is outside every region on purpose.
//
// `middle` has the locals and `main` has the try, because a function may have
// one or the other for now: each is a range in the same table and one would
// have to split the other.
extern "C" { int printf(const char *, ...); }

struct Noisy {
    int id;
    Noisy(int n) { id = n; printf("build %d\n", id); }
    ~Noisy() { printf("destroy %d\n", id); }
};

void risky(int n) { if (n > 0) throw n; }

void middle(int n) {
    Noisy a(1);
    risky(n);
    Noisy b(2);
    risky(n + 10);
    printf("nothing thrown\n");
}

int main() {
    try { middle(1); } catch (int e) { printf("caught %d\n", e); }
    printf("---\n");
    try { middle(0); } catch (int e) { printf("caught %d\n", e); }
    printf("---\n");
    middle(-20);
    return 0;
}
