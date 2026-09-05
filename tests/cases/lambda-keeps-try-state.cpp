// **A replayed body must not clear the enclosing function's exception state.**
// `topLevel` clears functionHasTry_/functionHasPads_/functionTypeIndex_ after it
// emits a function, so a lambda's operator() - replayed in the middle of another
// body - cleared the enclosing function's. functionHasTry_ is what refuses a
// `try` beside a local with a destructor, a pair cxx1 cannot lay out; cleared by
// a lambda written between them, the pair was accepted and reached the assembler
// as an undefined LSDA label. The refusal has to survive the replay, so this
// must still be refused with the lambda present - exactly as it is without it.
extern "C" int printf(const char *, ...);
struct Held { ~Held() { } };
static void g() { }
int main() {
    try { g(); } catch (int) { }
    auto lam = [](int x) { return x + 1; };
    Held h;
    printf("%d\n", lam(1));
    return 0;
}
