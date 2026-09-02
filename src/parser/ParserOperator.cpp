// The parser: the operator ladder.
//
// One function per precedence level, from the cast expression up to the comma,
// in the order [expr] gives them, plus the compound assignments and the
// increments that are written in terms of them.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

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

// **What a member pointer holds is an offset**, so `a.*p` is the object's
// address plus it, read as the member's type - `*(T *)((char *)&a + p)`. The
// cast to `char *` is what stops the pointer arithmetic scaling by the member's
// size, which is the one way this can be got wrong and give a wrong answer
// rather than an error.
//
// No backend was told any of this: the whole operator is an add and two casts
// they already knew.
// `&S::f` - the pair the ABI keeps, built into a slot of this frame: the
// function's address, and on Itanium a `this` adjustment beside it which is
// zero for every case this compiler accepts. The shape is the one
// classTemporary uses, a dereference of a comma, because the address of a comma
// is not something the backends take and the address of `*p` is `p`.
ExprPtr Parser::boundMemberPointer(const Type *cls, const Signature &f,
                                   std::size_t pos) {
    const Type *fn = types_.functionType(f.returns, f.params, f.variadic);
    const Type *mp = types_.memberFunctionPointerTo(cls, fn,
                                                    target_.microsoftNames());
    const int slot = allocateFrameSlot(mp);
    const Type *word = types_.pointerTo(types_.get(Kind::Void));

    Var *code = Var::global(f.name);
    code->setSymbol(f.symbol);
    ExprPtr target(code);
    target->setType(fn);
    ExprPtr addr(new Unary('&', std::move(target)));
    addr->setType(types_.pointerTo(fn));
    ExprPtr asWord(new Cast(word, std::move(addr)));
    asWord->setType(word);

    ExprPtr obj(Var::local("$mfp", slot));
    obj->setType(mp);
    const Member *fnSlot = mp->findMember("$fn");
    ExprPtr dst(new MemberAccess(std::move(obj), "$fn", fnSlot->offset, 0, 0));
    dst->setType(word);
    ExprPtr store(new Assign(std::move(dst), std::move(asWord)));
    store->setType(word);

    ExprPtr chain = std::move(store);
    if (const Member *adj = mp->findMember("$adj")) {
        ExprPtr self(Var::local("$mfp", slot));
        self->setType(mp);
        ExprPtr zeroAt(new MemberAccess(std::move(self), "$adj", adj->offset,
                                        0, 0));
        zeroAt->setType(adj->type);
        ExprPtr zero(new Num(0LL));
        zero->setType(adj->type);
        ExprPtr put(new Assign(std::move(zeroAt), std::move(zero)));
        put->setType(adj->type);
        ExprPtr both(new Comma(std::move(chain), std::move(put)));
        both->setType(adj->type);
        chain = std::move(both);
    }

    ExprPtr whole(Var::local("$mfp", slot));
    whole->setType(mp);
    ExprPtr at(new Unary('&', std::move(whole)));
    at->setType(types_.pointerTo(mp));
    ExprPtr seq(new Comma(std::move(chain), std::move(at)));
    seq->setType(types_.pointerTo(mp));
    ExprPtr made(new Unary('*', std::move(seq)));
    made->setType(mp);
    (void)pos;
    return made;
}

ExprPtr Parser::applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos) {
    const Type *mpt = mp->type()->unqualified();
    // A pointer to a member *function*: read the code pointer out of it and
    // leave the object's address for the call to pick up.
    if (mpt->isMemberFunctionPointer()) {
        const Member *slot = mpt->findMember("$fn");
        ExprPtr held(new MemberAccess(std::move(mp), "$fn", slot->offset, 0, 0));
        const Type *fnPtr = types_.pointerTo(mpt->pointee());
        held->setType(fnPtr);
        boundThis_ = std::move(addr);
        boundFn_ = mpt->pointee();
        boundAt_ = pos;
        return held;
    }
    if (!mpt->isMemberPointer())
        src_.fail(pos, "the right of '.*' has to be a pointer to a member, and "
                       "this is '" + mp->type()->describe() + "'");
    const Type *member = mpt->pointee();
    const Type *bytes = types_.pointerTo(types_.get(Kind::Char));

    ExprPtr asBytes(new Cast(bytes, std::move(addr)));
    asBytes->setType(bytes);
    ExprPtr sum(new Binary(BinOp::Add, std::move(asBytes), std::move(mp)));
    sum->setType(bytes);
    const Type *to = types_.pointerTo(member);
    ExprPtr at(new Cast(to, std::move(sum)));
    at->setType(to);
    ExprPtr read(new Unary('*', std::move(at)));
    read->setType(member);
    return read;
}

// [expr.mptr.oper]: `.*` and `->*` bind tighter than a multiplication and
// looser than a cast, which is the level this sits at.
ExprPtr Parser::memberPointerExpr() {
    ExprPtr n = castExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume(".*")) {
            const Type *of = n->type();
            if (!of->unqualified()->isStructOrUnion())
                src_.fail(pos, "the left of '.*' has to be an object of class "
                               "type, and this is '" + of->describe() + "'");
            ExprPtr addr(new Unary('&', std::move(n)));
            addr->setType(types_.pointerTo(of->unqualified()));
            n = applyMemberPointer(std::move(addr), memberPointerExpr(), pos);
            continue;
        }
        if (consume("->*")) {
            const Type *of = n->type();
            if (!of->isPointer() ||
                !of->pointee()->unqualified()->isStructOrUnion())
                src_.fail(pos, "the left of '->*' has to be a pointer to a "
                               "class, and this is '" + of->describe() + "'");
            n = applyMemberPointer(decay(std::move(n)), memberPointerExpr(), pos);
            continue;
        }
        return n;
    }
}

ExprPtr Parser::mul() {
    ExprPtr n = memberPointerExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("*"))      n = arithmetic(BinOp::Mul, std::move(n), memberPointerExpr(), pos);
        else if (consume("/")) n = arithmetic(BinOp::Div, std::move(n), memberPointerExpr(), pos);
        else if (consume("%")) n = arithmetic(BinOp::Mod, std::move(n), memberPointerExpr(), pos);
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
        std::size_t pos = peek().pos;
        if (consume("<<"))      op = BinOp::Shl;
        else if (consume(">>")) op = BinOp::Shr;
        else return n;

        n = shiftOf(op, std::move(n), add(), pos);
    }
}

ExprPtr Parser::relational() {
    ExprPtr n = shift();
    for (;;) {
        // [temp.names]: a `>` inside a template argument list closes it. This
        // is the whole reason C++ makes `f<(a > b)>` need its parentheses,
        // and the parentheses are where the flag is cleared.
        if (inTemplateArgs_ && (peek().is(">") || peek().is(">>"))) return n;
        std::size_t pos = peek().pos;
        if (consume("<"))       n = comparison(BinOp::Lt, std::move(n), shift(), pos);
        else if (consume("<=")) n = comparison(BinOp::Le, std::move(n), shift(), pos);
        else if (consume(">"))  n = comparison(BinOp::Gt, std::move(n), shift(), pos);
        else if (consume(">=")) n = comparison(BinOp::Ge, std::move(n), shift(), pos);
        else return n;
    }
}

ExprPtr Parser::equality() {
    ExprPtr n = relational();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("=="))      n = comparison(BinOp::Eq, std::move(n), relational(), pos);
        else if (consume("!=")) n = comparison(BinOp::Ne, std::move(n), relational(), pos);
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
        node->setType(types_.get(Kind::Bool));   // [expr.log.and]/1
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
        node->setType(types_.get(Kind::Bool));   // [expr.log.or]/1
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
        // **The copy carries the linker's name too.** A global's `name()` is
        // what the programmer wrote and its `symbol()` is what the object
        // file says - `n` against `?n@@3HA` on the Windows target - and this
        // clone kept only the first. Every operand cloned here is one that is
        // read and then written back, `++n` and `n += 1`, so the *write* went
        // to the mangled name and the read's address was taken of a symbol
        // nothing defines: `EXTERN n:PROC` in the assembly, and a link error
        // on the one target that mangles a variable. The Itanium targets
        // could not show it, because a global's symbol there is its name.
        raw->setSymbol(v->symbol());
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

ExprPtr Parser::shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos) {
    if (ExprPtr call = overloadedBinary(op, lhs, rhs, pos)) return call;
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

    // **`a += b` is not `a = a + b` when a is a class**, and this is where
    // that has to be said. A compound assignment is built here by reading the
    // target back, combining, and storing - which is the right rewrite for a
    // built-in operand and the wrong one for a class, where [over.match.oper]
    // wants `operator+=` and nothing else. Without this the rewrite finds the
    // class's `operator+`, the assignment goes through, and the program is
    // accepted where clang refuses it - which is what it did for as long as it
    // took to write a case for it.
    if (to->unqualified()->isStructOrUnion())
        src_.fail(pos, std::string("'operator") + binOpSpelling(op) +
                       "=' is not supported yet - and a compound assignment on "
                       "a class is that operator alone: it is not rewritten "
                       "into '" + binOpSpelling(op) + "' and an assignment the "
                       "way it is for a built-in type");

    if (ExprPtr readBack = clonePure(*target)) {
        ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
            ? shiftOf(op, std::move(readBack), std::move(value), pos)
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
        ? shiftOf(op, std::move(through[0]), std::move(value), pos)
        : arithmetic(op, std::move(through[0]), std::move(value), pos);
    ExprPtr store(new Assign(std::move(through[1]), convert(std::move(combined), to)));
    store->setType(to);

    ExprPtr node(new Comma(std::move(save), std::move(store)));
    node->setType(to);
    return node;
}

ExprPtr Parser::incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos) {
    // **The dummy `int` is how the standard tells the two apart**, and it is
    // a real parameter rather than a marker: [over.inc] gives the postfix form
    // an extra int and passes 0 in it, which is why `operator++(int)` is
    // written with a parameter nobody names and why the argument is built here
    // rather than being a flag on the call. With it, postfix is the ordinary
    // two-operand resolution and prefix is the one-operand one.
    if (target->type()->unqualified()->isStructOrUnion()) {
        const char *spelling = increment ? "++" : "--";
        const std::string name = std::string("operator") + spelling;
        const Type *self = target->type();

        ExprPtr dummy;
        if (!prefix) {
            dummy.reset(new Num(0LL));
            dummy->setType(types_.intType());
        }
        switch (resolveOperator(name, *target, dummy.get(), pos)) {
        case OperatorChoice::Member: {
            std::vector<ExprPtr> args;
            if (!prefix) args.push_back(std::move(dummy));
            return memberCallWith(std::move(target), self, name, pos,
                                  std::move(args));
        }
        case OperatorChoice::NonMember: {
            std::vector<ExprPtr> args;
            args.push_back(std::move(target));
            if (!prefix) args.push_back(std::move(dummy));
            const Signature &sig = resolveOverload(name, args, pos);
            applyDefaults(sig, args, pos);
            return completeCall(name, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, pos, std::move(args),
                                !sig.owner.empty());
        }
        case OperatorChoice::None:
            src_.fail(pos, std::string("'") + spelling + "' needs a '" + name +
                           "' for '" + self->describe() + "', and there is none");
        }
    }

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
    // std::nullptr_t is a scalar and is still not something to step: it has
    // one value, so there is no next one.
    if (!t->isScalar() || t->isNullPtr())
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

