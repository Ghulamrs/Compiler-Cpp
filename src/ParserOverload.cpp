// The parser: conversions and overload resolution.
//
// The implicit conversion sequences of [conv], ranked as [over.ics] ranks
// them, and the tournament between candidates that ranking decides. Also the
// assignability check, which asks the same questions for a different reason.
#include "Parser.h"
#include "ParserInternal.h"
#include "Mangle.h"
#include "Source.h"

#include <climits>
#include <cstring>

const Type *Parser::unsignedVersion(const Type *t) const {
    switch (t->kind()) {
    case Kind::Int:      return types_.get(Kind::UInt);
    case Kind::Long:     return types_.get(Kind::ULong);
    case Kind::LongLong: return types_.get(Kind::ULongLong);
    default:             return t;
    }
}

const Type *Parser::promote(const Type *t) const {
    if (t->isInteger() && t->rank() < types_.intType()->rank())
        return types_.intType();
    return t;
}

const Type *Parser::usualArithmetic(const Type *a, const Type *b) const {
    if (a->kind() == Kind::LongDouble || b->kind() == Kind::LongDouble)
        return types_.get(Kind::LongDouble);
    if (a->kind() == Kind::Double || b->kind() == Kind::Double)
        return types_.doubleType();
    if (a->kind() == Kind::Float || b->kind() == Kind::Float)
        return types_.get(Kind::Float);

    a = promote(a);
    b = promote(b);
    if (a == b) return a;

    bool as = a->isSigned(target_), bs = b->isSigned(target_);
    const Type *hi = a->rank() >= b->rank() ? a : b;
    if (as == bs) return hi;

    const Type *uns = as ? b : a;
    const Type *sig = as ? a : b;
    if (uns->rank() >= sig->rank()) return uns;
    if (sig->size(target_) > uns->size(target_)) return sig;
    return unsignedVersion(sig);
}

// Defined below, beside the conversion rules it belongs with.
static int publicBaseOffset(const Type *derived, const Type *base);

ExprPtr Parser::convert(ExprPtr e, const Type *to) const {
    if (e->type() == to) return e;

    // **Derived * to Base * moves the value when the base is not the first
    // one.** A is at 0 and needs nothing; B is at 4 and the pointer has to be
    // walked forward by four. The null check is not caution - [conv.ptr] says
    // a null pointer converts to a null pointer, and `(char *)0 + 4` is not
    // null.
    if (to->isPointer() && e->type()->isPointer() &&
        to->pointee()->isStructOrUnion() && e->type()->pointee()->isStructOrUnion()) {
        const int off = publicBaseOffset(e->type()->pointee(), to->pointee());
        if (off > 0) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);

            int slot = const_cast<Parser *>(this)->allocateFrameSlot(chars);
            std::string temp = ".bp" + std::to_string(const_cast<Parser *>(this)->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr test(Var::local(temp, slot));
            test->setType(chars);
            ExprPtr shift(Var::local(temp, slot));
            shift->setType(chars);
            ExprPtr by(new Num(static_cast<long long>(off)));
            by->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(shift), std::move(by)));
            moved->setType(chars);
            ExprPtr zero(new Num(0LL));
            zero->setType(chars);
            ExprPtr pick(new Conditional(std::move(test), std::move(moved),
                                         std::move(zero)));
            pick->setType(chars);

            ExprPtr both(new Comma(std::move(save), std::move(pick)));
            both->setType(chars);
            ExprPtr out(new Cast(to, std::move(both)));
            out->setType(to);
            return out;
        }
    }

    // A conversion to bool is not a narrowing. [conv.bool] says every non-zero
    // value becomes true, so (bool)256 is true where (char)256 is 0 - the two
    // cannot share a code path. It is lowered here to a comparison against
    // zero, an operation all three backends already have, rather than taught
    // to each of them as a new kind of cast.
    if (to->isBool() && !e->type()->isBool() && e->type()->isScalar()) {
        const Type *from = e->type();
        ExprPtr zero;
        if (from->isFloating()) {
            zero.reset(new Num(static_cast<long double>(0)));
            zero->setType(from);
        } else {
            ExprPtr n(new Num(static_cast<long long>(0)));
            n->setType(types_.intType());
            zero = convert(std::move(n), from);
        }
        ExprPtr test(new Binary(BinOp::Ne, std::move(e), std::move(zero)));
        test->setType(to);
        return test;
    }

    return ExprPtr(new Cast(to, std::move(e)));
}

ExprPtr Parser::decay(ExprPtr e) {
    if (!e->type()->isArray()) return e;
    const Type *to = types_.pointerTo(e->type()->pointee());
    return ExprPtr(new Cast(to, std::move(e)));
}

void Parser::requireScalar(const Expr &e, std::size_t pos, const char *what) {
    if (!e.type()->isScalar())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       e.type()->describe() + "'");
}

// A string literal reaches here wrapped in the Cast that decayed it from an
// array, so the literal has to be looked for underneath.
static bool isStringLiteral(const Expr &e) {
    if (dynamic_cast<const StrLit *>(&e) != nullptr) return true;
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return isStringLiteral(c->value());
    return false;
}

// [conv.qual]. A pointer may gain const on its way in and may never lose it,
// and const gained below the first level only counts if every level above it
// is const too - which is why 'char **' does not become 'const char **' but
// does become 'const char * const *'. Without that last rule a program could
// store a pointer-to-const into the writable pointer at the bottom and write
// through it, with nothing along the way having said no.
// Is `base` a base class of `derived`, publicly, at any depth? The conversion
// this permits costs nothing at run time - a base subobject sits at offset 0 -
// but it has to be allowed by the type system before a Derived * can be handed
// to anything taking a Base *.
//
// Only through public inheritance: a private base is an implementation detail
// and [conv.ptr] does not convert to it from outside.
// How far into a `derived` object its `base` subobject sits, or -1 when base
// is not a public base of it at all. **Walks every base, not just the first**,
// which is what multiple inheritance needs: A is at 0 and B is at 4, and a
// pointer to the second is the object's address plus that four.
static int publicBaseOffset(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    const Type *b = base->unqualified();
    if (d == b) return 0;
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (bases[i].access != Access::Public) continue;
        int deeper = publicBaseOffset(bases[i].type, b);
        if (deeper >= 0) return bases[i].offset + deeper;
    }
    return -1;
}

static bool publiclyDerivedFrom(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return false;
    if (derived->unqualified() == base->unqualified()) return false;
    return publicBaseOffset(derived, base) >= 0;
}

static bool qualificationConvertible(const Type *from, const Type *to) {
    bool prefixConst = true;
    for (;;) {
        if (from->unqualified() == to->unqualified()) return true;
        if (!from->isPointer() || !to->isPointer()) return false;
        from = from->pointee();
        to = to->pointee();
        if (from->isConst() && !to->isConst()) return false;
        if (!from->isConst() && to->isConst() && !prefixConst) return false;
        prefixConst = prefixConst && to->isConst();
    }
}

// ------------------------------------------------------------------ overloading
//
// What follows is [over.match] reduced to what rung 2 needs, and the reduction
// is deliberate: an implicit conversion sequence is ranked, the best viable
// function is the one no other beats, and anything this cannot rank is not
// viable rather than guessed at. A wrong overload compiles and runs and gives
// the wrong answer, which is the one outcome worth refusing loudly.

const Type *Parser::decayedType(const Type *t) {
    if (t->isArray()) return types_.pointerTo(t->pointee());
    if (t->isFunction()) return types_.pointerTo(t);
    return t;
}

// The integral and floating promotions, [conv.prom] and [conv.fpprom], and
// only those - every other arithmetic pairing is a conversion, which ranks
// below. This is what makes f(int) beat f(double) for a char argument.
static bool isPromotion(const Type *from, const Type *to) {
    if (to->kind() == Kind::Int) {
        switch (from->kind()) {
            case Kind::Bool: case Kind::Char: case Kind::SChar: case Kind::UChar:
            case Kind::Short: case Kind::UShort:
                return true;
            default:
                return false;
        }
    }
    return to->kind() == Kind::Double && from->kind() == Kind::Float;
}
Parser::Rank Parser::rankArgument(const Expr &arg, const Type *param) {
    const Type *given = arg.type();

    // A reference parameter binds or it does not; there is no conversion to
    // rank. The referent types have to be the same one, and a non-const
    // reference cannot bind a const object - that is not a worse match, it is
    // not a match. Anything more (a const reference taking a temporary from a
    // converted value) is a rung of its own and is left non-viable rather than
    // half-ranked.
    if (param->isReference()) {
        const Type *want = param->pointee();
        if (want->unqualified() != given->unqualified()) return Rank::None;
        if (!want->isConst() && given->isConst()) return Rank::None;

        // **Which reference will take this argument is a question about the
        // argument, not about a conversion.** An rvalue reference is not
        // viable for an object that has an address; and where both are
        // viable, for a value that has none, it is the better match - which
        // is what makes `f(T &&)` win over `f(const T &)` for a temporary
        // and is the whole of how a move is chosen.
        if (param->isRValueReference() && isLvalue(arg)) return Rank::None;
        if (param->isRValueReference()) return Rank::Identity;
        if (!isLvalue(arg)) return Rank::Qualification;

        return want->isConst() && !given->isConst() ? Rank::Qualification
                                                    : Rank::Identity;
    }

    const Type *from = decayedType(given);
    const Type *to = param;

    if (from == to) return Rank::Identity;
    // Top-level const on the parameter is not part of its type for this
    // purpose: void f(int) and void f(const int) are one function, and an
    // argument matches both the same way.
    if (from->unqualified() == to->unqualified()) return Rank::Identity;

    if (from->isArithmetic() && to->isArithmetic())
        return isPromotion(from, to) ? Rank::Promotion : Rank::Conversion;

    if (to->isPointer() && from->isPointer()) {
        // A qualification conversion - char * to const char * - is an Exact
        // Match, so it still beats a promotion. It loses to the identity
        // conversion alone, which is the whole reason the two are separate.
        if (qualificationConvertible(from, to)) return Rank::Qualification;
        // Derived * to Base * is a pointer conversion, which ranks below a
        // promotion - so f(Base *) loses to f(Derived *) for a Derived *,
        // which is what [over.ics.rank] asks for.
        if (publiclyDerivedFrom(from->pointee(), to->pointee()) &&
            (to->pointee()->isConst() || !from->pointee()->isConst()))
            return Rank::Conversion;
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            from->pointee()->isConst())
            return Rank::None;
        if (to->pointee()->isVoid() || from->pointee()->isVoid())
            return Rank::Conversion;
        return Rank::None;
    }

    if (to->isPointer() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    return Rank::None;
}

std::string Parser::describeSignature(const Signature &f) {
    std::string out = f.name + "(";
    for (std::size_t i = 0; i < f.params.size(); i++) {
        if (i > 0) out += ", ";
        out += f.params[i]->describe();
    }
    if (f.variadic) out += f.params.empty() ? "..." : ", ...";
    return out + ")";
}

// The best viable function, or a refusal naming every candidate. "Best" is
// [over.match.best] exactly: F beats G when it is no worse on every argument
// and better on at least one. Two functions that each win an argument beat
// each other, which is what an ambiguity IS - it is not a tie to be broken by
// declaration order, and breaking it that way would compile a program whose
// meaning depends on the order of its own prototypes.
// One candidate beats another when no conversion is worse and at least one is
// better. **And, all conversions being equal, when it is not a
// specialization** - [over.match.best]. That last line is not a tiebreak of
// convenience: deduction makes twice<int> match `twice(1)` exactly, and so
// does an ordinary `int twice(int)`, so without it every such call is
// ambiguous.
bool Parser::betterCandidate(const std::vector<Rank> &a,
                             const std::vector<Rank> &b,
                             const Signature &fa, const Signature &fb) const {
    bool better = false, worse = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        if (a[i] < b[i]) better = true;
        if (a[i] > b[i]) worse = true;
    }
    if (better && worse) return false;
    if (better) return true;
    if (worse) return false;
    return !fa.fromTemplate && fb.fromTemplate;
}

const Parser::Signature &Parser::resolveOverload(const std::string &name,
                                                 const std::vector<ExprPtr> &args,
                                                 std::size_t pos,
                                                 const Type *object) {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr) {
        // **`C(...)` where C is a class** is a temporary, not a call to a
        // function nobody declared, and saying "no prototype" sends the
        // reader looking for a declaration that was never meant to exist.
        // Worth intercepting by name now that passing a class by value copies
        // it, which is exactly when somebody writes this.
        if (const Type *cls = findTypedef(name))
            if (cls->isStructOrUnion())
                src_.fail(pos, "'" + name + "(...)' makes a temporary of type '" +
                               cls->describe() + "', which is not supported yet - "
                               "name a variable of that type and use that");
        src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
    }

    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    // Set when the only thing that stopped a candidate was the constness of
    // the object, so that "no function takes these arguments" can be replaced
    // by the message that says what actually went wrong.
    bool droppedForConst = false;

    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.variadic ? args.size() < f.params.size()
                       : args.size() != f.params.size()) continue;

        // **The implicit object parameter goes first**, and ranking it is what
        // separates `get()` from `get() const`. Binding it is a reference
        // binding like any other: an exact match where the constness agrees, a
        // qualification conversion where a const member is called on a
        // non-const object - which is why the non-const one wins there - and
        // no match at all the other way round.
        std::vector<Rank> r;
        if (object != nullptr) {
            if (object->isConst() && !f.constThis) { droppedForConst = true; continue; }
            r.push_back(object->isConst() == f.constThis ? Rank::Identity
                                                         : Rank::Qualification);
        }

        const std::size_t first = r.size();
        r.resize(first + args.size(), Rank::Ellipsis);
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            if (i >= f.params.size()) continue;      // reached by the ellipsis
            r[first + i] = rankArgument(*args[i], f.params[i]);
            if (r[first + i] == Rank::None) ok = false;
        }
        if (!ok) continue;
        viable.push_back((*set)[k]);
        ranks.push_back(r);
    }

    if (viable.empty() && droppedForConst)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(functions_[(*set)[k]]);
        src_.fail(pos, why);
    }
    if (viable.size() == 1) {
        functions_[viable[0]].used = true;
        return functions_[viable[0]];
    }

    std::size_t best = 0;
    for (std::size_t k = 1; k < viable.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            functions_[viable[k]], functions_[viable[best]]))
            best = k;
    for (std::size_t k = 0; k < viable.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             functions_[viable[best]], functions_[viable[k]])) {
            std::string why = "this call to '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < viable.size(); j++)
                why += "\n    candidate: " + describeSignature(functions_[viable[j]]);
            src_.fail(pos, why);
        }
    }
    functions_[viable[best]].used = true;
    return functions_[viable[best]];
}

void Parser::checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                             const std::string &what) const {
    const Type *ft = from.type();

    if (ft == to) return;

    // Copying ignores the const at the top: [dcl.init]/2 strips it from the
    // destination, and a const source is read rather than moved. Without this
    // 'const S b = a;' would be refused for a struct, where the arithmetic
    // rule below already lets 'const int b = a;' through.
    if (ft->unqualified() == to->unqualified()) return;

    if (ft->isArithmetic() && to->isArithmetic()) return;

    auto refuse = [&](const char *tail) {
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" +
                       ft->describe() + "'" + tail);
    };

    if (to->isPointer() && ft->isPointer()) {
        if (qualificationConvertible(ft, to)) return;
        // Derived * converts to Base *, the base being at offset 0, so the
        // value is unchanged and only the type moves.
        if (publiclyDerivedFrom(ft->pointee(), to->pointee()) &&
            (to->pointee()->isConst() || !ft->pointee()->isConst()))
            return;
        // An implicit conversion through void * is C's rule, kept here and
        // recorded in docs/CONFORMANCE.md - but it must not become the way
        // round const that the rule above just closed.
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            ft->pointee()->isConst())
            refuse(" - 'void *' would drop the const; 'const void *' keeps it");
        if (to->pointee()->isVoid() || ft->pointee()->isVoid()) return;
        // The commonest way to meet this rule is a C program handing a
        // string literal to a 'char *', so it is worth saying which rule
        // stopped it rather than leaving the reader to work back from const.
        if (isStringLiteral(from))
            refuse(" - a string literal is an array of const char in C++11, "
                   "and does not convert to a writable pointer");
        if (ft->pointee()->unqualified() == to->pointee()->unqualified())
            refuse(" - the const would be dropped, and then the thing it "
                   "protects could be written through");
        refuse(" - a cast says you meant it");
    }
    if (to->isPointer() && ft->isInteger()) {
        if (isNullConstant(from)) return;
        refuse(" - only the constant 0 becomes a pointer on its own");
    }
    if (to->isArithmetic() && ft->isPointer())
        refuse(" - a pointer is not a number here, though a cast makes it one");

    refuse("");
}

