// The parser: initialisers. Braced initialisers flattened onto an object's
// bytes, the run-time stores a local needs against the static image a global
// gets, and the integer, floating and address folding that chooses between them.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

Parser::Init Parser::parseInitialiser() {
    Init in;
    in.pos = peek().pos;
    if (consume("{")) {
        in.isList = true;
        if (peek().is("}"))
            src_.fail(in.pos, "an initialiser list needs at least one value");
        for (;;) {
            in.items.push_back(parseInitialiser());
            if (consume("}")) break;
            expect(",");
            if (consume("}")) break;
        }
        return in;
    }
    in.value = assign();
    return in;
}

const StrLit *Parser::stringInitialiser(const Init &in, const Type *type) {
    if (in.isList || !type->isArray()) return nullptr;
    const StrLit *s = dynamic_cast<const StrLit *>(in.value.get());
    if (s == nullptr) return nullptr;

    Kind want = type->pointee()->kind();
    Kind have = s->type()->pointee()->kind();
    bool wantNarrow = (want == Kind::Char || want == Kind::SChar || want == Kind::UChar);
    bool haveNarrow = (have == Kind::Char || have == Kind::SChar || have == Kind::UChar);
    if (wantNarrow != haveNarrow) return nullptr;
    if (!wantNarrow && want != have) return nullptr;
    return s;
}

void Parser::skipInit(const Type *type, InitCursor &c) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList)                        { c.at++; return; }
    if (stringInitialiser(item, type))      { c.at++; return; }

    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length() && !c.done(); i++) skipInit(elem, c);
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        std::size_t count = type->kind() == Kind::Union
                          ? (members.empty() ? std::size_t(0) : std::size_t(1))
                          : members.size();
        for (std::size_t i = 0; i < count && !c.done(); i++) {
            if (members[i].name.empty()) continue;
            skipInit(members[i].type, c);
        }
        return;
    }
    c.at++;
}

long long Parser::inferredLength(const Init &in, const Type *element, std::size_t pos) {
    if (const StrLit *s = stringInitialiser(in, types_.arrayOf(element, 1)))
        return static_cast<long long>(s->text().size()) + 1;
    if (!in.isList)
        src_.fail(pos, "an array with no length needs a braced initialiser to "
                       "count, or a string to measure");

    if (element->isArray() || element->isStructOrUnion()) {
        InitCursor c{ const_cast<std::vector<Init> *>(&in.items), 0 };
        long long rows = 0;
        while (!c.done()) {
            std::size_t before = c.at;
            skipInit(element, c);
            if (c.at == before) break;
            rows++;
        }
        return rows;
    }
    return static_cast<long long>(in.items.size());
}

ExprPtr Parser::targetFor(const std::string &name,
                          const std::vector<InitStep> &path) {
    ExprPtr e = objectRef(name);
    for (const InitStep &s : path) {
        if (s.member != nullptr) {
            const Member *m = s.member;
            ExprPtr acc(new MemberAccess(std::move(e), m->name, m->offset,
                                         m->width, m->bitOffset));
            acc->setType(m->type);
            e = std::move(acc);
        } else {
            const Type *elem = e->type()->pointee();
            ExprPtr index(new Num(s.index));
            index->setType(types_.intType());
            ExprPtr sum = pointerAdd(decay(std::move(e)), std::move(index));
            ExprPtr deref(new Unary('*', std::move(sum)));
            deref->setType(elem);
            e = std::move(deref);
        }
    }
    return e;
}

void Parser::initStore(const std::string &name, std::vector<InitStep> &path,
                       ExprPtr value, std::size_t pos,
                       std::vector<StmtPtr> &out) {
    ExprPtr target = targetFor(name, path);
    const Type *to = target->type();
    checkAssignable(*value, to, pos, "'" + name + "'");
    ExprPtr a(new Assign(std::move(target), convert(std::move(value), to)));
    a->setType(to);
    out.push_back(StmtPtr(new ExprStmt(std::move(a))));
}

void Parser::initZero(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, std::size_t pos,
                      std::vector<StmtPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            initZero(name, path, elem, pos, out);
            path.pop_back();
        }
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        std::size_t count = type->kind() == Kind::Union
                          ? (members.empty() ? std::size_t(0) : std::size_t(1))
                          : members.size();
        for (std::size_t i = 0; i < count; i++) {
            if (members[i].name.empty()) continue;
            path.push_back(InitStep{ &members[i], 0 });
            initZero(name, path, members[i].type, pos, out);
            path.pop_back();
        }
        return;
    }
    ExprPtr z;
    if (type->isFloating()) { z.reset(new Num(0.0L)); z->setType(types_.doubleType()); }
    else                    { z.reset(new Num(0LL));  z->setType(types_.intType()); }
    initStore(name, path, std::move(z), pos, out);
}

void Parser::emitString(const std::string &name, std::vector<InitStep> &path,
                        const Type *type, const StrLit *s, std::size_t pos,
                        std::vector<StmtPtr> &out) {
    long long len = type->length();
    const std::string &text = s->text();
    if (static_cast<long long>(text.size()) > len)
        src_.fail(pos, "'" + name + "' holds " + std::to_string(len) +
                       " characters and the string has " +
                       std::to_string(text.size()));
    for (long long i = 0; i < len; i++) {
        path.push_back(InitStep{ nullptr, i });
        long long ch = i < static_cast<long long>(text.size())
                ? static_cast<long long>(static_cast<unsigned char>(
                      text[static_cast<std::size_t>(i)]))
                : 0L;
        ExprPtr c(new Num(ch));
        c->setType(types_.intType());
        initStore(name, path, std::move(c), pos, out);
        path.pop_back();
    }
}

void Parser::emitFill(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, InitCursor &c,
                      std::vector<StmtPtr> &out) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList) {
        c.at++;
        emitInit(name, path, type, item, out);
        return;
    }
    if (const StrLit *s = stringInitialiser(item, type)) {
        c.at++;
        emitString(name, path, type, s, item.pos, out);
        return;
    }
    if (type->isArray() || type->isStructOrUnion()) {
        emitAggregate(name, path, type, c, item.pos, out);
        return;
    }

    c.at++;
    checkNarrowing(type, *item.value, item.pos, "'" + name + "'");
    initStore(name, path, decay(std::move(item.value)), item.pos, out);
}

void Parser::emitAggregate(const std::string &name, std::vector<InitStep> &path,
                           const Type *type, InitCursor &c, std::size_t pos,
                           std::vector<StmtPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            if (c.done()) initZero(name, path, elem, pos, out);
            else          emitFill(name, path, elem, c, out);
            path.pop_back();
        }
        return;
    }
    const std::vector<Member> &members = type->members();
    std::size_t count = type->kind() == Kind::Union
                      ? (members.empty() ? std::size_t(0) : std::size_t(1))
                      : members.size();
    for (std::size_t i = 0; i < count; i++) {
        if (members[i].name.empty()) continue;
        path.push_back(InitStep{ &members[i], 0 });
        if (c.done()) initZero(name, path, members[i].type, pos, out);
        else          emitFill(name, path, members[i].type, c, out);
        path.pop_back();
    }
}

void Parser::emitInit(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, Init &in, std::vector<StmtPtr> &out) {
    if (const StrLit *s = stringInitialiser(in, type)) {
        emitString(name, path, type, s, in.pos, out);
        return;
    }

    if (type->isArray()) {
        if (!in.isList)
            src_.fail(in.pos, "'" + name + "' is an array and needs a braced "
                              "initialiser");
    } else if (type->isStructOrUnion()) {

        if (!in.isList) {
            initStore(name, path, decay(std::move(in.value)), in.pos, out);
            return;
        }
    } else {
        if (!in.isList) {
            initStore(name, path, decay(std::move(in.value)), in.pos, out);
            return;
        }
        if (in.items.size() != 1)
            src_.fail(in.pos, "'" + name + "' is not an aggregate and takes one "
                              "value");
        if (!in.items[0].isList)
            checkNarrowing(type, *in.items[0].value, in.items[0].pos,
                           "'" + name + "'");
        emitInit(name, path, type, in.items[0], out);
        return;
    }

    InitCursor c{ &in.items, 0 };
    emitAggregate(name, path, type, c, in.pos, out);
    if (!c.done())
        src_.fail(c.cur().pos, "'" + name + "' is full, and there are " +
                               std::to_string(in.items.size() - c.at) +
                               " more initialiser(s) after this one");
}

static long double inType(const Type *t, const Target &target, long double v) {
    if (t->kind() == Kind::Float) return static_cast<float>(v);
    if (t->kind() == Kind::Double ||
        (t->kind() == Kind::LongDouble && !t->isX87(target)))
        return static_cast<double>(v);
    return v;
}

// **Every value below is carried as a `double`, exactly, or the fold says so.**
// A host `long double` is 64 bits of significand on the Linux box and 53 on the
// Mac, so the fold works in double and flags `past53` and `x87Rounded`.
static bool foldDouble(const Expr &e, const Target &target, long double *out,
                       bool *past53, bool *x87Rounded) {
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) {
            *out = inType(n->type(), target, n->dvalue());
        } else {
            unsigned long long a = n->value() < 0
                ? 0ULL - static_cast<unsigned long long>(n->value())
                : static_cast<unsigned long long>(n->value());
            while (a != 0 && (a & 1) == 0) a >>= 1;
            if (a >= (1ULL << 53)) *past53 = true;
            *out = static_cast<double>(n->value());
        }
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (!foldDouble(c->value(), target, out, past53, x87Rounded))
            return false;

        const Type *ct = c->type();
        if (ct->kind() == Kind::Float)       *out = static_cast<float>(*out);
        else if (ct->kind() == Kind::Double) *out = static_cast<double>(*out);
        else if (ct->kind() == Kind::LongDouble && !ct->isX87(target))
                                             *out = static_cast<double>(*out);
        else if (!ct->isFloating()) {
            if (ct->isSigned(target))
                *out = static_cast<long double>(
                           static_cast<long long>(*out));
            else
                *out = static_cast<long double>(
                           static_cast<unsigned long long>(*out));
        }
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '-' &&
            foldDouble(u->operand(), target, out, past53, x87Rounded)) {
            *out = -*out;
            return true;
        }
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long double l, r;
        if (!foldDouble(b->lhs(), target, &l, past53, x87Rounded) ||
            !foldDouble(b->rhs(), target, &r, past53, x87Rounded))
            return false;

        Kind bk = b->type()->kind();
        bool asDouble = bk == Kind::Double ||
                        (bk == Kind::LongDouble && !b->type()->isX87(target));
        if (bk == Kind::Float) {
            float fl = static_cast<float>(l), fr = static_cast<float>(r);
            switch (b->op()) {
            case BinOp::Add: *out = fl + fr; return true;
            case BinOp::Sub: *out = fl - fr; return true;
            case BinOp::Mul: *out = fl * fr; return true;
            case BinOp::Div: if (fr == 0) return false;
                             *out = fl / fr; return true;
            default: return false;
            }
        }
        if (asDouble) {
            double dl = static_cast<double>(l), dr = static_cast<double>(r);
            switch (b->op()) {
            case BinOp::Add: *out = dl + dr; return true;
            case BinOp::Sub: *out = dl - dr; return true;
            case BinOp::Mul: *out = dl * dr; return true;
            case BinOp::Div: if (dr == 0) return false;
                             *out = dl / dr; return true;
            default: return false;
            }
        }
        // The x87 lane. The operands are exact doubles by construction, so the
        // one question is whether this result still fits one - unless an integer
        // past 53 bits fed in, where the only honest answer is the flag.
        if (*past53) *x87Rounded = true;
        const double dl = static_cast<double>(l);
        const double dr = static_cast<double>(r);
        double sum;
        bool fits;
        switch (b->op()) {
        case BinOp::Add: {
            sum = dl + dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (dr - bb) == 0.0;
            break;
        }
        case BinOp::Sub: {
            sum = dl - dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (-dr - bb) == 0.0;
            break;
        }
        case BinOp::Mul:
            sum = dl * dr;
            fits = std::fma(dl, dr, -sum) == 0.0;
            break;
        case BinOp::Div:
            if (dr == 0) return false;
            sum = dl / dr;
            fits = std::fma(sum, dr, -dl) == 0.0;
            break;
        default: return false;
        }
        if (sum != 0.0 && sum > -2.2250738585072014e-308 &&
                          sum <  2.2250738585072014e-308)
            fits = false;
        if (!fits) *x87Rounded = true;
        *out = sum;
        return true;
    }
    return false;
}

// Whether every value of `from` is a value of `to` - the question
// [dcl.init.list]/7 asks about an integer source that is not a constant.
static bool holdsEvery(const Type *from, const Type *to, const Target &target) {
    if (from->isBool()) return true;
    if (to->isBool()) return false;
    const bool fromUnsigned = !from->isSigned(target);
    const bool toUnsigned = !to->isSigned(target);
    const int fs = from->size(target), ts = to->size(target);
    if (fromUnsigned == toUnsigned) return ts >= fs;
    if (toUnsigned) return false;
    return ts > fs;
}

// Whether an integer survives a trip through the floating type F - the question
// the same paragraph asks of a constant going to `float` or `double`. The bounds
// guard the way back: at or beyond 2^63 there is no integer to come back to.
template <typename F>
static bool roundTrips(long long v, bool isUnsigned) {
    if (isUnsigned) {
        const unsigned long long u = static_cast<unsigned long long>(v);
        const F f = static_cast<F>(u);
        if (f >= static_cast<F>(18446744073709551616.0L)) return false;
        return static_cast<unsigned long long>(f) == u;
    }
    const F f = static_cast<F>(v);
    if (f >= static_cast<F>(9223372036854775808.0L) ||
        f < -static_cast<F>(9223372036854775808.0L)) return false;
    return static_cast<long long>(f) == v;
}

static int floatingRank(const Type *t) {
    if (t->kind() == Kind::Float) return 0;
    if (t->kind() == Kind::Double) return 1;
    return 2;
}

// **[dcl.init.list]/7: a braced initialiser does not narrow.** `char c = {300}`
// gave 44 here without a word. The four conversions, and each "unless a constant
// whose value survives", were measured against clang over forty shapes.
void Parser::checkNarrowing(const Type *to, const Expr &value, std::size_t pos,
                            const std::string &what) {
    const Type *from = value.type();
    if (to == nullptr || from == nullptr) return;
    if (!to->isArithmetic() || !from->isArithmetic()) return;

    const std::string target = "'" + to->describe() + "'" +
                               (what.empty() ? std::string() : " for " + what);
    const std::string fromName = "'" + from->describe() + "'";
    const std::string cannotHoldEvery =
        "a value of type " + fromName + " cannot be narrowed to " + target +
        " in a braced initialiser - '" + to->describe() + "' cannot hold "
        "every " + fromName + ", and braces refuse the conversion unless the "
        "value is a constant that fits. Convert it with a cast, or take the "
        "braces off";

    if (from->isFloating() && to->isInteger())
        src_.fail(pos, "a value of type " + fromName + " cannot be narrowed "
                       "to " + target + " in a braced initialiser - a floating "
                       "value never converts to an integer inside braces, even "
                       "a constant. Convert it with a cast, or take the braces "
                       "off");

    if (from->isInteger()) {
        const bool fromUnsigned = !from->isBool() && !from->isSigned(target_);
        long long v = 0;
        if (!fold(value, &v, pos)) {
            if (to->isFloating())
                src_.fail(pos, "a value of type " + fromName + " cannot be "
                               "narrowed to " + target + " in a braced "
                               "initialiser - an integer converts to a "
                               "floating type inside braces only as a constant "
                               "that survives the trip. Convert it with a cast, "
                               "or take the braces off");
            if (!holdsEvery(from, to, target_)) src_.fail(pos, cannotHoldEvery);
            return;
        }
        const std::string shown = fromUnsigned
            ? std::to_string(static_cast<unsigned long long>(v))
            : std::to_string(v);
        bool fits;
        if (to->isFloating()) {
            if (to->kind() == Kind::LongDouble && to->isX87(target_)) fits = true;
            else if (to->kind() == Kind::Float) fits = roundTrips<float>(v, fromUnsigned);
            else fits = roundTrips<double>(v, fromUnsigned);
            if (!fits)
                src_.fail(pos, shown + " cannot be narrowed to " + target +
                               " - converting it to '" + to->describe() + "' "
                               "and back does not give " + shown + " again, "
                               "and a braced initialiser refuses a conversion "
                               "that changes the value");
            return;
        }
        if (to->isBool()) fits = (v == 0 || v == 1);
        else if (to->size(target_) >= 8)
            // Between two 64-bit types only the sign bit can be lost.
            fits = v >= 0 || fromUnsigned == !to->isSigned(target_);
        else fits = !(fromUnsigned && v < 0) && narrowTo(v, to) == v;
        if (!fits)
            src_.fail(pos, shown + " cannot be narrowed to " + target + " - it "
                           "is not a value '" + to->describe() + "' can hold, "
                           "and a braced initialiser refuses a conversion that "
                           "changes the value. Write one that fits, or convert "
                           "it with a cast");
        return;
    }

    // Floating to floating: only a step down in rank can narrow, and a constant
    // still within range after the step has not. `long double` to `double` is
    // the step even where the target gives the two one representation.
    if (floatingRank(to) >= floatingRank(from)) return;
    long double d = 0;
    // **The two flags are read and dropped, and that is the merge's answer.**
    // foldDouble's flags ask what the build host can represent; this rule asks
    // what the target type can hold. Different questions - see the handover.
    bool past53 = false, x87Rounded = false;
    if (!foldDouble(value, target_, &d, &past53, &x87Rounded))
        src_.fail(pos, cannotHoldEvery);
    const long double limit = to->kind() == Kind::Float
                            ? static_cast<long double>(FLT_MAX)
                            : static_cast<long double>(DBL_MAX);
    if (std::isfinite(d) && std::fabs(d) > limit)
        src_.fail(pos, "this constant cannot be narrowed to " + target +
                       " - it is outside the range '" + to->describe() +
                       "' can hold, and a braced initialiser refuses a "
                       "conversion that changes the value");
}

void Parser::flattenFill(const Type *type, InitCursor &c, int base,
                         std::vector<GlobalPiece> &out) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList) {
        c.at++;
        flattenInit(type, item, base, out);
        return;
    }
    if (const StrLit *s = stringInitialiser(item, type)) {
        c.at++;
        const std::string &text = s->text();
        if (static_cast<long long>(text.size()) > type->length())
            src_.fail(item.pos, "the string has " + std::to_string(text.size()) +
                                " characters and the array holds " +
                                std::to_string(type->length()));

        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < text.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w,
                                       static_cast<long long>(
                                           static_cast<unsigned char>(text[i])), std::string() });
        return;
    }
    if (type->isArray() || type->isStructOrUnion()) {
        flattenAggregate(type, c, base, out);
        return;
    }

    checkNarrowing(type, *item.value, item.pos, std::string());
    flattenScalar(type, item, base, out);
    c.at++;
}

void Parser::flattenAggregate(const Type *type, InitCursor &c, int base,
                              std::vector<GlobalPiece> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        int step = elem->size(target_);
        for (long long i = 0; i < type->length() && !c.done(); i++)
            flattenFill(elem, c, base + static_cast<int>(i) * step, out);
        return;
    }
    const std::vector<Member> &members = type->members();
    std::size_t count = type->kind() == Kind::Union
                      ? (members.empty() ? std::size_t(0) : std::size_t(1))
                      : members.size();
    for (std::size_t i = 0; i < count && !c.done(); i++) {
        const Member &m = members[i];
        if (m.name.empty()) continue;
        if (m.isBitField())
            src_.fail(c.cur().pos,
                      "a bit-field cannot be initialised at file scope yet - "
                      "assign to it in a function");
        flattenFill(m.type, c, base + m.offset, out);
    }
}

void Parser::flattenInit(const Type *type, Init &in, int base,
                         std::vector<GlobalPiece> &out) {
    if (const StrLit *s = stringInitialiser(in, type)) {
        const std::string &text = s->text();
        if (static_cast<long long>(text.size()) > type->length())
            src_.fail(in.pos, "the string has " + std::to_string(text.size()) +
                              " characters and the array holds " +
                              std::to_string(type->length()));
        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < text.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w,
                                       static_cast<long long>(
                                           static_cast<unsigned char>(text[i])), std::string() });
        return;
    }

    if (type->isArray() || type->isStructOrUnion()) {
        if (!in.isList)
            src_.fail(in.pos, type->isArray()
                              ? "an array at file scope needs a braced initialiser"
                              : "a struct or union at file scope needs a braced "
                                "initialiser");
        // **The same C++11 rule the local path names, and a program could reach
        // past it here.** [dcl.init.aggr]/1 makes a class with a member
        // initialiser no aggregate, so these braces are not aggregate init.
        if (type->isStructOrUnion() && !type->tag().empty() &&
            hasMemberInitialiser(type->tag()))
            src_.fail(in.pos, "'" + type->describe() + "' writes an "
                              "initialiser on a member, so in C++11 it is not "
                              "an aggregate and a braced list cannot "
                              "initialise it - C++14 changed that rule and "
                              "this compiler is C++11");
        InitCursor c{ &in.items, 0 };
        flattenAggregate(type, c, base, out);
        if (!c.done())
            src_.fail(c.cur().pos, "this is full, and there are " +
                                   std::to_string(in.items.size() - c.at) +
                                   " more initialiser(s) after it");
        return;
    }

    if (in.isList) {
        if (in.items.size() != 1)
            src_.fail(in.pos, "this is not an aggregate and takes one value");
        if (!in.items[0].isList)
            checkNarrowing(type, *in.items[0].value, in.items[0].pos,
                           std::string());
        flattenInit(type, in.items[0], base, out);
        return;
    }
    flattenScalar(type, in, base, out);
}

void Parser::flattenScalar(const Type *type, Init &in, int base,
                           std::vector<GlobalPiece> &out) {
    ExprPtr value = decay(std::move(in.value));

    if (type->isFloating()) {
        long double d;
        bool past53 = false, x87Rounded = false;
        if (!foldDouble(*value, target_, &d, &past53, &x87Rounded))
            src_.fail(in.pos, "expected a constant initialiser, and this is not "
                              "a constant");
        // **The same refusal the literal gets, for the same reason.** The gate
        // sat on the literal alone and one folded `+` walked past it, so the
        // emitted constant differed by which machine had built the compiler.
        if (x87Rounded || (past53 && type->isX87(target_)))
            src_.fail(in.pos, "this 'long double' constant expression needs "
                              "more precision than a double holds, and a "
                              "double is what this compiler folds constants "
                              "in - the target would keep more of it than "
                              "the build machine can promise. It is refused "
                              "rather than approximated; compute it at run "
                              "time, or write the value as a 'double'");
        long long bits = 0;
        if (type->isX87(target_)) {

            unsigned long long sig = 0;
            unsigned int hi = 0;
            x87Parts(d, &sig, &hi);
            out.push_back(GlobalPiece{ base, 8, static_cast<long long>(sig),
                                       std::string() });
            out.push_back(GlobalPiece{ base + 8, 2, static_cast<long long>(hi),
                                       std::string() });
            return;
        }
        if (type->kind() == Kind::Float) {
            float f = static_cast<float>(d);
            unsigned int u;
            std::memcpy(&u, &f, sizeof u);
            bits = static_cast<long long>(u);
        } else {
            double dd = static_cast<double>(d);
            unsigned long long u;
            std::memcpy(&u, &dd, sizeof u);
            bits = static_cast<long long>(u);
        }
        out.push_back(GlobalPiece{ base, type->size(target_), bits, std::string() });
        return;
    }

    if (type->isPointer()) {
        std::string sym;
        long long off = 0;
        if (foldAddress(*value, &sym, &off)) {
            out.push_back(GlobalPiece{ base, type->size(target_), off, sym });
            return;
        }
    }

    long long v;
    if (!fold(*value, &v, in.pos))
        src_.fail(in.pos, "expected a constant initialiser, and this is not an "
                          "integer constant expression");
    if (type->isInteger()) v = narrowTo(v, type);
    out.push_back(GlobalPiece{ base, type->size(target_), v, std::string() });
}

void Parser::typedefFunctionSuffix(Declared &td) {
    if (!peek().is("(")) return;
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    td.type = types_.functionType(td.type, std::move(params), variadic);
}

bool Parser::foldAddress(const Expr &e, std::string *sym, long long *off) const {
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return e.type()->isPointer() && foldAddress(c->value(), sym, off);

    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        *sym = s->label();
        *off = 0;
        return true;
    }

    if (e.type()->isArray() || e.type()->isFunction())
        return addressOfObject(e, sym, off);

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '&') return addressOfObject(u->operand(), sym, off);

        if (u->op() == '*') return foldAddress(u->operand(), sym, off);
        return false;
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        if (b->op() != BinOp::Add && b->op() != BinOp::Sub) return false;
        long long n = 0;

        if (foldAddress(b->lhs(), sym, off) && fold(b->rhs(), &n, 0)) {
            *off += (b->op() == BinOp::Add) ? n : -n;
            return true;
        }

        if (b->op() == BinOp::Add && fold(b->lhs(), &n, 0) &&
            foldAddress(b->rhs(), sym, off)) {
            *off += n;
            return true;
        }
        return false;
    }
    return false;
}

bool Parser::addressOfObject(const Expr &e, std::string *sym, long long *off) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {

        if (v->isLocal()) return false;
        *sym = v->symbol();
        *off = 0;
        return true;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField()) return false;
        if (!addressOfObject(m->object(), sym, off)) return false;
        *off += m->offset();
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '*') return foldAddress(u->operand(), sym, off);
    return false;
}

