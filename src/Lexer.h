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
    // ladder of types from a decimal one**, so the base has to survive the
    // lexer: `0x80000000` is `unsigned int` and `2147483648` is `long`.
    bool decimal = true;
    // **Whether this floating literal is exactly a `double`**, decided from the
    // digits and not from a host library. Every constant is carried as a double,
    // so this is what lets the parser refuse what it cannot spell everywhere.
    bool exactInDouble = true;
    bool isFloat = false;
    bool suffixF = false;

    long double dvalue = 0;

    bool wide = false;
    // A single-character literal: int in C and char in C++, which is the whole
    // of why this flag exists - sizeof('a') is 4 in one and 1 in the other. A
    // multi-character literal ('ab') is int in both and does not set it.
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
    // Is the decimal literal spanning [from, to) exactly a double? Answered from
    // the digits: M * 10^E is dyadic only where the negative powers of ten divide
    // out and the significand fits in 53 bits. Conservative when it cannot tell.
    static bool exactlyADouble(const std::string &s, std::size_t from,
                               std::size_t to);

    static bool isKeyword(const std::string &word);
};
