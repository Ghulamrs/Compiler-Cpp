// The parser: expressions, up to the operator ladder.
//
// Pointer arithmetic and the usual conversions, primary expressions, calls
// and the temporaries they need, references and how they bind, member access,
// throw, new and delete.
#include "Parser.h"
#include "ParserInternal.h"
#include "Mangle.h"
#include "Source.h"

#include <climits>
#include <cstring>

ExprPtr Parser::pointerAdd(ExprPtr p, ExprPtr n) {
    const Type *pt = p->type();
    long long stride = pt->pointee()->size(target_);

    ExprPtr size(new Num(stride));
    size->setType(types_.get(Kind::Long));
    ExprPtr scaled(new Binary(BinOp::Mul,
                              convert(std::move(n), types_.get(Kind::Long)),
                              std::move(size)));
    scaled->setType(types_.get(Kind::Long));

    ExprPtr sum(new Binary(BinOp::Add, std::move(p), std::move(scaled)));
    sum->setType(pt);
    return sum;
}

ExprPtr Parser::pointerSub(ExprPtr l, ExprPtr r, std::size_t pos) {
    if (l->type()->pointee() != r->type()->pointee())
        src_.fail(pos, "'" + l->type()->describe() + "' minus '" +
                       r->type()->describe() + "' needs the same pointee type");
    long long stride = l->type()->pointee()->size(target_);

    ExprPtr diff(new Binary(BinOp::Sub, std::move(l), std::move(r)));
    diff->setType(types_.get(Kind::Long));
    ExprPtr size(new Num(stride));
    size->setType(types_.get(Kind::Long));
    ExprPtr n(new Binary(BinOp::Div, std::move(diff), std::move(size)));
    n->setType(types_.get(Kind::Long));
    return n;
}

ExprPtr Parser::arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos) {
    lhs = decay(std::move(lhs));
    rhs = decay(std::move(rhs));

    if (op == BinOp::Add) {
        if (lhs->type()->isPointer() && rhs->type()->isInteger())
            return pointerAdd(std::move(lhs), std::move(rhs));
        if (lhs->type()->isInteger() && rhs->type()->isPointer())
            return pointerAdd(std::move(rhs), std::move(lhs));
    }
    if (op == BinOp::Sub && lhs->type()->isPointer()) {
        if (rhs->type()->isInteger()) {
            const Type *lt = promote(rhs->type());
            ExprPtr neg(new Unary('-', convert(std::move(rhs), lt)));
            neg->setType(lt);
            return pointerAdd(std::move(lhs), std::move(neg));
        }
        if (rhs->type()->isPointer())
            return pointerSub(std::move(lhs), std::move(rhs), pos);
    }

    if (!lhs->type()->isArithmetic() || !rhs->type()->isArithmetic())
        src_.fail(pos, "'" + lhs->type()->describe() + "' and '" +
                       rhs->type()->describe() + "' cannot be combined like that");
    if (op == BinOp::Mod && (lhs->type()->isFloating() || rhs->type()->isFloating()))
        src_.fail(pos, "'%' needs integers, not floating point");

    const Type *common = usualArithmetic(lhs->type(), rhs->type());
    ExprPtr n(new Binary(op, convert(std::move(lhs), common),
                             convert(std::move(rhs), common)));
    n->setType(common);
    return n;
}

ExprPtr Parser::comparison(BinOp op, ExprPtr lhs, ExprPtr rhs) {
    lhs = decay(std::move(lhs));
    rhs = decay(std::move(rhs));
    ExprPtr n;
    if (lhs->type()->isPointer() || rhs->type()->isPointer()) {
        n = ExprPtr(new Binary(op, std::move(lhs), std::move(rhs)));
    } else {
        const Type *common = usualArithmetic(lhs->type(), rhs->type());
        n = ExprPtr(new Binary(op, convert(std::move(lhs), common),
                                   convert(std::move(rhs), common)));
    }
    n->setType(types_.intType());
    return n;
}

// Recognised by the lexer, with no rule in this parser yet. Naming the
// keyword is the whole point: without this the word reaches expression
// parsing as an unknown identifier and the error lands on whatever follows
// it, which is never where the reader is looking.
static const char *notYetSupported(const std::string &word) {
    static const char *const pending[] = {
        "alignas", "alignof", "and", "and_eq", "asm",
        "bitand", "bitor", "catch", "char16_t", "char32_t", "compl",
        "constexpr", "const_cast", "dynamic_cast",
        "explicit", "export", "friend", "inline", "mutable", "namespace",
        "noexcept", "not", "not_eq", "nullptr", "operator", "or",
        "or_eq", "reinterpret_cast",
        "static_assert", "static_cast", "template", "thread_local",
        "typeid", "using", "virtual",
        "xor", "xor_eq"
    };
    for (const char *k : pending)
        if (word == k) return k;
    return nullptr;
}

ExprPtr Parser::primary(Program *program) {
    if (peek().is("true") || peek().is("false")) {
        bool value = peek().is("true");
        at_++;
        ExprPtr n(new Num(static_cast<long long>(value ? 1 : 0)));
        n->setType(types_.get(Kind::Bool));
        return n;
    }

    // `this` is the hidden first parameter, read back. It is a pointer and not
    // a reference, which is C++'s own choice and the reason `->` is written so
    // often inside a member function.
    if (peek().is("this")) {
        std::size_t pos = peek().pos;
        at_++;
        if (currentClass_ == nullptr)
            src_.fail(pos, "'this' is only inside a member function, and this "
                           "is not one");
        const Local *slot = findLocal("this");
        ExprPtr v(Var::local("this", slot != nullptr ? slot->offset : thisOffset_));
        v->setType(slot != nullptr ? slot->type
                                   : types_.pointerTo(currentClass_));
        return v;
    }

    if (peek().kind == TokenKind::Keyword) {
        if (const char *pending = notYetSupported(peek().text))
            src_.fail(peek().pos, std::string("'") + pending +
                                  "' is not supported yet");
    }

    // A template named in an expression. A function template with its
    // arguments written is instantiated; everything else is refused by name,
    // and with the argument list stepped over first so that the reader is
    // told about the template rather than about the `<`.
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text))
        return templateCall(program);

    if (peek().is("__builtin_va_start")) {
        std::size_t pos = peek().pos;
        at_++;

        if (!variadicBody_)
            src_.fail(pos, "va_start is only allowed in a function declared "
                           "with '...'");
        expect("(");

        ExprPtr list = decay(assign());
        expect(")");
        if (!list->type()->isPointer())
            src_.fail(pos, "va_start needs a va_list");
        ExprPtr n(new VaStart(std::move(list)));
        n->setType(types_.voidType());
        return n;
    }

    if (peek().is("__builtin_va_arg")) {
        std::size_t pos = peek().pos;
        at_++;
        if (!variadicBody_)
            src_.fail(pos, "va_arg is only allowed in a function declared "
                           "with '...'");
        expect("(");
        ExprPtr list = decay(assign());
        if (!list->type()->isPointer())
            src_.fail(pos, "va_arg needs a va_list");
        expect(",");
        StorageClass sc;
        const Type *want = specifiers(&sc);
        want = declarator(want, true).type;
        expect(")");

        if (!want->isComplete())
            src_.fail(pos, "va_arg needs a complete type");

        const char *promotes = nullptr;
        switch (want->kind()) {
        case Kind::Char: case Kind::SChar: case Kind::UChar:
        case Kind::Short: case Kind::UShort: promotes = "int"; break;
        case Kind::Float:                    promotes = "double"; break;
        default: break;
        }
        if (promotes != nullptr)
            src_.fail(pos, "'" + want->describe() + "' is promoted before it "
                           "reaches a variadic function, so va_arg cannot ask "
                           "for it - ask for '" + promotes + "'");

        if (want->isStructOrUnion() || want->isArray())
            src_.fail(pos, "va_arg of an aggregate is not supported yet");

        ExprPtr n(new VaArg(std::move(list)));
        n->setType(want);
        return n;
    }

    if (consume("(")) {
        // Inside parentheses a `>` is an operator again, which is exactly why
        // C++ makes a comparison in a template argument need them.
        const bool wasInArgs = inTemplateArgs_;
        inTemplateArgs_ = false;
        ExprPtr e = expr();
        inTemplateArgs_ = wasInArgs;
        expect(")");
        return e;
    }

    if (peek().kind == TokenKind::Str) {
        std::string label = ".L.str." + std::to_string(strings_++);
        std::string text = peek().text;
        at_++;

        bool wide = tokens_[at_ - 1].wide;
        while (peek().kind == TokenKind::Str) {
            text += peek().text;

            wide = wide || peek().wide;
            at_++;
        }

        const Type *elem = wide ? types_.get(target_.wcharType())
                                : types_.charType();
        int width = elem->size(target_);

        std::string bytes;
        for (unsigned char ch : text) {
            bytes.push_back(static_cast<char>(ch));
            for (int k = 1; k < width; k++) bytes.push_back('\0');
        }
        for (int k = 0; k < width; k++) bytes.push_back('\0');

        program->strings.push_back(StringLit{ label, bytes, width });
        ExprPtr n(new StrLit(label, text));
        n->setType(types_.arrayOf(types_.withConst(elem),
                                  static_cast<long long>(text.size()) + 1));
        return n;
    }

    if (peek().kind == TokenKind::Num && peek().isFloat) {
        const Token &t = peek();
        ExprPtr n(new Num(t.dvalue));
        n->setType(types_.get(t.suffixF ? Kind::Float
                            : t.suffixL ? Kind::LongDouble : Kind::Double));
        at_++;
        return n;
    }

    if (peek().kind == TokenKind::Num) {
        const Token &t = peek();
        const Type *ty;
        unsigned long long u = static_cast<unsigned long long>(t.value);

        auto fits = [&](Kind k) {
            const Type *c = types_.get(k);
            int bits = c->size(target_) * 8;
            unsigned long long limit =
                c->isSigned(target_) ? (1ULL << (bits - 1)) - 1
                                     : (bits >= 64 ? ~0ULL : (1ULL << bits) - 1);
            return u <= limit;
        };

        if (t.suffixU && t.suffixLL)     ty = types_.get(Kind::ULongLong);
        else if (t.suffixLL)             ty = fits(Kind::LongLong)
                                            ? types_.get(Kind::LongLong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixU && t.suffixL) ty = fits(Kind::ULong)
                                            ? types_.get(Kind::ULong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixU)              ty = fits(Kind::UInt)  ? types_.get(Kind::UInt)
                                            : fits(Kind::ULong) ? types_.get(Kind::ULong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixL)              ty = fits(Kind::Long)  ? types_.get(Kind::Long)
                                            : fits(Kind::ULong) ? types_.get(Kind::ULong)
                                            : types_.get(Kind::LongLong);

        else if (t.wide)                 ty = types_.get(target_.wcharType());
        // [lex.ccon]/2: an ordinary character literal has type char, where C
        // gives it int. So sizeof('a') is 1 here, and a program that stores
        // one in a char is not narrowing anything.
        else if (t.isChar)               ty = types_.get(Kind::Char);
        else if (fits(Kind::Int))        ty = types_.intType();
        else if (fits(Kind::Long))       ty = types_.get(Kind::Long);

        else if (fits(Kind::ULong))      ty = types_.get(Kind::ULong);
        else if (fits(Kind::LongLong))   ty = types_.get(Kind::LongLong);
        else                             ty = types_.get(Kind::ULongLong);
        ExprPtr n(new Num(t.value));
        n->setType(ty);
        at_++;
        return n;
    }

    // **`Counter::total` - a static member named through its class.** Asked
    // before the identifier is read as a name of its own, and only taken when
    // the class really does have such a member, so `Point::get()` and anything
    // else spelled with a '::' falls through untouched.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident) {
        // The longest prefix that names a class and has such a member wins, so
        // that `Outer::Inner::shared` finds Inner's rather than stopping at
        // Outer.
        std::string q = peek().text;
        const Type *owner = nullptr;
        std::string member;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (const Type *cls = findTypedef(q))
                if (cls->isStructOrUnion() &&
                    cls->findStaticMember(component) != nullptr) {
                    owner = cls;
                    member = component;
                    consumed = k + 2;
                }
            q += "::" + component;
        }
        if (owner != nullptr) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            return staticMemberRef(owner, *owner->findStaticMember(member),
                                   owner->tag(), qpos);
        }
    }

    if (peek().kind == TokenKind::Ident) {
        std::string name = peek().text;
        std::size_t pos = peek().pos;

        const Local *l = findLocal(name);
        const GlobalSym *g = l != nullptr ? nullptr : findGlobal(name);
        const Type *held = l != nullptr ? l->type : (g != nullptr ? g->type : nullptr);
        bool callsThroughObject = held != nullptr && held->isFunctionPointer();

        // **An unqualified static member, inside a member function.** It needs
        // no object, which is what lets it be answered here rather than
        // through `this` the way an ordinary member is. A local or a global of
        // the same name is nearer and was already found above.
        if (l == nullptr && g == nullptr && currentClass_ != nullptr &&
            !peekAt(1).is("(")) {
            if (const Type::StaticMember *s =
                    currentClass_->findStaticMember(name)) {
                at_++;
                return staticMemberRef(currentClass_, *s, currentClass_->tag(),
                                       pos);
            }
        }

        // An unqualified call inside a member function looks for a member of
        // this class first - [class.mfct.non-static] makes `secret()` mean
        // `this->secret()`. It has to be asked before the free-function branch
        // below, which would otherwise report a member as undeclared.
        bool inherited = false;
        for (const Type *c = currentClass_; c != nullptr; c = c->base())
            if (overloadsOf(c->tag() + "::" + name) != nullptr) { inherited = true; break; }
        if (peekAt(1).is("(") && !callsThroughObject && currentClass_ != nullptr &&
            l == nullptr && g == nullptr && inherited) {
            if (const Local *self = findLocal("this")) {
                at_ += 2;
                ExprPtr me(Var::local("this", self->offset));
                me->setType(self->type);
                ExprPtr obj(new Unary('*', std::move(me)));
                obj->setType(self->type->pointee());
                return memberCall(std::move(obj), self->type->pointee(), name, pos);
            }
        }

        if (peekAt(1).is("(") && !callsThroughObject) {
            at_ += 2;
            // The arguments first, then the function: with a set to choose
            // from there is nothing to convert them to until one is chosen.
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverload(name, args, pos);
            return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                                sig.variadic, pos, std::move(args));
        }

        at_++;
        if (const EnumConst *e = findEnum(name)) {
            ExprPtr n(new Num(e->value));
            n->setType(types_.intType());
            return n;
        }
        if (ExprPtr v = objectRef(name)) return v;

        // Inside a member function an unqualified name may be a member of the
        // class - [class.mfct.non-static] says it is `this->name`, and that is
        // exactly what is built here rather than a second kind of lookup.
        if (currentClass_ != nullptr && findLocal(name) == nullptr &&
            findGlobal(name) == nullptr) {
            const Local *self = findLocal("this");
            if (const Member *m = currentClass_->findMember(name)) {
                if (self == nullptr)
                    src_.fail(pos, "'" + name + "' is a member and there is no "
                                   "object here to read it from");
                ExprPtr me(Var::local("this", self->offset));
                me->setType(self->type);
                ExprPtr obj(new Unary('*', std::move(me)));
                const Type *held = self->type->pointee();
                obj->setType(held);
                ExprPtr acc(new MemberAccess(std::move(obj), name, m->offset,
                                             m->width, m->bitOffset));
                acc->setType(held->isConst() ? types_.withConst(m->type) : m->type);
                return acc;
            }
        }

        // Taking the address of an overloaded name needs a target type to
        // choose by - [over.over] - and there is none here. Refused by name
        // rather than by silently taking the first, which would compile and
        // call the wrong function.
        if (const std::vector<std::size_t> *set = overloadsOf(name)) {
            if (set->size() > 1)
                src_.fail(pos, "'" + name + "' names " +
                               std::to_string(set->size()) + " functions, and "
                               "which one this is cannot be told from the use "
                               "alone - choosing an overload by the type it is "
                               "assigned to is not supported yet");
        }
        if (const Signature *sig = findFunction(name)) {
            Var *v = Var::global(name);
            ExprPtr target(v);
            const Type *fn = types_.functionType(sig->returns, sig->params,
                                                 sig->variadic);
            target->setType(fn);
            ExprPtr n(new Unary('&', std::move(target)));
            n->setType(types_.pointerTo(fn));
            return n;
        }
        src_.fail(pos, "'" + name + "' was not declared");
    }

    src_.fail(peek().pos, "expected an expression");
}

// `x`, `p.q`, `p->q->r` and nothing else - the shape [dcl.type.simple] calls
// an id-expression or a class member access, which answers with a declared
// type rather than an expression's. A single pair of parentheses around it
// changes the answer, which is why this reads tokens rather than the tree.
bool Parser::atNamePath() const {
    if (peek().kind != TokenKind::Ident) return false;
    std::size_t k = 1;
    for (;;) {
        if (peekAt(k).is(")")) return true;
        if (!peekAt(k).is(".") && !peekAt(k).is("->")) return false;
        if (peekAt(k + 1).kind != TokenKind::Ident) return false;
        k += 2;
    }
}

const Type *Parser::decltypeSpecifier() {
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");
    const bool namePath = atNamePath();

    // **A name on its own is looked up, not evaluated.** A reference variable
    // is the case that needs it: every mention of one is lowered to a
    // dereference, so the expression `r` has type T where `r` was declared
    // `T &` - and `decltype(r)` has to answer what the declaration said.
    if (namePath && peekAt(1).is(")")) {
        const std::string name = peek().text;
        const Type *declared = nullptr;
        if (const Local *l = findLocal(name)) declared = l->type;
        else if (const GlobalSym *g = findGlobal(name)) declared = g->type;
        if (declared != nullptr) {
            at_ += 2;
            return declared;
        }
    }

    ExprPtr e = expr();
    expect(")");
    const Type *t = e->type();
    if (t == nullptr)
        src_.fail(pos, "'decltype' needs an expression with a type, and this "
                       "one has none");
    if (namePath) return t;
    return isLvalue(*e) ? types_.referenceTo(t) : t;
}

// A reference is used by going through the address in its slot, and every
// use does it. What the program named is what the slot points at, so the
// parser hands back a dereference: from here on, assignment, address-of and
// member access all see an ordinary lvalue and none of them needs to know a
// reference was ever involved. This is the trade from '(bool)x' becoming
// 'x != 0' - a new thing in the language, lowered to one the backends have.
ExprPtr Parser::useReference(ExprPtr e) {
    if (!e->type()->isReference()) return e;
    const Type *referent = e->type()->referent();
    e->setType(types_.pointerTo(referent));
    ExprPtr deref(new Unary('*', std::move(e)));
    deref->setType(referent);
    return deref;
}

// What is stored in a reference is an address, so binding one is taking the
// address of the initialiser. The rules here are the whole of what makes a
// reference different from a pointer that is always dereferenced.
ExprPtr Parser::bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                              const std::string &what) {
    const Type *referent = ref->referent();
    const Type *it = init->type();

    // Binding takes an address, so the two things that have none cannot be
    // bound to directly. A const reference still may: it copies them into a
    // temporary below, which is what the standard says happens. Same rule as
    // unary '&', reached by a road that does not go through it.
    const char *noAddressBecause = nullptr;
    std::string noAddressName;
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(init.get()))
        if (m->isBitField()) {
            noAddressBecause = "a bit-field";
            noAddressName = m->name();
        }
    if (const Var *v = dynamic_cast<const Var *>(init.get()))
        if (v->noAddress()) {
            noAddressBecause = "register";
            noAddressName = v->name();
        }

    // **An rvalue reference takes exactly what an lvalue one will not.**
    // [dcl.init.ref]: `T &&` binds to a value with nowhere to live and
    // refuses an object that has somewhere - which is the whole of what it
    // says about a caller, and the reason a move can take an object apart
    // without anyone noticing.
    if (ref->isRValueReference() && isLvalue(*init))
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is an "
                       "object with an address of its own - an rvalue "
                       "reference binds only to a value that has none, so "
                       "that taking it apart harms nobody");

    // The direct binding: an addressable lvalue of exactly the type named,
    // which the reference then *is*. Everything else either makes a temporary
    // below or is refused.
    if (isLvalue(*init) && noAddressBecause == nullptr &&
        it->unqualified() == referent->unqualified()) {
        if (it->isConst() && !referent->isConst())
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference that can write "
                           "cannot bind to a const");
        ExprPtr addr(new Unary('&', std::move(init)));
        addr->setType(types_.pointerTo(referent));
        return addr;
    }

    // Anything else needs a temporary to bind to, and only a const reference
    // may have one - [dcl.init.ref]/5. A write through the other kind would
    // land in a copy nobody can read back, so the two cases are refused
    // separately: a type that does not match, and a value with no address.
    // A temporary is what an rvalue reference is *for*, so unlike a plain
    // `T &` it is allowed one without being const.
    if (!referent->isConst() && !ref->isRValueReference()) {
        if (noAddressBecause != nullptr)
            src_.fail(pos, "'" + noAddressName + "' is " + noAddressBecause +
                           ", and has no address for a reference to hold - a "
                           "'const " + referent->unqualified()->describe() +
                           " &' would take a copy of it instead");
        // In C++ a '?:' whose arms are lvalues of one type is itself an
        // lvalue, so this is a reference binding the standard allows and
        // this compiler cannot make yet. Say that, rather than the generic
        // complaint about a value with no address.
        if (dynamic_cast<const Conditional *>(init.get()) != nullptr)
            src_.fail(pos, "a '?:' is an lvalue in C++ when both arms are, and "
                           "this compiler does not build one yet - bind the "
                           "reference in an if/else instead");
        if (isLvalue(*init))
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference binds to the type "
                           "it names, and nothing is converted on the way in; a "
                           "'const " + referent->unqualified()->describe() +
                           " &' would take a converted copy");
        src_.fail(pos, what + " is '" + ref->describe() + "', and this is a "
                       "value with no address to bind to - a 'const " +
                       referent->unqualified()->describe() + " &' would take a "
                       "copy of it instead");
    }

    checkAssignable(*init, referent, pos, what);
    const Type *store = referent->unqualified();
    const Type *addrType = types_.pointerTo(referent);
    int slot = allocateFrameSlot(store);
    std::string temp = ".ref" + std::to_string(refTemps_++);

    ExprPtr target(Var::local(temp, slot));
    target->setType(store);
    ExprPtr keep(new Assign(std::move(target), convert(std::move(init), store)));
    keep->setType(store);

    ExprPtr held(Var::local(temp, slot));
    held->setType(store);
    ExprPtr addr(new Unary('&', std::move(held)));
    addr->setType(addrType);

    ExprPtr both(new Comma(std::move(keep), std::move(addr)));
    both->setType(addrType);
    return both;
}

ExprPtr Parser::objectRef(const std::string &name) {
    if (const Local *l = findLocal(name)) {
        Var *v = l->staticName.empty() ? Var::local(name, l->offset)
                                       : Var::global(l->staticName);
        v->setReadOnly(l->isConst);
        v->setNoAddress(l->isRegister);
        ExprPtr n(v);
        n->setType(l->type);
        return useReference(std::move(n));
    }
    if (const GlobalSym *g = findGlobal(name)) {
        Var *v = Var::global(name);
        v->setSymbol(g->symbol);
        v->setReadOnly(g->isConst);
        ExprPtr n(v);
        n->setType(g->type);
        return n;
    }
    return nullptr;
}

void Parser::parseArguments(std::vector<ExprPtr> &args) {
    if (consume(")")) return;
    for (;;) {
        // **`rest...` - one thing written, one argument per member.** The
        // names were made when the parameter list expanded, so this is a
        // lookup and not a substitution: whatever `rest$0` and `rest$1` are
        // now, that is what goes here.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            auto pk = packs_.find(peek().text);
            if (pk != packs_.end() && !pk->second.names.empty()) {
                at_ += 2;
                for (std::size_t i = 0; i < pk->second.names.size(); i++) {
                    ExprPtr one = objectRef(pk->second.names[i]);
                    if (one == nullptr)
                        src_.fail(peek().pos, "'" + pk->second.names[i] +
                                              "' went missing from this pack");
                    args.push_back(decay(std::move(one)));
                }
                if (consume(")")) break;
                expect(",");
                continue;
            }
            // An empty pack expands to no arguments at all.
            if (pk != packs_.end() && pk->second.types.empty()) {
                at_ += 2;
                if (consume(")")) break;
                expect(",");
                continue;
            }
        }
        args.push_back(assign());
        if (consume(")")) break;
        expect(",");
    }
}

// **Split from completeCall so that overload resolution can stand between
// them.** Choosing a function needs the arguments, and converting the
// arguments needs the function, so the two cannot happen in one pass. A call
// through a function pointer has nothing to choose and still comes here.
ExprPtr Parser::finishCall(const std::string &name, const std::string &symbol,
                           ExprPtr callee, const Type *returns,
                           const std::vector<const Type *> &params,
                           bool variadic, std::size_t pos) {
    std::vector<ExprPtr> args;
    parseArguments(args);
    return completeCall(name, symbol, std::move(callee), returns, params,
                        variadic, pos, std::move(args));
}

// The caller's half of passing a class by value: a temporary in this frame,
// the copy constructor run into it, and its address handed over. The whole
// thing is one expression - `(ctor(&tmp, arg), &tmp)` - so it needs no
// statement to sit in and works wherever a call does.
//
// The temporary belongs to the caller on the Itanium targets, which is also
// who destroys it. The Microsoft ABI puts that on the callee; see
// docs/CONFORMANCE.md, which records the difference and what it costs.
// The end of a full expression, where the temporaries it made are destroyed,
// in the reverse of the order they were made. The value has to be put
// somewhere first, because the destructors run between the expression and its
// value being used: `(r = <expr>, ~T(&tmp), r)`.
//
// Called from the places an expression becomes a statement or a condition. A
// site that forgets to call it does not lose the destructor - the temporary
// stays on the list and goes at the next full expression - so the failure
// mode is late rather than absent.
// The same end-of-full-expression rule where the expression has already
// become statements - a declaration's initialiser - so there is no value to
// carry past the destructors and they are simply appended.
void Parser::flushTemporaries(std::vector<StmtPtr> &into) {
    if (pendingTemps_.empty()) return;
    std::vector<std::pair<int, const Type *> > mine;
    mine.swap(pendingTemps_);
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].second);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].first));
        what->setType(mine[k].second);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].second));
        into.push_back(StmtPtr(new ExprStmt(
            destructorCall(std::move(at), *dtor, 0))));
    }
}

ExprPtr Parser::endFullExpression(ExprPtr e) {
    if (pendingTemps_.empty()) return e;
    std::vector<std::pair<int, const Type *> > mine;
    mine.swap(pendingTemps_);

    const Type *t = e->type();
    const bool hasValue = t != nullptr && !t->isVoid() && !t->isFunction();
    int keep = 0;
    if (hasValue) {
        keep = allocateFrameSlot(t);
        ExprPtr where(Var::local("$full", keep));
        where->setType(t);
        ExprPtr save(new Assign(std::move(where), std::move(e)));
        save->setType(t);
        e = std::move(save);
    }
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].second);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].first));
        what->setType(mine[k].second);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].second));
        ExprPtr gone = destructorCall(std::move(at), *dtor, 0);
        ExprPtr seq(new Comma(std::move(e), std::move(gone)));
        seq->setType(t);
        e = std::move(seq);
    }
    if (hasValue) {
        ExprPtr back(Var::local("$full", keep));
        back->setType(t);
        ExprPtr seq(new Comma(std::move(e), std::move(back)));
        seq->setType(t);
        e = std::move(seq);
    }
    return e;
}

ExprPtr Parser::materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                                const std::string &what,
                                std::vector<std::pair<int, const Type *> > &destroy) {
    const Type *cls = type->unqualified();
    const Signature *cc = copyConstructorOf(cls);

    // **A class that only has a destructor still goes by address on Itanium**,
    // and the copy the caller makes for it is a move of bytes rather than a
    // call - there is no copy constructor, because copying it is trivial. What
    // is not trivial is destroying it, which is why it travels this way at
    // all.
    if (cc == nullptr) {
        checkAssignable(*arg, cls, pos, what);
        const int plain = allocateFrameSlot(cls);
        const Type *to = types_.pointerTo(cls);
        if (destructorOf(cls) != nullptr)
            destroy.push_back(std::make_pair(plain, cls));

        ExprPtr slot(Var::local("$copy", plain));
        slot->setType(cls);
        ExprPtr store(new Assign(std::move(slot), std::move(arg)));
        store->setType(cls);

        ExprPtr again(Var::local("$copy", plain));
        again->setType(cls);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(to);

        ExprPtr node(new Comma(std::move(store), std::move(at)));
        node->setType(to);
        return node;
    }
    if (cc->access != Access::Public && currentClass_ != cls)
        src_.fail(pos, "'" + cls->describe() + "' is passed by value as " + what +
                       ", which copies it, and its copy constructor is " +
                       (cc->access == Access::Private ? "private" : "protected"));
    checkAssignable(*arg, cls, pos, what);

    const int tmp = allocateFrameSlot(cls);
    const Type *ptr = types_.pointerTo(cls);
    if (destructorOf(cls) != nullptr)
        destroy.push_back(std::make_pair(tmp, cls));

    // **Elision, where the argument is already one of these coming back
    // through a hidden pointer.** The call can build its result straight into
    // the temporary this argument needs, and then no copy constructor runs at
    // all - which is what clang does at -O0 and what C++11 permits.
    if (Call *made = dynamic_cast<Call *>(arg.get())) {
        if (made->type() == cls && returnsIndirectly(cls)) {
            made->setResultSlot(tmp);
            ExprPtr built(Var::local("$copy", tmp));
            built->setType(cls);
            ExprPtr at(new Unary('&', std::move(built)));
            at->setType(ptr);
            ExprPtr node(new Comma(std::move(arg), std::move(at)));
            node->setType(ptr);
            return node;
        }
    }

    functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;

    ExprPtr slot(Var::local("$copy", tmp));
    slot->setType(cls);
    ExprPtr addr(new Unary('&', std::move(slot)));
    addr->setType(ptr);

    std::vector<ExprPtr> ctorArgs;
    ctorArgs.push_back(std::move(addr));
    ctorArgs.push_back(std::move(arg));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    ps.push_back(cc->params[0]);
    ExprPtr build = completeCall(cls->tag(), cc->symbol, nullptr,
                                 types_.get(Kind::Void), ps, false, pos,
                                 std::move(ctorArgs));

    ExprPtr again(Var::local("$copy", tmp));
    again->setType(cls);
    ExprPtr result(new Unary('&', std::move(again)));
    result->setType(ptr);

    ExprPtr node(new Comma(std::move(build), std::move(result)));
    node->setType(ptr);
    return node;
}

ExprPtr Parser::completeCall(const std::string &name, const std::string &symbol,
                             ExprPtr callee, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, std::size_t pos,
                             std::vector<ExprPtr> args) {
    if (variadic ? args.size() < params.size() : args.size() != params.size())
        src_.fail(pos, "'" + name + "' takes " + (variadic ? "at least " : "") +
                       std::to_string(params.size()) + " argument(s), given " +
                       std::to_string(args.size()));

    // Temporaries this call makes for its by-value class arguments, and which
    // this call therefore has to destroy once it returns.
    std::vector<std::pair<int, const Type *> > destroy;

    for (std::size_t i = 0; i < args.size(); i++) {
        if (i >= params.size()) {
            args[i] = defaultPromote(decay(std::move(args[i])));
            continue;
        }
        std::string what = "argument " + std::to_string(i + 1) + " of '" + name + "'";
        if (params[i]->isReference()) {
            args[i] = bindReference(params[i], std::move(args[i]), pos, what);
            continue;
        }
        // **A class whose copy is a constructor call is copied by the
        // caller**, into a temporary the caller owns, and what the callee
        // receives is that temporary's address. Measured on all three
        // targets: clang and cl each emit the copy constructor at the call
        // site and then pass a pointer.
        if (passedByAddress(params[i])) {
            args[i] = materialiseCopy(params[i], std::move(args[i]), pos, what,
                                      destroy);
            continue;
        }
        args[i] = decay(std::move(args[i]));
        checkAssignable(*args[i], params[i], pos, what);
        args[i] = convert(std::move(args[i]), params[i]);
    }

    int slot = returns->isStructOrUnion() ? allocateFrameSlot(returns) : 0;
    int named = static_cast<int>(params.size());

    std::vector<int> argSlots(args.size(), 0);
    for (std::size_t i = 0; i < args.size(); i++)
        if (args[i]->type()->isStructOrUnion())
            argSlots[i] = allocateFrameSlot(args[i]->type());

    Call *call = new Call(name, std::move(callee), std::move(args), variadic,
                          slot, named, std::move(argSlots));
    call->setSymbol(symbol);
    ExprPtr n(call);
    n->setType(returns);

    // **The caller destroys the copies it made** - measured from clang, which
    // emits the destructor of the argument temporary in the caller. The
    // Microsoft ABI puts that on the callee instead; docs/CONFORMANCE.md has
    // the difference and what it costs. They are handed to the full
    // expression rather than destroyed here, because that is when the
    // standard says they go.
    if (!target_.microsoftNames())
        for (std::size_t k = 0; k < destroy.size(); k++)
            pendingTemps_.push_back(destroy[k]);

    // A call that returns a reference is an lvalue, and useReference is what
    // makes it one: the address comes back in a register and the dereference
    // around it is what the caller actually named.
    return useReference(std::move(n));
}

ExprPtr Parser::postfix() {
    ExprPtr n = primary(current_);
    for (;;) {
        std::size_t pos = peek().pos;

        if (peek().is("(") && n->type()->isFunctionPointer()) {
            at_++;
            const Type *fn = n->type()->pointee();
            std::string called = n->type()->describe();
            n = finishCall(called, called, std::move(n), fn->returns(),
                           fn->params(), fn->isVariadicFn(), pos);
            continue;
        }

        if (peek().is("[")) {
            at_++;
            ExprPtr index = expr();
            expect("]");
            ExprPtr sum = arithmetic(BinOp::Add, std::move(n), std::move(index), pos);
            if (!sum->type()->isPointer())
                src_.fail(pos, "subscript needs an array or a pointer");
            const Type *elem = sum->type()->pointee();
            ExprPtr deref(new Unary('*', std::move(sum)));
            deref->setType(elem);
            n = std::move(deref);
            continue;
        }

        if (peek().is("->")) {
            at_++;
            if (!n->type()->isPointer() || !n->type()->pointee()->isStructOrUnion())
                src_.fail(pos, "'->' needs a pointer to a struct or union, not '" +
                               n->type()->describe() + "'");
            const Type *obj = n->type()->pointee();
            ExprPtr deref(new Unary('*', std::move(n)));
            deref->setType(obj);
            n = std::move(deref);
            std::string name = expectIdent("a member name");
            if (consume("(")) { n = memberCall(std::move(n), obj, name, pos); continue; }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
                // [expr.ref] evaluates the object expression even though what
                // the whole thing names is the one shared object. Where that
                // expression is pure there is nothing to evaluate, and
                // dropping it leaves an ordinary lvalue rather than a comma -
                // which is what `b.count = 1` and `&b.count` need.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
            const Member *m = obj->findMember(name);
            if (!m) src_.fail(pos, "'" + obj->describe() + "' has no member '" + name + "'");
            checkAccessible(obj, *m, pos);
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const:
            // [expr.ref] gives the member the object's cv-qualification, and
            // without this 's.x = 2' on a const s would be a way round it.
            acc->setType(obj->isConst() ? types_.withConst(m->type) : m->type);
            n = std::move(acc);
            continue;
        }

        if (peek().is("++") || peek().is("--")) {
            bool up = peek().is("++");
            at_++;
            n = incDec(std::move(n), up, false, pos);
            continue;
        }

        if (peek().is(".")) {
            at_++;
            if (!n->type()->isStructOrUnion())
                src_.fail(pos, "'.' needs a struct or union, not '" +
                               n->type()->describe() + "'");
            const Type *obj = n->type();
            std::string name = expectIdent("a member name");
            if (consume("(")) { n = memberCall(std::move(n), obj, name, pos); continue; }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
                // [expr.ref] evaluates the object expression even though what
                // the whole thing names is the one shared object. Where that
                // expression is pure there is nothing to evaluate, and
                // dropping it leaves an ordinary lvalue rather than a comma -
                // which is what `b.count = 1` and `&b.count` need.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
            const Member *m = obj->findMember(name);
            if (!m) src_.fail(pos, "'" + obj->describe() + "' has no member '" + name + "'");
            checkAccessible(obj, *m, pos);
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const:
            // [expr.ref] gives the member the object's cv-qualification, and
            // without this 's.x = 2' on a const s would be a way round it.
            acc->setType(obj->isConst() ? types_.withConst(m->type) : m->type);
            n = std::move(acc);
            continue;
        }

        return n;
    }
}

ExprPtr Parser::unary() {
    std::size_t pos = peek().pos;

    if (consume("+")) return decay(castExpr());

    if (peek().is("++") || peek().is("--")) {
        bool inc = peek().is("++");
        at_++;
        return incDec(unary(), inc, true, pos);
    }
    if (consume("~")) {
        ExprPtr v = decay(castExpr());
        if (!v->type()->isInteger())
            src_.fail(pos, "'~' needs an integer, not '" + v->type()->describe() + "'");
        const Type *t = promote(v->type());
        ExprPtr ones(new Num(-1LL));
        ones->setType(t);
        ExprPtr n(new Binary(BinOp::BitXor, convert(std::move(v), t), std::move(ones)));
        n->setType(t);
        return n;
    }
    if (consume("!")) {
        ExprPtr v = decay(castExpr());
        requireScalar(*v, pos, "'!'");
        ExprPtr node(new Unary('!', std::move(v)));
        node->setType(types_.intType());
        return node;
    }
    if (consume("-")) {
        ExprPtr v = decay(castExpr());
        if (!v->type()->isArithmetic())
            src_.fail(pos, "unary '-' needs a number, not '" + v->type()->describe() + "'");
        const Type *t = promote(v->type());
        ExprPtr n(new Unary('-', convert(std::move(v), t)));
        n->setType(t);
        return n;
    }
    if (consume("&")) {
        ExprPtr v = castExpr();
        if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(v.get()))
            if (m->isBitField())
                src_.fail(pos, "'" + m->name() + "' is a bit-field, and a "
                               "bit-field has no address");
        if (const Var *rv = dynamic_cast<const Var *>(v.get()))
            if (rv->noAddress())
                src_.fail(pos, "'" + rv->name() + "' is register, and a register "
                               "object has no address - drop the register");
        if (dynamic_cast<const Conditional *>(v.get()) != nullptr)
            src_.fail(pos, "'?:' is not an lvalue, and its address cannot be "
                           "taken - assign it to something first");
        if (dynamic_cast<const Call *>(v.get()) != nullptr)
            src_.fail(pos, "a call is not an lvalue, and its address cannot be "
                           "taken - assign it to something first");
        const Type *of = v->type();
        ExprPtr n(new Unary('&', std::move(v)));
        n->setType(types_.pointerTo(of));
        return n;
    }
    if (consume("*")) {
        ExprPtr v = decay(castExpr());
        if (!v->type()->isPointer())
            src_.fail(pos, "'*' needs a pointer, not '" + v->type()->describe() + "'");

        if (v->type()->pointee()->isFunction()) return v;
        const Type *elem = v->type()->pointee();
        if (elem->isVoid()) src_.fail(pos, "'void *' cannot be dereferenced");
        ExprPtr n(new Unary('*', std::move(v)));
        n->setType(elem);
        return n;
    }
    if (peek().is("new")) {
        std::size_t pos = peek().pos;
        at_++;
        return newExpression(pos);
    }
    if (peek().is("delete")) {
        std::size_t pos = peek().pos;
        at_++;
        return deleteExpression(pos);
    }

    // `sizeof...(Ts)` and `sizeof...(args)` - how many, not how big. Both
    // spellings name the same pack: the parameter and the function parameter
    // made from it are one entry here.
    if (peek().is("sizeof") && peekAt(1).is("...")) {
        const std::size_t spos = peek().pos;
        at_ += 2;
        expect("(");
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'sizeof...' takes the name of a parameter "
                                  "pack");
        auto pack = packs_.find(peek().text);
        if (pack == packs_.end())
            src_.fail(peek().pos, "'" + peek().text + "' is not a parameter "
                                  "pack");
        const long long n = static_cast<long long>(pack->second.types.size());
        at_++;
        expect(")");
        (void)spos;
        ExprPtr n2(new Num(n));
        n2->setType(types_.get(target_.sizeType()));
        return n2;
    }

    if (peek().is("sizeof")) {
        at_++;
        const Type *measured = nullptr;
        if (peek().is("(") && [this] {
                std::size_t save = at_; at_++; bool t = atTypeName(); at_ = save; return t;
            }()) {
            at_++;
            StorageClass sc;
            measured = specifiers(&sc);
            measured = declarator(measured, true).type;
            expect(")");
        } else {
            ExprPtr operand = unary();
            if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(operand.get()))
                if (m->isBitField())
                    src_.fail(pos, "sizeof cannot be applied to '" + m->name() +
                                   "', which is a bit-field");
            measured = operand->type();
        }
        // **A signature that depends on its parameters through an
        // *expression* cannot be given a name.** Itanium spells a
        // specialization's return type from the pattern, and a pattern
        // holding `sizeof(T) == 4` is spelled as the expression itself -
        // `N9enable_ifIXeqstT_Li4EEiE4typeE`, measured with clang. Nothing
        // here can write that, so it is refused where it is written rather
        // than left to reach a type that has no size.
        if (patternOnly_ && (measured->kind() == Kind::TemplateParam ||
                             measured->kind() == Kind::DependentMember))
            src_.fail(pos, "'sizeof' of a template parameter in a signature is "
                           "not supported yet - the linker name would have to "
                           "spell the expression, and that is its own step");
        if (!measured->isComplete())
            src_.fail(pos, "sizeof needs a complete type");
        ExprPtr n(new Num(static_cast<long long>(measured->size(target_))));
        n->setType(types_.get(target_.sizeType()));
        return n;
    }
    return postfix();
}

// ---------------------------------------------------------------- new and delete
//
// **The four operator functions are called by name, and the names were
// measured rather than read** - `clang++ -target ... -S -O0` over a file that
// news and deletes, on all three targets. -O0 matters: at -O1 clang elides the
// allocation entirely, which it is allowed to do, and the assembly comes back
// with nothing to read.
//
//     operator new(size_t)     _Znwm    ??2@YAPEAX_K@Z
//     operator new[](size_t)   _Znam    ??_U@YAPEAX_K@Z
//     operator delete(void *)  _ZdlPv   ??3@YAXPEAX@Z
//     operator delete[](void *) _ZdaPv  ??_V@YAXPEAX@Z
//
// Darwin writes the Itanium name with a leading underscore, and the backend
// already does that to every symbol, so what is emitted here is the plain one.
//
// **These are calls to the platform's own operators, not to an allocator this
// compiler ships**, which is what makes `new` here interoperate with a `delete`
// in a translation unit built by clang. It also means allocation failure does
// what the platform does - the real operator new throws - and this compiler has
// no exceptions until rung 6. docs/CONFORMANCE.md records that.
ExprPtr Parser::runtimeCall(const char *symbol, const Type *returns,
                            std::vector<ExprPtr> args) {
    std::vector<int> argSlots(args.size(), 0);
    Call *call = new Call(symbol, nullptr, std::move(args), false, 0,
                          static_cast<int>(argSlots.size()),
                          std::move(argSlots));
    call->setSymbol(symbol);
    ExprPtr n(call);
    n->setType(returns);
    return n;
}

// **The Microsoft ABI throws from the stack, not from the heap.**
//
//     T tmp = x;
//     _CxxThrowException(&tmp, &_TI1<letter>);
//
// where Itanium asks the runtime for memory first. The exception object is an
// ordinary local here, and what carries its identity is the ThrowInfo chain -
// four objects the *backend* emits, listed on the Program so that only the
// backend which needs them sees them.
StmtPtr Parser::microsoftThrow(ExprPtr value, std::size_t pos) {
    const Type *thrown = value->type()->unqualified();
    MicrosoftThrow names;
    std::string why;
    if (!microsoftThrowNames(thrown, thrown->size(target_), &names, &why))
        src_.fail(pos, "'throw' cannot name the type of this: " + why);

    bool had = false;
    for (std::size_t i = 0; i < current_->thrown.size(); i++)
        if (current_->thrown[i] == thrown) had = true;
    if (!had) current_->thrown.push_back(thrown);

    const int slot = allocateFrameSlot(thrown);
    const std::string temp = ".ex" + std::to_string(refTemps_++);
    ExprPtr held(Var::local(temp, slot));
    held->setType(thrown);
    ExprPtr store(new Assign(std::move(held), convert(std::move(value), thrown)));
    store->setType(thrown);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    ExprPtr object(Var::local(temp, slot));
    object->setType(thrown);
    ExprPtr address(new Unary('&', std::move(object)));
    address->setType(voidPtr);
    args.push_back(std::move(address));

    Var *ti = Var::global(names.info);
    ti->setSymbol(names.info);
    ExprPtr tiRef(ti);
    tiRef->setType(types_.get(Kind::Char));
    ExprPtr tiAddr(new Unary('&', std::move(tiRef)));
    tiAddr->setType(voidPtr);
    args.push_back(std::move(tiAddr));

    ExprPtr thrower = runtimeCall("_CxxThrowException", types_.get(Kind::Void),
                                  std::move(args));
    ExprPtr whole(new Comma(std::move(store), std::move(thrower)));
    whole->setType(types_.get(Kind::Void));
    return StmtPtr(new ExprStmt(std::move(whole)));
}

// **`throw x;` is three calls and a store, and no new machinery.**
//
//     void *e = __cxa_allocate_exception(sizeof x);
//     *(T *)e = x;
//     __cxa_throw(e, &_ZTI<T>, 0);
//
// The Itanium ABI puts the object in memory the runtime owns, hands it over
// with the type that identifies it, and never returns. Written as one comma
// expression so that it is a statement wherever an expression is one.
//
// **The type_info pointer is the whole of the work.** cxx1 has no RTTI: the
// vtable's typeinfo slot is a plain zero and `typeid` is refused. For a
// *fundamental* type the object is already in the standard library and naming
// it is enough, which is why this rung starts there and refuses everything
// else by name.
StmtPtr Parser::throwStatement(ExprPtr value, std::size_t pos) {
    const Type *thrown = value->type()->unqualified();
    if (target_.microsoftNames()) return microsoftThrow(std::move(value), pos);

    std::string info, why;
    if (!itaniumTypeInfoName(thrown, &info, &why))
        src_.fail(pos, "'throw' cannot name the type of this: " + why);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int slot = allocateFrameSlot(voidPtr);
    const std::string temp = ".ex" + std::to_string(refTemps_++);

    std::vector<ExprPtr> sizeArg;
    ExprPtr howBig(new Num(static_cast<long long>(thrown->size(target_))));
    howBig->setType(types_.get(target_.sizeType()));
    sizeArg.push_back(std::move(howBig));
    ExprPtr got = runtimeCall("__cxa_allocate_exception", voidPtr,
                              std::move(sizeArg));

    ExprPtr held(Var::local(temp, slot));
    held->setType(voidPtr);
    ExprPtr save(new Assign(std::move(held), std::move(got)));
    save->setType(voidPtr);

    const Type *thrownPtr = types_.pointerTo(thrown);
    ExprPtr asT(Var::local(temp, slot));
    asT->setType(voidPtr);
    ExprPtr cast(new Cast(thrownPtr, std::move(asT)));
    cast->setType(thrownPtr);
    ExprPtr into(new Unary('*', std::move(cast)));
    into->setType(thrown);
    ExprPtr store(new Assign(std::move(into), convert(std::move(value), thrown)));
    store->setType(thrown);

    // The exception object, the type that identifies it, and the destructor
    // it does not have. A fundamental type needs none, so the third argument
    // is the null the ABI asks for there.
    std::vector<ExprPtr> throwArgs;
    ExprPtr object(Var::local(temp, slot));
    object->setType(voidPtr);
    throwArgs.push_back(std::move(object));

    Var *ti = Var::global(info);
    ti->setSymbol(info);
    ExprPtr tiRef(ti);
    tiRef->setType(types_.get(Kind::Char));
    ExprPtr tiAddr(new Unary('&', std::move(tiRef)));
    tiAddr->setType(voidPtr);
    throwArgs.push_back(std::move(tiAddr));

    ExprPtr none(new Num(0LL));
    none->setType(voidPtr);
    throwArgs.push_back(std::move(none));

    ExprPtr thrower = runtimeCall("__cxa_throw", types_.get(Kind::Void),
                                  std::move(throwArgs));

    ExprPtr first(new Comma(std::move(save), std::move(store)));
    first->setType(thrown);
    ExprPtr whole(new Comma(std::move(first), std::move(thrower)));
    whole->setType(types_.get(Kind::Void));
    return StmtPtr(new ExprStmt(std::move(whole)));
}

ExprPtr Parser::callAllocator(const char *itanium, const char *microsoft,
                              const Type *returns, ExprPtr arg,
                              std::size_t pos) {
    (void)pos;
    std::vector<ExprPtr> args;
    args.push_back(std::move(arg));
    std::vector<int> argSlots(args.size(), 0);

    Call *call = new Call(target_.microsoftNames() ? microsoft : itanium,
                          nullptr, std::move(args), false, 0, 1,
                          std::move(argSlots));
    call->setSymbol(target_.microsoftNames() ? microsoft : itanium);
    ExprPtr n(call);
    n->setType(returns);
    return n;
}

ExprPtr Parser::newExpression(std::size_t pos) {
    if (peek().is("("))
        src_.fail(peek().pos, "placement new is not supported yet - and a "
                              "parenthesised type after 'new' is read the same "
                              "way, so write 'new int' rather than 'new (int)'");

    StorageClass sc = StorageNone;
    const Type *made = specifiers(&sc);
    if (sc != StorageNone)
        src_.fail(pos, "a storage class has no meaning in a new-expression");

    // The pointer part of the new-type-id, by hand: `declarator` reads an array
    // bound with constantExpression, and the whole point of `new T[n]` is that
    // n need not be one.
    for (;;) {
        if (consume("*")) {
            made = types_.pointerTo(made);
            while (peek().is("const")) { at_++; made = types_.withConst(made); }
            continue;
        }
        break;
    }

    ExprPtr count;
    bool array = false;
    if (consume("[")) {
        array = true;
        count = expr();
        expect("]");
        if (peek().is("["))
            src_.fail(peek().pos, "an array of arrays from 'new' is not "
                                  "supported yet - only the first dimension "
                                  "may be given here");
    }

    if (!made->isComplete())
        src_.fail(pos, "'new' needs a complete type, and '" + made->describe() +
                       "' is not one here");
    if (made->isReference())
        src_.fail(pos, "'new' cannot make a reference - a reference is a name "
                       "for something that already exists");

    // The initialiser, and only the forms that need no constructor. Anything
    // else is rung 3 and is refused by name rather than half-built.
    // A class with constructors is built by calling one, here as much as on the
    // stack - the only difference is where the object is.
    const bool constructed = made->isStructOrUnion() && !made->tag().empty() &&
                             overloadsOf(constructorKey(made->tag())) != nullptr;
    std::vector<ExprPtr> ctorArgs;
    bool hasInit = false;
    ExprPtr init;
    if (peek().is("(")) {
        if (array)
            src_.fail(peek().pos, "'new T[n](...)' cannot initialise an array");
        at_++;
        hasInit = true;
        if (constructed) {
            if (!peek().is(")")) parseArguments(ctorArgs);
            else at_++;
        } else if (!consume(")")) {
            init = assign();
            if (peek().is(","))
                src_.fail(peek().pos, "more than one value in a new-expression "
                                      "needs a constructor, which is not "
                                      "supported yet");
            expect(")");
        }
    }
    if (constructed && array)
        src_.fail(pos, "'new T[n]' of a class with a constructor would have to "
                       "run it once per element - not supported yet");

    const Type *sizeT = types_.get(target_.sizeType());
    ExprPtr bytes(new Num(static_cast<long long>(made->size(target_))));
    bytes->setType(sizeT);
    if (array) {
        ExprPtr n = convert(decay(std::move(count)), sizeT);
        ExprPtr total(new Binary(BinOp::Mul, std::move(n), std::move(bytes)));
        total->setType(sizeT);
        bytes = std::move(total);
    }

    const Type *pointer = types_.pointerTo(made);
    ExprPtr raw = callAllocator(array ? "_Znam" : "_Znwm",
                                array ? "??_U@YAPEAX_K@Z" : "??2@YAPEAX_K@Z",
                                types_.pointerTo(types_.get(Kind::Void)),
                                std::move(bytes), pos);
    ExprPtr typed(new Cast(pointer, std::move(raw)));
    typed->setType(pointer);

    if (!hasInit && !constructed) return typed;

    // `new int(5)` is two things - an allocation and a store - and an
    // expression yields one value, so the pointer is kept in a temporary and
    // the comma operator sequences them. The same shape bindReference already
    // uses for a temporary, and for the same reason. A constructed object is
    // the same shape with a call where the store is.
    int slot = allocateFrameSlot(pointer);
    std::string temp = ".new" + std::to_string(newTemps_++);

    ExprPtr held(Var::local(temp, slot));
    held->setType(pointer);
    ExprPtr keep(new Assign(std::move(held), std::move(typed)));
    keep->setType(pointer);

    if (constructed) {
        const Signature &ctor = resolveOverload(constructorKey(made->tag()),
                                                ctorArgs, pos);
        std::vector<ExprPtr> all;
        ExprPtr self(Var::local(temp, slot));
        self->setType(pointer);
        all.push_back(std::move(self));
        for (std::size_t i = 0; i < ctorArgs.size(); i++)
            all.push_back(std::move(ctorArgs[i]));

        std::vector<const Type *> full;
        full.push_back(pointer);
        for (std::size_t i = 0; i < ctor.params.size(); i++)
            full.push_back(ctor.params[i]);

        ExprPtr build = completeCall(made->tag(), ctor.symbol, nullptr,
                                     types_.get(Kind::Void), full, false, pos,
                                     std::move(all));
        ExprPtr made2(new Comma(std::move(keep), std::move(build)));
        made2->setType(types_.get(Kind::Void));

        ExprPtr answer(Var::local(temp, slot));
        answer->setType(pointer);
        ExprPtr whole(new Comma(std::move(made2), std::move(answer)));
        whole->setType(pointer);
        return whole;
    }

    ExprPtr base(Var::local(temp, slot));
    base->setType(pointer);
    ExprPtr where(new Unary('*', std::move(base)));
    where->setType(made);

    // `new int()` is value-initialisation, which for these types is a zero.
    ExprPtr value;
    if (init) {
        checkAssignable(*init, made, pos, "the value in a new-expression");
        value = convert(decay(std::move(init)), made);
    } else {
        value.reset(new Num(0LL));
        value->setType(types_.intType());
        value = convert(std::move(value), made);
    }
    ExprPtr store(new Assign(std::move(where), std::move(value)));
    store->setType(made);

    ExprPtr result(Var::local(temp, slot));
    result->setType(pointer);

    ExprPtr both(new Comma(std::move(keep), std::move(store)));
    both->setType(made);
    ExprPtr all(new Comma(std::move(both), std::move(result)));
    all->setType(pointer);
    return all;
}

ExprPtr Parser::deleteExpression(std::size_t pos) {
    bool array = false;
    if (consume("[")) { expect("]"); array = true; }

    ExprPtr what = decay(unary());
    const Type *t = what->type();
    if (!t->isPointer())
        src_.fail(pos, "'delete' needs a pointer, and this is '" +
                       t->describe() + "'");
    if (t->pointee()->isVoid())
        src_.fail(pos, "'delete' of a 'void *' does not know what it is "
                       "freeing - give it the pointer's real type");

    // **The destructor runs before the memory goes back**, which is the order
    // clang emits and the only one that can work: the destructor reads the
    // object. A class with no destructor skips straight to the free.
    const Signature *dtor = destructorOf(t->pointee());

    // **A virtual destructor is reached through the vtable**, because the
    // static type is not necessarily the one that has to be destroyed. The
    // slot holds the deleting form, which destroys AND frees - so this path
    // makes one indirect call and does not call operator delete itself.
    if (dtor != nullptr && dtor->isVirtual) {
        if (array)
            src_.fail(pos, "'delete[]' of a polymorphic type is not supported "
                           "yet - the count and the dynamic type are both "
                           "needed and neither is recorded");
        const Type *cls = t->pointee()->unqualified();
        const std::vector<VSlot> &slots = vtables_[cls->tag()];
        int index = -1;
        for (std::size_t i = 0; i < slots.size(); i++) {
            const bool ms = target_.microsoftNames();
            if (slots[i].name == (ms ? "~" : "~$deleting")) { index = static_cast<int>(i); break; }
        }
        if (index < 0)
            src_.fail(pos, "'" + cls->describe() + "' has a virtual destructor "
                           "with no deleting slot");

        const bool ms = target_.microsoftNames();
        std::vector<const Type *> full;
        full.push_back(t);
        const Type *flagType = types_.get(Kind::UInt);
        if (ms) full.push_back(flagType);
        const Type *ret = ms ? types_.pointerTo(types_.get(Kind::Void))
                             : types_.get(Kind::Void);

        int slot = allocateFrameSlot(t);
        std::string temp = ".dv" + std::to_string(refTemps_++);
        ExprPtr keep(Var::local(temp, slot));
        keep->setType(t);
        ExprPtr save(new Assign(std::move(keep), std::move(what)));
        save->setType(t);

        const Type *fnType = types_.functionType(ret, full, false);
        const Type *fnPtr = types_.pointerTo(fnType);
        const Type *table = types_.pointerTo(fnPtr);

        ExprPtr load(Var::local(temp, slot));
        load->setType(t);
        ExprPtr asTable(new Cast(types_.pointerTo(table), std::move(load)));
        asTable->setType(types_.pointerTo(table));
        ExprPtr vptr(new Unary('*', std::move(asTable)));
        vptr->setType(table);
        if (index != 0) {
            ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(target_)));
            at->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
            moved->setType(table);
            vptr = std::move(moved);
        }
        ExprPtr entry(new Unary('*', std::move(vptr)));
        entry->setType(fnPtr);

        std::vector<ExprPtr> args;
        ExprPtr self(Var::local(temp, slot));
        self->setType(t);
        args.push_back(std::move(self));
        if (ms) {
            ExprPtr flag(new Num(1LL));      // 1 = free the memory too
            flag->setType(flagType);
            args.push_back(std::move(flag));
        }
        ExprPtr call = completeCall("~", std::string(), std::move(entry), ret,
                                    full, false, pos, std::move(args));
        ExprPtr both(new Comma(std::move(save), std::move(call)));
        both->setType(ret);
        return both;
    }

    if (dtor != nullptr) {
        if (array)
            src_.fail(pos, "'delete[]' of a type with a destructor needs the "
                           "count that 'new[]' recorded, and this compiler does "
                           "not write one - not supported yet");
        int slot = allocateFrameSlot(t);
        std::string temp = ".del" + std::to_string(refTemps_++);

        ExprPtr keep(Var::local(temp, slot));
        keep->setType(t);
        ExprPtr save(new Assign(std::move(keep), std::move(what)));
        save->setType(t);

        ExprPtr held(Var::local(temp, slot));
        held->setType(t);
        ExprPtr run = destructorCall(std::move(held), *dtor, pos);

        ExprPtr both(new Comma(std::move(save), std::move(run)));
        both->setType(types_.get(Kind::Void));

        ExprPtr again(Var::local(temp, slot));
        again->setType(t);
        const Type *vp = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr freed(new Cast(vp, std::move(again)));
        freed->setType(vp);
        ExprPtr release = callAllocator("_ZdlPv", "??3@YAXPEAX@Z",
                                        types_.get(Kind::Void),
                                        std::move(freed), pos);
        ExprPtr all(new Comma(std::move(both), std::move(release)));
        all->setType(types_.get(Kind::Void));
        return all;
    }

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr raw(new Cast(voidPtr, std::move(what)));
    raw->setType(voidPtr);

    return callAllocator(array ? "_ZdaPv" : "_ZdlPv",
                         array ? "??_V@YAXPEAX@Z" : "??3@YAXPEAX@Z",
                         types_.get(Kind::Void), std::move(raw), pos);
}

// A call through an object: `p.move(1, 2)`. The object's address goes in front
// of the written arguments and the declared parameters gain a matching leading
// pointer, so from here down it is an ordinary call - arity, conversions and
// the backends all see what they already knew how to handle.
const Type *Parser::findMemberOwner(const Type *cls,
                                    const std::string &name) const {
    if (cls == nullptr) return nullptr;
    const Type *c = cls->unqualified();
    if (overloadsOf(c->tag() + "::" + name) != nullptr) return c;
    const std::vector<Type::BaseSpec> &bases = c->bases();
    for (std::size_t i = 0; i < bases.size(); i++)
        if (const Type *found = findMemberOwner(bases[i].type, name)) return found;
    return nullptr;
}

ExprPtr Parser::memberCall(ExprPtr object, const Type *cls,
                           const std::string &name, std::size_t pos) {
    const Type *plain = cls->unqualified();

    // **A member function is looked for up the base chain**, unlike a data
    // member, which the layout already copied down. The two are asymmetric on
    // purpose: a member lives at an offset and can be copied, a function lives
    // under a name and cannot be without inventing a second symbol for it.
    //
    // The first class that has the name wins outright - the derived class's
    // set hides the base's rather than joining it, which is [class.member.
    // lookup] and the reason a derived `f(int)` stops `f()` from being found.
    const Type *owner = findMemberOwner(plain, name);
    if (owner == nullptr) owner = plain;
    std::string key = owner->tag() + "::" + name;

    std::vector<ExprPtr> args;
    parseArguments(args);
    const Signature &sig = resolveOverload(key, args, pos, cls);

    // Now there IS an inside, and this is where it starts to mean something:
    // a private member is reachable from another member of the same class.
    if (sig.access != Access::Public && currentClass_ != plain &&
        currentClass_ != owner) {
        const char *how = sig.access == Access::Private ? "private" : "protected";
        src_.fail(pos, "'" + name + "' is " + how + " in '" + plain->describe() +
                       "' - it can be called only from inside the class");
    }
    if (cls->isConst() && !sig.constThis)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");

    // `this` inside the base's member function is a Base *, and the base
    // subobject sits at offset 0 - so the derived address IS that pointer and
    // the conversion costs nothing at run time.
    const Type *self = owner;
    const Type *pointee = sig.constThis ? types_.withConst(self) : self;
    const Type *thisType = types_.pointerTo(pointee);

    // **`this` is the base's address, not the object's**, and those differ
    // once a class has a second base: B sits at offset 4 in C, so B's member
    // functions expect &c + 4. convert() knows how to move a pointer to a
    // base, so the address is built as a Derived * and handed to it.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(plain));
    if (owner != plain) addr = convert(std::move(addr), thisType);
    else addr->setType(thisType);

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < sig.params.size(); i++) full.push_back(sig.params[i]);

    // **A virtual call reads the slot rather than naming the function.** The
    // object's first word is the vptr; the slot is at a fixed index, the same
    // index in every class in the chain, which is what the table's ordering
    // bought. Everything below the load is an ordinary indirect call - the
    // machinery a call through a function pointer already used.
    ExprPtr callee;
    ExprPtr keepAddress;
    if (sig.isVirtual) {
        int index = -1;
        const std::vector<VSlot> &slots = vtables_[plain->tag()];
        for (std::size_t i = 0; i < slots.size(); i++) {
            if (slots[i].name != name || slots[i].constThis != sig.constThis) continue;
            if (slots[i].params.size() != sig.params.size()) continue;
            bool same = true;
            for (std::size_t k = 0; k < sig.params.size(); k++)
                if (slots[i].params[k] != sig.params[k]) { same = false; break; }
            if (same) { index = static_cast<int>(i); break; }
        }
        if (index < 0)
            src_.fail(pos, "'" + name + "' is virtual but has no vtable slot in "
                           "'" + plain->describe() + "'");

        const Type *fnType = types_.functionType(sig.returns, full, sig.variadic);
        const Type *fnPtr = types_.pointerTo(fnType);
        const Type *table = types_.pointerTo(fnPtr);       // what the vptr is

        // **The address is needed twice** - once to read the vptr out of the
        // object, once as the `this` argument - and an expression is used up
        // when it is moved. So it goes into a slot first and both readers name
        // that, which is the shape bindReference and `new` already use.
        int slot = allocateFrameSlot(thisType);
        std::string temp = ".vc" + std::to_string(refTemps_++);

        ExprPtr held(Var::local(temp, slot));
        held->setType(thisType);
        keepAddress.reset(new Assign(std::move(held), std::move(addr)));
        keepAddress->setType(thisType);

        ExprPtr again(Var::local(temp, slot));
        again->setType(thisType);
        addr.reset(Var::local(temp, slot));
        addr->setType(thisType);

        ExprPtr forLoad(new Cast(types_.pointerTo(table), std::move(again)));
        forLoad->setType(types_.pointerTo(table));
        ExprPtr vptr(new Unary('*', std::move(forLoad)));
        vptr->setType(table);

        if (index != 0) {
            // Bytes again, and for the same reason as the header skip in the
            // constructor: a hand-built Add is not scaled by the pointee.
            ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(target_)));
            at->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
            moved->setType(table);
            vptr = std::move(moved);
        }
        ExprPtr entry(new Unary('*', std::move(vptr)));
        entry->setType(fnPtr);
        callee = std::move(entry);
    }

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));

    ExprPtr call = completeCall(name, sig.symbol, std::move(callee), sig.returns,
                                full, sig.variadic, pos, std::move(all));
    if (keepAddress == nullptr) return call;

    // The address is saved, then the call reads it - in that order, which the
    // comma operator is exactly for.
    const Type *result = call->type();
    ExprPtr both(new Comma(std::move(keepAddress), std::move(call)));
    both->setType(result);
    return both;
}

// [class.access]: a member that is not public may be named only from inside the
// class. There is no inside yet - member functions are the next step of this
// rung - so from here every non-public member is out of reach, which is exactly
// what a class with private data and no member functions means.
void Parser::checkAccessible(const Type *object, const Member &m,
                             std::size_t pos) const {
    if (m.access == Access::Public) return;
    if (currentClass_ != nullptr && currentClass_ == object->unqualified()) return;
    const char *how = m.access == Access::Private ? "private" : "protected";
    src_.fail(pos, "'" + m.name + "' is " + how + " in '" + object->describe() +
                   "' - it can be named only from inside the class, and this "
                   "is outside it");
}

