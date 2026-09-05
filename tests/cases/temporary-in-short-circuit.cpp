// **The right operand of `&&` or `||` may not run at all**, and a temporary it
// would have built must not be destroyed. [expr.log.and]/1: the second operand
// is evaluated only if the first is true, so an object it would have made was
// never made, and the destructor the full expression owes is owed for nothing.
//
// cxx1 destroyed it regardless: a `std::string` returned by a call in the
// skipped half was freed from a frame slot nothing had constructed, which is a
// `free` of whatever the stack held. Compiler++ met it on one line -
// `a->name == b->name && sig(a->params) == want` - and died there.
//
// The guard a `?:` arm already took is what fixes it, with one difference this
// operand forces: `?:` discards its arm's value and `&&` reads its operand's,
// so the value is kept and handed back after the guard is set.
//
// Counting *live* objects rather than constructor calls is CLAUDE.md's rule -
// elision may change how many copies happen and cannot change how many exist.
// `ran` is the other half: whether the skipped operand ran at all is a
// question no elision may answer differently.
extern "C" int printf(const char *, ...);

int live = 0;
int ran = 0;

// Defined out of line on purpose. Written inside the class these are inline,
// and clang emits none of them - it elides every copy here and folds the rest
// away, so the names suite would report a difference about emission rather
// than about mangling. CLAUDE.md records that trap twice already.
struct S {
    int v;
    S(int n);
    S(const S &o);
    ~S();
};

S::S(int n) : v(n) { live++; }
S::S(const S &o) : v(o.v) { live++; }
S::~S() { live--; }

S make(int n) { ran++; return S(n); }
int look(const S &s) { return s.v != 0; }

int main() {
    bool t = true;
    bool f = false;

    // Skipped: nothing built, nothing destroyed, and `make` never called.
    if (f && look(make(1))) printf("unreachable\n");
    printf("and skipped: live %d ran %d\n", live, ran);

    // Taken: built and destroyed at the end of the full expression.
    if (t && look(make(2))) printf("and taken\n");
    printf("and taken:   live %d ran %d\n", live, ran);

    // `||` skips its right operand when the left is true.
    if (t || look(make(3))) printf("or skipped\n");
    printf("or skipped:  live %d ran %d\n", live, ran);

    if (f || look(make(4))) printf("or taken\n");
    printf("or taken:    live %d ran %d\n", live, ran);

    // Two temporaries in one skipped operand, and a nested `&&` inside it.
    if (f && (look(make(5)) && look(make(6)))) printf("unreachable\n");
    printf("nested:      live %d ran %d\n", live, ran);

    // The operand's own value still decides the branch, which is what the
    // kept-and-handed-back slot is for: make(0) makes `look` answer 0.
    if (t && look(make(0))) printf("unreachable\n");
    printf("value kept:  live %d ran %d\n", live, ran);
    return 0;
}
