// The parser: what is evaluated where it is written. `static_assert` and an
// exception specification are both read and answered at the point they stand,
// and under them is the constant folder every such answer goes through - the
// same one a case label, an array bound and a global's initialiser ask.
//
// Split out of ParserStmt.cpp, which is the file that reads statements; this is
// the arithmetic those statements are checked against, and it names no Stmt.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// **An alias declaration is C++11 and is not a using-declaration** - it names
// a type where the other names an entity, and it is what a program writes in
// place of a typedef. Refused here so that the three using-declaration
// refusals do not answer for it, each in a different scope.
void Parser::refuseAliasDeclaration() {
    if (!peek().is("using")) return;
    if (peekAt(1).kind != TokenKind::Ident || !peekAt(2).is("=")) return;
    src_.fail(peek().pos, "an alias declaration - 'using X = T;' - is not "
                          "supported yet, though it is C++11: 'typedef T X;' "
                          "says the same thing here");
}

// **`= default` and `= delete` are C++11 and sit exactly where `= 0` does.**
// The first asks for the member the compiler would have written; the second
// leaves a candidate that overload resolution must find and then refuse,
// which is not the same as one that was never declared.
void Parser::refuseDefaultedOrDeleted() {
    if (!peek().is("=")) return;
    const bool def = peekAt(1).is("default");
    if (!def && !peekAt(1).is("delete")) return;
    src_.fail(peek().pos, std::string("'= ") + (def ? "default" : "delete") +
                          "' is not supported yet: a defaulted member is "
                          "written with an empty body here, and a deleted one "
                          "by declaring it private and never defining it");
}

bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In C++11 it is
// *not* part of the function's type - measured, both spellings mangle alike - so what
// it buys is `noexcept(e)`. `throw()` is taken as one; `throw(int)` is refused.
bool Parser::exceptionSpecification() {
    if (consume("noexcept")) {
        if (!consume("(")) return true;
        const std::size_t at = peek().pos;
        const long long value = constantExpression("a constant in 'noexcept('");
        expect(")");
        (void) at;
        return value != 0;
    }
    if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (!consume(")"))
            src_.fail(at, "a dynamic exception specification - 'throw(T)' - is "
                          "not supported yet; it needs a run-time check of the "
                          "thrown type against a list, where 'noexcept' is a "
                          "promise the compiler only has to record. 'throw()' "
                          "with nothing in it is 'noexcept' and works");
        return true;
    }
    return false;
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    long long v;
    if (!fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

// **What moved, and what did not.** fold(), narrowTo() and singleReturnValue()
// are arithmetic over an expression that has already been built, and they are
// ConstantEvaluator's now. They keep their names here because twenty-six call
// sites across the parser read better saying `fold(e, &v, pos)`, and because a
// forwarder is where the change stops - no other file had to be touched for it.
bool Parser::fold(const Expr &e, long long *out, std::size_t pos) const {
    return constants_.fold(e, out, pos);
}

const Expr *Parser::singleReturnValue(const Stmt &body) const {
    return constants_.singleReturnValue(body);
}

long long Parser::narrowTo(long long v, const Type *t) const {
    return constants_.narrowTo(v, t);
}

// **This one stays.** `Init` is the parser's own shape for what a declaration
// was written with, so asking whether it is a constant is a question about a
// declaration rather than about arithmetic. It asks the folder for the last step.
bool Parser::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    if (t == nullptr || !t->isConst() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return constants_.fold(*in.value, out, in.pos);
}
