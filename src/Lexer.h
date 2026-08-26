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

    static bool isKeyword(const std::string &word);
};
