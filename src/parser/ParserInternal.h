#ifndef CXX1_PARSER_INTERNAL_H
#define CXX1_PARSER_INTERNAL_H

// The parser is one class over the files in this directory, and this is the
// little that has to cross between them: the helpers whose callers sit on both
// sides of a seam, which is why they are not `static`. CLAUDE.md keeps why.

#include "../Ast.h"

#include <string>

// Round `n` up to a multiple of `a`. Alignment arithmetic, wanted by layout,
// by frame allocation and by the backends' data emission alike, which is why
// this one is used from nearly every part of the parser.
int alignTo(int n, int a);

// Does this expression name an object with an address of its own? Overload
// resolution asks (a reference parameter binds by value category) and so does
// reference binding itself, and those two live in different units.
bool isLvalue(const Expr &e);

// Does it name an object, lvalue or xvalue alike? Reference binding wants an
// address and both kinds have one; only the rule that an rvalue reference
// refuses an lvalue needs the two told apart.
bool isGlvalue(const Expr &e);

// The integer literal 0, which [conv.ptr] lets stand for a null pointer.
// Asked by argument ranking and again by the conditional operator.
bool isNullConstant(const Expr &e);

// The keyword, if `word` is one the lexer knows and this parser has no rule
// for. Asked wherever a keyword can appear in a place a rule would have
// consumed it: in an expression, where a type was wanted, and where a name was.
const char *notYetSupported(const std::string &word);

// How a BinOp is written in source, which is what `operator` is followed by
// when one is overloaded. Wanted where the operator is dispatched and again
// where a compound assignment on a class is refused, and those are two units.
const char *binOpSpelling(BinOp op);

#endif
