// The parser: the operator ladder.
//
// One function per precedence level, from the cast expression up to the comma,
// in the order [expr] gives them, plus the compound assignments and the
// increments that are written in terms of them.
#include "Parser.h"
#include "ParserInternal.h"
#include "Mangle.h"
#include "Source.h"

#include <climits>
#include <cstring>

ExprPtr Parser::castExpr() {
    if (peek().is("(")) {
        std::size_t save = at_;
        at_++;
        if (atTypeName()) {
            StorageClass sc;
            const Type *to = specifiers(&sc);
            to = declarator(to, true).type;
            expect(")");
            ExprPtr v = decay(castExpr());
            if (to->isVoid()) return ExprPtr(new Cast(to, std::move(v)));
            return convert(std::move(v), to);
        }
        at_ = save;
    }
    return unary();
}

ExprPtr Parser::mul() {
    ExprPtr n = castExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("*"))      n = arithmetic(BinOp::Mul, std::move(n), castExpr(), pos);
        else if (consume("/")) n = arithmetic(BinOp::Div, std::move(n), castExpr(), pos);
        else if (consume("%")) n = arithmetic(BinOp::Mod, std::move(n), castExpr(), pos);
        else return n;
    }
}

ExprPtr Parser::add() {
    ExprPtr n = mul();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("+"))      n = arithmetic(BinOp::Add, std::move(n), mul(), pos);
        else if (consume("-")) n = arithmetic(BinOp::Sub, std::move(n), mul(), pos);
        else return n;
    }
}

ExprPtr Parser::shift() {
    ExprPtr n = add();
    for (;;) {
        BinOp op;
        if (inTemplateArgs_ && peek().is(">>")) return n;
        if (consume("<<"))      op = BinOp::Shl;
        else if (consume(">>")) op = BinOp::Shr;
        else return n;

        n = shiftOf(op, std::move(n), add());
    }
}

ExprPtr Parser::relational() {
    ExprPtr n = shift();
    for (;;) {
        // [temp.names]: a `>` inside a template argument list closes it. This
        // is the whole reason C++ makes `f<(a > b)>` need its parentheses,
        // and the parentheses are where the flag is cleared.
        if (inTemplateArgs_ && (peek().is(">") || peek().is(">>"))) return n;
        if (consume("<"))       n = comparison(BinOp::Lt, std::move(n), shift());
        else if (consume("<=")) n = comparison(BinOp::Le, std::move(n), shift());
        else if (consume(">"))  n = comparison(BinOp::Gt, std::move(n), shift());
        else if (consume(">=")) n = comparison(BinOp::Ge, std::move(n), shift());
        else return n;
    }
}

ExprPtr Parser::equality() {
    ExprPtr n = relational();
    for (;;) {
        if (consume("=="))      n = comparison(BinOp::Eq, std::move(n), relational());
        else if (consume("!=")) n = comparison(BinOp::Ne, std::move(n), relational());
        else return n;
    }
}

ExprPtr Parser::bitAnd() {
    ExprPtr n = equality();
    while (peek().is("&")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitAnd, std::move(n), equality(), pos);
    }
    return n;
}

ExprPtr Parser::bitXor() {
    ExprPtr n = bitAnd();
    while (peek().is("^")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitXor, std::move(n), bitAnd(), pos);
    }
    return n;
}

ExprPtr Parser::bitOr() {
    ExprPtr n = bitXor();
    while (peek().is("|")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitOr, std::move(n), bitXor(), pos);
    }
    return n;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr n = bitOr();
    while (peek().is("&&")) {
        std::size_t pos = peek().pos;
        at_++;
        ExprPtr r = decay(bitOr());
        n = decay(std::move(n));
        requireScalar(*n, pos, "'&&'");
        requireScalar(*r, pos, "'&&'");
        ExprPtr node(new Binary(BinOp::LAnd, std::move(n), std::move(r)));
        node->setType(types_.intType());
        n = std::move(node);
    }
    return n;
}

ExprPtr Parser::logicalOr() {
    ExprPtr n = logicalAnd();
    while (peek().is("||")) {
        std::size_t pos = peek().pos;
        at_++;
        ExprPtr r = decay(logicalAnd());
        n = decay(std::move(n));
        requireScalar(*n, pos, "'||'");
        requireScalar(*r, pos, "'||'");
        ExprPtr node(new Binary(BinOp::LOr, std::move(n), std::move(r)));
        node->setType(types_.intType());
        n = std::move(node);
    }
    return n;
}

ExprPtr Parser::clonePure(const Expr &e) {
    if (const Num *n = dynamic_cast<const Num *>(&e)) {

        ExprPtr c(n->type() && n->type()->isFloating() ? new Num(n->dvalue())
                                                      : new Num(n->value()));
        c->setType(n->type());
        return c;
    }
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        Var *raw = v->isLocal() ? Var::local(v->name(), v->offset())
                                : Var::global(v->name());
        raw->setReadOnly(v->readOnly());
        raw->setNoAddress(v->noAddress());
        ExprPtr c(raw);
        c->setType(v->type());
        return c;
    }
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        ExprPtr c(new StrLit(s->label(), s->text()));
        c->setType(s->type());
        return c;
    }
    if (const Cast *k = dynamic_cast<const Cast *>(&e)) {

        ExprPtr inner = clonePure(k->value());
        if (!inner) return nullptr;
        return ExprPtr(new Cast(k->type(), std::move(inner)));
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        ExprPtr inner = clonePure(u->operand());
        if (!inner) return nullptr;
        ExprPtr c(new Unary(u->op(), std::move(inner)));
        c->setType(u->type());
        return c;
    }
    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        ExprPtr l = clonePure(b->lhs());
        if (!l) return nullptr;
        ExprPtr r = clonePure(b->rhs());
        if (!r) return nullptr;
        ExprPtr c(new Binary(b->op(), std::move(l), std::move(r)));
        c->setType(b->type());
        return c;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        ExprPtr obj = clonePure(m->object());
        if (!obj) return nullptr;
        ExprPtr c(new MemberAccess(std::move(obj), m->name(), m->offset(),
                                   m->width(), m->bitOffset()));
        c->setType(m->type());
        return c;
    }
    return nullptr;
}

ExprPtr Parser::cloneLvalue(const Expr &e, std::size_t pos) {
    if (ExprPtr copy = clonePure(e)) return copy;
    src_.fail(pos, "the left of a compound assignment is read and then written, "
                   "so it is evaluated twice, and this one has an effect that "
                   "cannot happen twice - give the subscript or the call a name "
                   "first, or write it out as 'x = x op e'");
}

ExprPtr Parser::shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs) {
    const Type *lt = promote(lhs->type());
    const Type *rt = promote(rhs->type());
    ExprPtr n(new Binary(op, convert(std::move(lhs), lt),
                             convert(std::move(rhs), rt)));
    n->setType(lt);
    return n;
}

void Parser::requireAssignable(const Expr &e, std::size_t pos, const char *what) {
    if (!isLvalue(e))
        src_.fail(pos, std::string(what) + " is not something that can be assigned to");
    if (e.type()->isArray())
        src_.fail(pos, "an array cannot be assigned to");
    if (const Var *v = dynamic_cast<const Var *>(&e))
        if (v->readOnly())
            src_.fail(pos, "'" + v->name() + "' is const and cannot be assigned to");
    // Reaching a const through a pointer or a member is the case the
    // read-only flag on the object cannot see: nothing here is a named
    // object, and the only record that it may not be written is its type.
    if (e.type()->isConst())
        src_.fail(pos, std::string(what) + " is '" + e.type()->describe() +
                       "', and a const cannot be assigned to");
}

ExprPtr Parser::compound(BinOp op, ExprPtr target, ExprPtr value, std::size_t pos) {
    requireAssignable(*target, pos, "the left of a compound assignment");
    const Type *to = target->type();

    if (ExprPtr readBack = clonePure(*target)) {
        ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
            ? shiftOf(op, std::move(readBack), std::move(value))
            : arithmetic(op, std::move(readBack), std::move(value), pos);
        ExprPtr node(new Assign(std::move(target), convert(std::move(combined), to)));
        node->setType(to);
        return node;
    }

    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(target.get()))
        if (m->isBitField())
            src_.fail(pos, "'" + m->name() + "' is a bit-field, so it has no "
                           "address to take - and the object it is reached "
                           "through has an effect that cannot happen twice; "
                           "give that object a name first");

    const Type *ptr = types_.pointerTo(to);
    int slot = allocateFrameSlot(ptr);

    const std::string hidden = "$compound";

    ExprPtr addr(new Unary('&', std::move(target)));
    addr->setType(ptr);
    ExprPtr slotVar(Var::local(hidden, slot));
    slotVar->setType(ptr);
    ExprPtr save(new Assign(std::move(slotVar), std::move(addr)));
    save->setType(ptr);

    ExprPtr through[2];
    for (int k = 0; k < 2; k++) {
        ExprPtr v(Var::local(hidden, slot));
        v->setType(ptr);
        ExprPtr d(new Unary('*', std::move(v)));
        d->setType(to);
        through[k] = std::move(d);
    }

    ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
        ? shiftOf(op, std::move(through[0]), std::move(value))
        : arithmetic(op, std::move(through[0]), std::move(value), pos);
    ExprPtr store(new Assign(std::move(through[1]), convert(std::move(combined), to)));
    store->setType(to);

    ExprPtr node(new Comma(std::move(save), std::move(store)));
    node->setType(to);
    return node;
}

ExprPtr Parser::incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos) {
    if (prefix) {
        ExprPtr one(new Num(1LL));
        one->setType(types_.intType());
        return compound(increment ? BinOp::Add : BinOp::Sub, std::move(target),
                        std::move(one), pos);
    }

    const char *what = increment ? "the operand of postfix '++'"
                                 : "the operand of postfix '--'";
    requireAssignable(*target, pos, what);
    const Type *t = target->type();
    if (!t->isScalar())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       t->describe() + "'");
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(target.get()))
        if (m->isBitField())
            src_.fail(pos, "postfix '++' and '--' on a bit-field are not supported "
                           "yet - the prefix form works, and so does 'f.a = f.a + 1'");
    if (t->isPointer() && !t->pointee()->isComplete())
        src_.fail(pos, std::string(what) + " is '" + t->describe() +
                       "', and there is no size to step by");

    long long step = t->isPointer() ? t->pointee()->size(target_) : 1;
    ExprPtr n(new Postfix(std::move(target), increment, step));
    n->setType(t);
    return n;
}

ExprPtr Parser::conditional() {
    ExprPtr cond = logicalOr();
    if (!peek().is("?")) return cond;

    std::size_t pos = peek().pos;
    at_++;
    cond = decay(std::move(cond));
    requireScalar(*cond, pos, "the condition of '?:'");

    ExprPtr a = decay(expr());
    expect(":");
    ExprPtr b = decay(conditional());

    const Type *ta = a->type();
    const Type *tb = b->type();
    const Type *result = nullptr;

    if (ta->isArithmetic() && tb->isArithmetic()) {
        result = usualArithmetic(ta, tb);
        a = convert(std::move(a), result);
        b = convert(std::move(b), result);
    } else if (ta == tb) {
        result = ta;
    } else if (ta->isPointer() && isNullConstant(*b)) {
        result = ta;
        b = convert(std::move(b), result);
    } else if (tb->isPointer() && isNullConstant(*a)) {
        result = tb;
        a = convert(std::move(a), result);
    } else {
        src_.fail(pos, "the arms of '?:' have incompatible types '" +
                       ta->describe() + "' and '" + tb->describe() + "'");
    }

    ExprPtr n(new Conditional(std::move(cond), std::move(a), std::move(b)));
    n->setType(result);
    return n;
}

ExprPtr Parser::assign() {
    ExprPtr n = conditional();

    static const struct { const char *tok; BinOp op; } kCompound[] = {
        { "+=", BinOp::Add }, { "-=", BinOp::Sub }, { "*=", BinOp::Mul },
        { "/=", BinOp::Div }, { "%=", BinOp::Mod }, { "&=", BinOp::BitAnd },
        { "|=", BinOp::BitOr }, { "^=", BinOp::BitXor },
        { "<<=", BinOp::Shl }, { ">>=", BinOp::Shr },
    };
    for (const auto &c : kCompound) {
        if (peek().is(c.tok)) {
            std::size_t pos = peek().pos; at_++;
            return compound(c.op, std::move(n), decay(assign()), pos);
        }
    }

    if (!peek().is("=")) return n;
    std::size_t pos = peek().pos;
    at_++;

    requireAssignable(*n, pos, "the left of '='");

    const Type *to = n->type();
    ExprPtr value = decay(assign());
    checkAssignable(*value, to, pos, "the left of '='");

    // **A class with a copy assignment of its own is assigned by calling it**,
    // not by moving its bytes. Where the copy is trivial there is no such
    // function and nothing was declared, and this is the struct assignment it
    // has always been.
    if (const Signature *op = copyAssignOf(to->unqualified())) {
        functions_[static_cast<std::size_t>(op - &functions_[0])].used = true;
        const Type *selfPtr = types_.pointerTo(to->unqualified());
        ExprPtr addr(new Unary('&', std::move(n)));
        addr->setType(selfPtr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        args.push_back(std::move(value));
        std::vector<const Type *> ps;
        ps.push_back(selfPtr);
        ps.push_back(op->params[0]);
        ExprPtr call = completeCall(to->unqualified()->tag() + "::operator=",
                                    op->symbol, nullptr, selfPtr, ps, false, pos,
                                    std::move(args));
        // It answers `X &`, which is a pointer below the parser - so what the
        // expression yields is that pointer read back, and `(a = b).m` goes on
        // working the way it does for a written assignment.
        ExprPtr result(new Unary('*', std::move(call)));
        result->setType(to->unqualified());
        return result;
    }

    ExprPtr node(new Assign(std::move(n), convert(std::move(value), to)));
    node->setType(to);
    return node;
}

ExprPtr Parser::expr() {
    ExprPtr n = assign();
    while (consume(",")) {
        ExprPtr right = decay(assign());
        const Type *t = right->type();
        ExprPtr c(new Comma(std::move(n), std::move(right)));
        c->setType(t);
        n = std::move(c);
    }
    return n;
}

