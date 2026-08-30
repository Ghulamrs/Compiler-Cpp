#ifndef CXX1_PARSER_INTERNAL_H
#define CXX1_PARSER_INTERNAL_H

// The parser is one class spread over nine translation units, and this is the
// little that has to cross between them.
//
// Everything here was a file-scope `static` while the parser was a single
// source file. Splitting the file did not make any of it public - none of it
// is declared in Parser.h, and nothing outside src/Parser*.cpp includes this -
// but three helpers turned out to have callers on both sides of a seam, and a
// `static` cannot be seen from another unit. So they lost the keyword and
// gained a declaration, and their definitions sit together in Parser.cpp.
//
// It is deliberately four and not thirty. Every other helper the parser had
// stayed `static` in the file that uses it, which is what decided where the
// seams went: a cut that would have orphaned a helper was moved rather than
// paid for with an entry here. The fourth was added afterwards and on
// purpose: `notYetSupported` had one caller when it was written and wants
// three, because the refusal it exists to give is wanted wherever a keyword
// can turn up and not only in an expression.

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
// consumed it: in an expression, where a type was wanted, and where a name
// was. Answering the word is the whole point - see the definition.
const char *notYetSupported(const std::string &word);

// How a BinOp is written in source, which is what `operator` is followed by
// when one is overloaded. Wanted where the operator is dispatched and again
// where a compound assignment on a class is refused, and those are two units.
const char *binOpSpelling(BinOp op);

#endif
