// An alternative token is a keyword, so it is not a name.
//
// The other half of alternative-tokens.cpp, and the reason this file exists is
// that the two could be broken by the same wrong fix. In C the eleven are
// macros from <iso646.h>, so `int and = 1;` compiles there once the header is
// not included; in C++ they are keywords ([lex.digraph]/2 and table 4), and
// the declaration is ill-formed however the compiler spells its complaint.
//
// A lexer that handed `and` on as an identifier would make every expression in
// alternative-tokens.cpp fail loudly, and this one pass quietly. Only one of
// the two says which mistake was made.
//
// clang says "expected unqualified-id" here, at the same column.

int main() {
    int and = 1;
    return and;
}
