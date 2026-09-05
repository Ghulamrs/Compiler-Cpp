// **`::name` - the global scope and nothing nearer.** It earns its keep only
// where something nearer hides the global name, which is exactly what this
// case sets up: a `cc::Lexer` beside a `::Lexer`, and a member function whose
// class is in the namespace that hides it.
//
// A direct look in the one type table *is* "global scope only": a class or
// typedef at file scope is keyed by its bare name, one inside a namespace by
// its qualified name, and one local to a function lives in a table of its own.
// So the leading `::` says where to look and everything after it is the
// ordinary path.
//
// It was refused by name until 2026-09-04 - three of Compiler++'s sixteen
// sources write `::Lexer *lexer;` in a class body and stopped there.
extern "C" int printf(const char *, ...);

struct Lexer { int k; };
int f() { return 3; }
int shared = 40;

namespace cc {
    // Both names are taken in here, which is what makes the `::` necessary
    // rather than merely explicit.
    struct Lexer { int other; };
    struct Parser {
        // Reached past the nearer `cc::Lexer`, which is what the `::` is for.
        ::Lexer *lexer;
        cc::Lexer *mine;
        int get() { return lexer->k + f() + shared; }
    };
}

int main() {
    ::Lexer real;
    real.k = 1;
    cc::Parser p;
    p.lexer = &real;
    cc::Lexer other;
    other.other = 7;
    p.mine = &other;
    printf("%d %d\n", p.get(), p.mine->other);
    return 0;
}
