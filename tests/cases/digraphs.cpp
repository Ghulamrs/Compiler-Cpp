// The six digraphs of [lex.digraph] table 2, the other half of that table.
//
// `<% %> <: :>` are `{ } [ ]` and belong to the lexer; `%:` and `%:%:` are `#`
// and `##` and belong to the preprocessor, which here reads text rather than
// the lexer's tokens and so needs its own answer. Like the eleven words in
// alternative-tokens.cpp, each behaves in every respect as the token it spells
// - so a brace digraph opens a class, a namespace, a function and a braced
// initializer without any of them having a rule for it, and `<:` is a
// declarator's `[` as readily as a subscript's.
//
// **`<::` is the one place a digraph is not one** ([lex.pptoken]/3). `<::`
// with neither `:` nor `>` after it is `<` followed by `::`, which is what
// makes `Box<::Tag>` a template-id and not `Box[:Tag>`. The exclusion is
// itself excluded when `>` follows, so `<::>` is `[` `]` - both spellings are
// here, and getting either wrong breaks the other.
//
// `N::sum` takes a pointer and not an array **because an array parameter is a
// separate defect**: the Microsoft ABI mangles one as `Q` where a pointer is
// `P`, measured on cl and on clang, and cxx1 writes `P` for both. Found from
// this case's first shape; it is not about digraphs and is recorded in
// CLAUDE.md instead.
//
// Nothing is ambiguous about the rest: `%` and `:` each need a right operand,
// so `%>` and `:>` cannot be the end of one expression and the start of the
// next. `7 % 3` and a ternary are here to say so.
//
// All six were refused with `expected '{'` or `expected a type` before this -
// a message naming no feature, which `tools/exclusions` cannot list. Measured
// against clang++ -std=c++11 -pedantic-errors, which accepts all six.

extern "C" int printf(const char *, ...);

%:define STR(x) %: x
%:define CAT(a, b) a %:%: b
#define ALSO(a, b) a ## b

struct Tag <% int k; %>;

template <class T> struct Box <% T v; %>;

namespace N <%
    int sum(const int *a, int n) <%
        int t = 0;
        for (int i = 0; i < n; i++) t += a<:i:>;
        return t;
    %>
%>

int main() <%
    int a<:3:> = <% 1, 2, 3 %>;
    // `<::>` is `[` `]`, because the [lex.pptoken]/3 exception is itself
    // excluded when a `>` follows the `<::`.
    int e<::> = <% 4, 5 %>;
    printf("%d %d %d\n", a<:1:>, N::sum(a, 3), N::sum(e, 2));

    Box<::Tag> b;
    b.v.k = 9;
    printf("%d\n", b.v.k);

    int xy = 4, zw = 6;
    printf("%s %d %d\n", STR(word), CAT(x, y), ALSO(z, w));

%:if 1 and not 0
    printf("pct if\n");
%:endif

    // `%` is still modulo and `:` still a ternary's, which is why `%>` and
    // `:>` can be digraphs at all.
    printf("%d %d\n", 7 % 3, a<:0:> < 2 ? 1 : 0);
    return 0;
%>
