// **A replayed body must not clear the enclosing function's exception state.**
// `topLevel` clears functionHasTry_/functionHasPads_/functionTypeIndex_ after
// it emits a function, so a lambda's operator() - replayed in the middle of
// another body - cleared the enclosing function's. That state decides how the
// call-site table is laid out, so losing it produced a function whose LSDA
// named labels nothing defined, and the assembler said so.
//
// **This case used to prove that by being refused**, because a `try` beside a
// local with a destructor was refused function-wide and the lambda made the
// refusal disappear. That shape compiles now - the region is split around the
// `try` and the pair is laid out correctly - so the case proves the same thing
// by *running*: the handler catches, the lambda is called, and ~Held runs once
// at the end. With the state lost, this no longer assembles at all.
extern "C" int printf(const char *, ...);

// Out of line, like throw-class.cpp: defined inside the class they are
// inline, and clang then emits only C2 on x86_64-linux where cxx1 emits C1
// and C2 - a difference about emission the names suite would report as a
// mangling one.
struct Held { int v; Held(int n); ~Held(); };
Held::Held(int n) : v(n) {}
Held::~Held() { printf("~Held "); }
static void g() { throw 3; }

int main() {
    try { g(); } catch (int e) { printf("caught %d ", e); }
    auto lam = [](int x) { return x + 1; };
    Held h(2);
    printf("%d %d\n", lam(1), h.v);
    return 0;
}
