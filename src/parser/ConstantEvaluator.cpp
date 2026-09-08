// The arithmetic a constant expression is checked against - what a case
// label, an array bound, a `static_assert` and a global's initialiser all
// ask. Moved here from ParserConst.cpp, which keeps the grammar half.
#include "ConstantEvaluator.h"

#include "../Ast.h"
#include "../Source.h"
#include "../Type.h"

bool ConstantEvaluator::fold(const Expr &e, long long *out, std::size_t pos) const {
    // **A name, when it names a constant.** The object is real and has an address; what
    // is answered here is what it is worth when read, which [expr.const] allows of a
    // const integral. Locals first, a local shadowing the global as everywhere else.
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        // **Inside a constexpr call, a local name is a parameter.** The body being
        // folded belongs to another function, so its Vars name slots in a frame that
        // does not exist; what they are worth is on the top of this stack.
        if (v->isLocal() && !frames_.empty()) {
            const std::vector<std::pair<int, long long> > &frame = frames_.back();
            for (std::size_t i = 0; i < frame.size(); i++)
                if (frame[i].first == v->offset()) { *out = frame[i].second; return true; }
        }
        if (v->isLocal())
            return names_.localConstant(v->name(), out);
        if (names_.globalConstant(v->name(), out)) return true;
        // A static member is not among the globals - it is kept apart from the
        // member list so nothing walking a layout has to skip it - so the
        // read-back asks by its symbol.
        return names_.staticMemberConstant(v->symbol(), out);
    }
    // **A call to a constexpr function.** C++11 lets its body be one return statement,
    // so running it is folding that expression with the parameters standing for the
    // arguments. A call to anything else simply does not fold, which is the answer.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        std::map<std::string, Fn>::const_iterator it = fns_.find(c->symbol());
        if (it == fns_.end()) return false;
        const Fn &fn = it->second;
        if (fn.value == nullptr || c->args().size() != fn.slots.size())
            return false;

        // A recursion that does not end is a compiler that does not either.
        // The standard lets an implementation set a limit and say so; this is
        // that limit, and it is said where it is reached.
        if (frames_.size() >= 256)
            src_.fail(pos, "this constant expression is more than 256 calls "
                           "deep - a 'constexpr' function that never stops "
                           "recursing cannot be worked out while compiling");

        std::vector<std::pair<int, long long> > frame;
        for (std::size_t i = 0; i < c->args().size(); i++) {
            long long v = 0;
            // **Folded outside the new frame, in the caller's.** An argument is an
            // expression where the call is written, so `fact(n - 1)` reads the
            // caller's n; folding it after the push would read the callee's slot.
            if (!fold(*c->args()[i], &v, pos)) return false;
            frame.push_back(std::make_pair(fn.slots[i], v));
        }
        frames_.push_back(frame);
        long long result = 0;
        const bool ok = fold(*fn.value, &result, pos);
        frames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) return false;
        *out = n->value();
        return true;
    }

    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        long long v;
        if (!fold(c->value(), &v, pos)) return false;
        if (!e.type()->isInteger()) return false;
        *out = narrowTo(v, e.type());
        return true;
    }

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        long long v;
        if (!fold(u->operand(), &v, pos)) return false;
        switch (u->op()) {
        case '-': *out = static_cast<long long>(0ULL - static_cast<unsigned long long>(v)); return true;
        case '+': *out = v; return true;
        case '!': *out = !v; return true;
        case '~': *out = ~v; return true;
        default: return false;
        }
    }

    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, pos)) return false;
        return fold(t ? c->thenArm() : c->elseArm(), out, pos);
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long l, r;
        if (!fold(b->lhs(), &l, pos) || !fold(b->rhs(), &r, pos)) return false;

        const Type *t = b->lhs().type();
        bool uns = t->isInteger() && !t->isSigned(target_);
        unsigned long long ul = static_cast<unsigned long long>(l);
        unsigned long long ur = static_cast<unsigned long long>(r);

        switch (b->op()) {
        case BinOp::Add: *out = static_cast<long long>(ul + ur); return true;
        case BinOp::Sub: *out = static_cast<long long>(ul - ur); return true;
        case BinOp::Mul: *out = static_cast<long long>(ul * ur); return true;
        case BinOp::Div:
        case BinOp::Mod:
            if (r == 0)
                src_.fail(pos, "division by zero in a constant expression");
            if (!uns && ul == (1ULL << 63) && r == -1) {
                *out = (b->op() == BinOp::Div) ? l : 0;
                return true;
            }
            if (b->op() == BinOp::Div)
                *out = uns ? static_cast<long long>(ul / ur) : l / r;
            else
                *out = uns ? static_cast<long long>(ul % ur) : l % r;
            return true;
        case BinOp::Shl:
        case BinOp::Shr:
            if (r < 0 || r >= 64)
                src_.fail(pos, "shift count out of range in a constant expression");
            if (b->op() == BinOp::Shl) *out = static_cast<long long>(ul << r);
            else *out = uns ? static_cast<long long>(ul >> r) : (l >> r);
            return true;
        case BinOp::BitAnd: *out = l & r; return true;
        case BinOp::BitOr:  *out = l | r; return true;
        case BinOp::BitXor: *out = l ^ r; return true;
        case BinOp::Eq: *out = (l == r); return true;
        case BinOp::Ne: *out = (l != r); return true;
        case BinOp::Lt: *out = uns ? (ul <  ur) : (l <  r); return true;
        case BinOp::Le: *out = uns ? (ul <= ur) : (l <= r); return true;
        case BinOp::Gt: *out = uns ? (ul >  ur) : (l >  r); return true;
        case BinOp::Ge: *out = uns ? (ul >= ur) : (l >= r); return true;
        case BinOp::LAnd: *out = (l && r); return true;
        case BinOp::LOr:  *out = (l || r); return true;
        }
        return false;
    }

    return false;
}

// **[expr.const]/3: a named integral constant is a constant expression**, which is the
// rule that lets C++ write `const int n = 4; int a[n];`. Only integral, because that
// is what fold() answers in. Below it: the one expression a constexpr body may be.
const Expr *ConstantEvaluator::singleReturnValue(const Stmt &body) const {
    const Stmt *at = &body;
    for (;;) {
        const Block *b = dynamic_cast<const Block *>(at);
        if (b == nullptr) break;
        if (b->body().size() != 1) return nullptr;
        at = b->body()[0].get();
    }
    const Return *r = dynamic_cast<const Return *>(at);
    if (r == nullptr || !r->hasValue()) return nullptr;
    return &r->value();
}

long long ConstantEvaluator::narrowTo(long long v, const Type *t) const {
    int bits = t->size(target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}
