// An `explicit` constructor is not a conversion.
//
// That is the whole of what the keyword does, and it is worth a case because
// the rule is one line in the search that finds the constructor - so it would
// be lost silently rather than loudly. clang refuses the same program.
struct E {
    int v;
    explicit E(int x) : v(x) {}
};

static int take(const E &e) { return e.v; }

int main(void) {
    return take(7);
}
