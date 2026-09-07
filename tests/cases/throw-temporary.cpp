// A temporary in a thrown expression - `throw Msg("hello")` - which is how
// every real program raises an exception and how `<stdexcept>` will be used.
//
// **Three faults sat behind one refusal here, and they interlocked.**
//
//   1. The exception object was initialised by an `Assign` of a class, which
//      is a *block copy*: neither the copy constructor nor `operator=` ran.
//      Measured by making both print. For a class owning a buffer the
//      exception object was handed a pointer into the temporary.
//   2. The operand's temporaries were never destroyed. `throwStatement` built
//      the whole throw as one expression, so there was nowhere to put the
//      destruction, and the temporary was left in `pendingTemps_` for whatever
//      was parsed next - a `throw E(1)` inside a `try` leaked its E into the
//      *handler's* block.
//   3. `__cxa_throw`'s third argument was a null written when only fundamental
//      types could be thrown, so the runtime destroyed no exception object -
//      a buffer leaked per throw, invisibly.
//
// Fixing 2 alone turns 1 from a leak into a **use-after-free**: the handler
// read `[]` where clang read `[hello]`. All three are fixed together, and this
// case is written so that it cannot pass with any of them undone.
//
// **Written to be elision-invariant.** [class.copy]/31 lets an implementation
// build the exception object directly from the operand; clang does and cxx1
// does not, so cxx1 constructs two objects here and clang one. Counting
// constructions would therefore be counting a permitted choice. What both must
// agree on is that **nothing is outstanding at the end** and that the handler
// reads what was thrown - so the case prints a balance, never a total.

extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);
extern "C" char *strcpy(char *, const char *);

int outstanding = 0;

struct Msg {
    char *p;
    Msg(const char *s);
    Msg(const Msg &o);
    ~Msg();
};

Msg::Msg(const char *s) { p = (char *)malloc(64); strcpy(p, s); outstanding++; }
Msg::Msg(const Msg &o) { p = (char *)malloc(64); strcpy(p, o.p); outstanding++; }
Msg::~Msg() { free(p); p = 0; outstanding--; }

void raise() { throw Msg("hello"); }

int main() {
    // Thrown in another function, caught here.
    try { raise(); } catch (const Msg &m) { printf("caught [%s] ", m.p); }
    printf("| outstanding %d\n", outstanding);

    // Thrown and caught in the same function, which is where the refusal was.
    try { throw Msg("inline"); } catch (const Msg &m) { printf("caught [%s] ", m.p); }
    printf("| outstanding %d\n", outstanding);

    // A destructible local beside the throw: it goes before the handler, and
    // the exception object still outlives it.
    try { Msg keep("keep"); throw Msg("second"); }
    catch (const Msg &m) { printf("caught [%s] ", m.p); }
    printf("| outstanding %d\n", outstanding);
    return 0;
}
