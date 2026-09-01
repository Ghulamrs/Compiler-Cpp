#pragma once

#include <string>
#include <vector>

class Source;

enum class TokenKind { Punct, Num, Str, Ident, Keyword, End };

struct Token {
    TokenKind kind = TokenKind::End;
    long long value = 0;
    bool suffixU = false;
    bool suffixL = false;
    bool suffixLL = false;
    // **[lex.icon] table 6 gives a hexadecimal or octal literal a different
    // ladder of types from a decimal one** - the unsigned rungs are open to
    // it and closed to decimal - so which base it was written in has to
    // survive the lexer. `0x80000000` is `unsigned int`; `2147483648` is
    // `long`, and never unsigned anything.
    bool decimal = true;
    bool isFloat = false;
    bool suffixF = false;

    long double dvalue = 0;

    bool wide = false;
    // A single-character literal. It is int in C and char in C++, which is
    // the whole of why this flag exists: sizeof('a') is 4 in one language and
    // 1 in the other. A multi-character literal ('ab') stays int in both, so
    // this is not set for one.
    bool isChar = false;
    std::string text;
    std::size_t pos = 0;

    bool is(const char *s) const {
        return (kind == TokenKind::Punct || kind == TokenKind::Ident ||
                kind == TokenKind::Keyword) && text == s;
    }
};

class Lexer {
public:
    explicit Lexer(const Source &src) : src_(src) {}

    std::vector<Token> tokenize();

private:
    const Source &src_;

    // A `'` between digits is C++14's separator, and here it opens a
    // character constant - so the number's own position is where it has to
    // be named, not the unterminated literal three lines later.
    void digitSeparator(const std::string &s, std::size_t at) const;

    static bool isKeyword(const std::string &word);
};
