// nullptr.
//
// **At the machine it is a pointer-sized zero and nothing else**, so no
// backend heard about this feature: `Num(0)` with a type on it is the whole of
// the code generation. Everything nullptr buys is in the front end, where a
// null pointer constant stops being spelled the same as the number 0 - which
// is what makes `f(int)` lose to `f(char *)`, and `int n = nullptr;` a
// diagnostic rather than a zero.
//
// The type is `Kind::NullPtr`, and it sits between Void and Bool in that enum
// for two reasons worth keeping: TypeTable builds one Type for each value in
// the Void..Function range, so a fundamental type has to be inside it, and
// `isInteger()` is a range check starting at Bool, so anything before Bool is
// automatically not an integer - which is the one thing this type must never
// be mistaken for.
//
// It is a *scalar* ([basic.types]/9), which is what lets `!nullptr` and
// `nullptr && x` be written: a contextual conversion to bool, lowered like any
// other to a comparison against zero. That does not make `bool b = nullptr;`
// legal - see nullptr-to-bool-refused - because copy-initialization goes
// through a different gate.

extern "C" int printf(const char *, ...);

struct S { int x; };

int takesPointer(int *p)             { return p == nullptr ? 1 : 2; }
int takesNullPtr(decltype(nullptr) n) { return n == nullptr ? 3 : 4; }

int *givesNull(void) { return nullptr; }

int main(void) {
    int *p = nullptr;
    int q = 7;
    int *r = &q;

    // The pointer shapes it reaches: object, function, and pointer to data
    // member. A pointer to member *function* is not here, and not because of
    // nullptr: it is two words on both ABIs and has no null value yet by any
    // spelling - `= 0` is refused in the same words.
    int (*fn)(int *) = nullptr;
    int S::*dm = nullptr;

    printf("%d %d %d %d %d\n",
           p == nullptr, r != nullptr, nullptr == nullptr, nullptr == 0,
           givesNull() == nullptr);
    printf("%d %d\n", fn == nullptr, dm == nullptr);
    printf("%d %d\n", takesPointer(nullptr), takesNullPtr(nullptr));

    // Assignment after the fact, and the '?:' arms that have to agree.
    r = nullptr;
    int *pick = (q == 7 ? &q : nullptr);
    printf("%d %d\n", r == nullptr, *pick);

    // Contextual bool, three ways, and none of them is an initialization.
    int viaIf = 0;
    if (nullptr) viaIf = 1;
    printf("%d %d %d\n", viaIf, !nullptr, (nullptr || true) ? 1 : 0);

    // The type is a type: it has a size, it can be named, and 0 reaches it.
    decltype(nullptr) n = 0;
    printf("%d %d\n", (int) sizeof(nullptr), n == nullptr);
    return 0;
}
