// The parser: making and destroying objects in an expression. `new` and `delete`
// and the allocator calls behind them, `throw` and the Microsoft form of it, and
// the temporaries a class-typed expression needs, the value-init zeroing included.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


// `P(1)` - a temporary of class type. **The object goes in a slot of this frame
// and the expression answers with its name**, the constructor sequenced in front;
// it dies at the end of the full expression, which is what `pendingTemps_` is for.
ExprPtr Parser::pathAccess(ExprPtr root, const std::vector<InitStep> &path) {
    ExprPtr e = std::move(root);
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

// **Zero every scalar leaf of `type` reachable from `root`**, one store per leaf
// in declaration order. The root is whatever names the object and is copied for
// each leaf with clonePure, so it has to be pure; it used to be a slot number.
void Parser::zeroLeaves(const Expr &root, const Type *type,
                        std::vector<InitStep> &path,
                        std::vector<ExprPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            zeroLeaves(root, elem, path, out);
            path.pop_back();
        }
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        // A union is zeroed through its first member, which is what
        // [dcl.init]/8 asks for and what initZero already does.
        const std::size_t count = type->kind() == Kind::Union
                                ? (members.empty() ? std::size_t(0) : std::size_t(1))
                                : members.size();
        for (std::size_t i = 0; i < count; i++) {
            if (members[i].name.empty()) continue;
            path.push_back(InitStep{ &members[i], 0 });
            zeroLeaves(root, members[i].type, path, out);
            path.pop_back();
        }
        return;
    }
    ExprPtr z;
    if (type->isFloating()) { z.reset(new Num(0.0L)); z->setType(types_.doubleType()); }
    else                    { z.reset(new Num(0LL));  z->setType(types_.intType()); }
    ExprPtr again = clonePure(root);
    if (again == nullptr)
        src_.fail(0, "internal: value-initialisation was asked to zero an "
                     "object it cannot name twice");
    ExprPtr target = pathAccess(std::move(again), path);
    ExprPtr store(new Assign(std::move(target), convert(std::move(z), type)));
    store->setType(type);
    out.push_back(std::move(store));
}

// The stores zeroLeaves makes, chained by commas into one expression - or
// nullptr when there is no leaf to set, which is what an empty class is.
ExprPtr Parser::zeroChain(const Expr &root, const Type *type) {
    std::vector<ExprPtr> zeros;
    std::vector<InitStep> path;
    zeroLeaves(root, type->unqualified(), path, zeros);
    if (zeros.empty()) return nullptr;
    ExprPtr chain = std::move(zeros[0]);
    for (std::size_t i = 1; i < zeros.size(); i++) {
        const Type *t = zeros[i]->type();
        ExprPtr next(new Comma(std::move(chain), std::move(zeros[i])));
        next->setType(t);
        chain = std::move(next);
    }
    return chain;
}

ExprPtr Parser::classTemporary(const Type *cls, std::size_t pos) {
    const Type *plain = cls->unqualified();
    std::vector<ExprPtr> args;
    parseArguments(args);

    const std::string key = constructorKey(plain->tag());
    if (overloadsOf(key) == nullptr) {
        // No constructor at all: `P(x)` is then a copy of another P, which is
        // a move of bytes, and `P()` is an object with nothing to set.
        if (args.size() > 1)
            src_.fail(pos, "'" + plain->describe() + "' has no constructor, so "
                           "'" + plain->tag() + "(...)' can only be a copy of "
                           "another one - and this gives " +
                           std::to_string(args.size()) + " arguments");
        const int slot = allocateFrameSlot(plain);
        if (destructorOf(plain) != nullptr)
            pendingTemps_.push_back(std::make_pair(slot, plain));
        ExprPtr obj(Var::local("$tmp", slot));
        obj->setType(plain);
        // Expiring, whichever of the three shapes below answers - the same
        // mark the constructor branch sets, for the same reason.
        obj->setXvalue();
        if (args.empty()) {
            // **[dcl.init]/8: `P()` value-initialises, and for a class with no
            // user-provided constructor that means zeroing it.** The slot was
            // handed back as it stood, so `f(P())` read whatever the frame held.
            ExprPtr chain = zeroChain(*obj, plain);
            if (chain == nullptr) return obj;
            // Comma'd with the object's *address* and dereferenced, which is
            // the shape the argument path below already uses: a comma whose
            // value is a class lvalue is not something every backend spells.
            ExprPtr again(Var::local("$tmp", slot));
            again->setType(plain);
            ExprPtr at(new Unary('&', std::move(again)));
            at->setType(types_.pointerTo(plain));
            ExprPtr both(new Comma(std::move(chain), std::move(at)));
            both->setType(types_.pointerTo(plain));
            ExprPtr made(new Unary('*', std::move(both)));
            made->setType(plain);
            made->setXvalue();
            return made;
        }
        checkAssignable(*args[0], plain, pos, "this temporary");
        ExprPtr store(new Assign(std::move(obj), std::move(args[0])));
        store->setType(plain);
        ExprPtr again(Var::local("$tmp", slot));
        again->setType(plain);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(types_.pointerTo(plain));
        ExprPtr both(new Comma(std::move(store), std::move(at)));
        both->setType(types_.pointerTo(plain));
        ExprPtr made(new Unary('*', std::move(both)));
        made->setType(plain);
        made->setXvalue();
        return made;
    }

    // **Read before the argument list is touched.** [dcl.init]/8's other half: a
    // class whose default constructor nobody wrote is zeroed *and then* built,
    // where a user-provided one gets no zeroing. The same paragraph says both.
    const bool valueInit = args.empty();
    const Signature &ctor = resolveOverload(key, args, pos);
    applyDefaults(ctor, args, pos);
    if (ctor.access != Access::Public && currentClass_ != plain &&
        !isFriendOf(plain))
        src_.fail(pos, "'" + plain->describe() + "' has no public constructor "
                       "taking these arguments - the one that matches is " +
                       (ctor.access == Access::Private ? "private" : "protected"));
    const bool zeroFirst = valueInit && ctor.implicit;

    const int slot = allocateFrameSlot(plain);
    if (destructorOf(plain) != nullptr)
        pendingTemps_.push_back(std::make_pair(slot, plain));

    const Type *ptr = types_.pointerTo(plain);
    ExprPtr obj(Var::local("$tmp", slot));
    obj->setType(plain);
    ExprPtr addr(new Unary('&', std::move(obj)));
    addr->setType(ptr);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
    std::vector<const Type *> full;
    full.push_back(ptr);
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);

    ExprPtr call = completeCall(plain->tag(), ctor.symbol, nullptr,
                                types_.get(Kind::Void), full, false, pos,
                                std::move(all));
    if (zeroFirst) {
        ExprPtr fresh(Var::local("$tmp", slot));
        fresh->setType(plain);
        if (ExprPtr chain = zeroChain(*fresh, plain)) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    }

    // **A dereference of a pointer, not the object beside a comma.** `isGlvalue`
    // gives a comma its right operand's value category, so the parser would let
    // anyone take its address and no backend can. `*(ctor(&tmp), &tmp)` they know.
    ExprPtr again(Var::local("$tmp", slot));
    again->setType(plain);
    ExprPtr at(new Unary('&', std::move(again)));
    at->setType(ptr);
    ExprPtr both(new Comma(std::move(call), std::move(at)));
    both->setType(ptr);
    ExprPtr made(new Unary('*', std::move(both)));
    made->setType(plain);
    // **What `T(...)` makes is about to expire, and the marker is how the rest of
    // the compiler is told.** The lowering reads as an ordinary lvalue, so without
    // it a temporary passed by value reached for the copy constructor.
    made->setXvalue();
    return made;
}

// ---------------------------------------------------------------- new and delete
// **The four operator functions are called by name, and the names were measured**
// at -O0 on all three targets. The platform's own: a cxx1 `new` meets clang's.
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

// **The Microsoft ABI throws from the stack, not from the heap**: `T tmp = x;` and
// then `_CxxThrowException(&tmp, &_TI1<letter>)`, where Itanium asks the runtime
// for memory. Identity is the ThrowInfo chain, four objects the backend emits.
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

// **`throw x;` is three calls and a store, and no new machinery**:
// __cxa_allocate_exception, the store, __cxa_throw with the type that identifies
// it. That type_info pointer is the work - fundamental types, the rest refused.
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

    // The initialiser, and only the forms that need no constructor; anything else
    // is refused by name rather than half-built. A class with constructors is
    // built by calling one, here as much as on the stack.
    const bool constructed = made->isStructOrUnion() && !made->tag().empty() &&
                             overloadsOf(constructorKey(made->tag())) != nullptr;
    std::vector<ExprPtr> ctorArgs;
    bool hasInit = false;
    ExprPtr init;
    if (peek().is("(")) {
        at_++;
        hasInit = true;
        if (array) {
            // **`new T[n]()` value-initialises every element** - [expr.new]/17
            // allows exactly the empty pair there and nothing inside it, so
            // `new int[n](5)` is refused the way clang refuses it.
            if (!consume(")"))
                src_.fail(peek().pos, "'new T[n](x)' cannot initialise an "
                                      "array - only the empty '()' is allowed "
                                      "there, and it zeroes every element");
        } else if (constructed) {
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
    // The byte count is wanted twice for `new T[n]()` - once by the allocator
    // and once by the zeroing - and n is any expression, so it is computed
    // once into a slot and the allocator reads the assignment's value.
    int bytesSlot = 0;
    std::string bytesTemp;
    if (array && hasInit) {
        bytesSlot = allocateFrameSlot(sizeT);
        bytesTemp = ".newn" + std::to_string(newTemps_);
        ExprPtr held(Var::local(bytesTemp, bytesSlot));
        held->setType(sizeT);
        ExprPtr save(new Assign(std::move(held), std::move(bytes)));
        save->setType(sizeT);
        bytes = std::move(save);
    }

    const Type *pointer = types_.pointerTo(made);
    ExprPtr raw = callAllocator(array ? "_Znam" : "_Znwm",
                                array ? "??_U@YAPEAX_K@Z" : "??2@YAPEAX_K@Z",
                                types_.pointerTo(types_.get(Kind::Void)),
                                std::move(bytes), pos);
    ExprPtr typed(new Cast(pointer, std::move(raw)));
    typed->setType(pointer);

    if (!hasInit && !constructed) return typed;

    // `new int(5)` is an allocation and a store where an expression yields one
    // value, so the pointer is kept in a temporary and the comma sequences them,
    // as bindReference does. A constructed object puts a call where the store is.
    int slot = allocateFrameSlot(pointer);
    std::string temp = ".new" + std::to_string(newTemps_++);

    ExprPtr held(Var::local(temp, slot));
    held->setType(pointer);
    ExprPtr keep(new Assign(std::move(held), std::move(typed)));
    keep->setType(pointer);

    if (array) {
        // **n elements zeroed by the platform's `memset`**, as the storage came
        // from its `operator new[]`: n is a run-time value and this expression
        // language has no loop. It is also what clang emits for the same line.
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        std::vector<ExprPtr> args;
        ExprPtr at(Var::local(temp, slot));
        at->setType(pointer);
        ExprPtr asVoid(new Cast(voidPtr, std::move(at)));
        asVoid->setType(voidPtr);
        args.push_back(std::move(asVoid));
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        args.push_back(std::move(zero));
        ExprPtr n(Var::local(bytesTemp, bytesSlot));
        n->setType(sizeT);
        args.push_back(std::move(n));
        std::vector<int> argSlots(args.size(), 0);
        Call *fill = new Call("memset", nullptr, std::move(args), false, 0, -1,
                              std::move(argSlots));
        fill->setSymbol("memset");
        ExprPtr filled(fill);
        filled->setType(voidPtr);

        ExprPtr result(Var::local(temp, slot));
        result->setType(pointer);
        ExprPtr both(new Comma(std::move(keep), std::move(filled)));
        both->setType(voidPtr);
        ExprPtr all(new Comma(std::move(both), std::move(result)));
        all->setType(pointer);
        return all;
    }

    if (constructed) {
        const Signature &ctor = resolveOverload(constructorKey(made->tag()),
                                                ctorArgs, pos);
        // `new V()` and not `new V`, for a class whose default constructor nobody
        // wrote: zeroed first, then built. **Read before applyDefaults**, which
        // appends to ctorArgs and would make "is the list empty" another question.
        const bool zeroFirst = hasInit && ctorArgs.empty() && ctor.implicit;
        // **The defaults, as every other constructor call reads them.** This call
        // is built by hand, one argument per parameter, and `new M` of an
        // `M(int a = 5)` was refused after resolution had already accepted it.
        applyDefaults(ctor, ctorArgs, pos);
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
        if (zeroFirst) {
            ExprPtr p(Var::local(temp, slot));
            p->setType(pointer);
            ExprPtr obj(new Unary('*', std::move(p)));
            obj->setType(made);
            if (ExprPtr chain = zeroChain(*obj, made)) {
                const Type *ct = chain->type();
                ExprPtr seq(new Comma(std::move(keep), std::move(chain)));
                seq->setType(ct);
                keep = std::move(seq);
            }
        }
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

    // **`new P()` for a class with no constructor is value-initialisation too**,
    // which [dcl.init]/8 makes a zeroing. The scalar branch converted its 0 to the
    // class type, so `new P()` read address 0; the stores go through the pointer.
    if (!init && made->isStructOrUnion()) {
        ExprPtr result0(Var::local(temp, slot));
        result0->setType(pointer);
        ExprPtr chain = zeroChain(*where, made);
        if (chain == nullptr) {
            // An empty class: nothing observable to set, the allocation is
            // the whole of the work.
            ExprPtr all(new Comma(std::move(keep), std::move(result0)));
            all->setType(pointer);
            return all;
        }
        const Type *ct = chain->type();
        ExprPtr both(new Comma(std::move(keep), std::move(chain)));
        both->setType(ct);
        ExprPtr all(new Comma(std::move(both), std::move(result0)));
        all->setType(pointer);
        return all;
    }

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

    // **A virtual destructor is reached through the vtable**, the static type not
    // being the one that has to be destroyed. The slot holds the deleting form,
    // which frees as well, so this path calls once and never operator delete.
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
        ExprPtr both(new Comma(std::move(save),
                               guardAgainstNull(temp, slot, t, std::move(call))));
        both->setType(types_.get(Kind::Void));
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

        ExprPtr both(new Comma(std::move(save),
                               guardAgainstNull(temp, slot, t, std::move(run))));
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

// **[expr.delete]/2: deleting a null pointer has no effect**, and running the
// destructor on one is how `delete p;` crashed. Written `p != 0 ? (call, 1) : 0`
// rather than with void arms, an int each side being a shape every backend emits.
ExprPtr Parser::guardAgainstNull(const std::string &temp, int slot,
                                 const Type *ptr, ExprPtr body) {
    ExprPtr probe(Var::local(temp, slot));
    probe->setType(ptr);
    ExprPtr n(new Num(static_cast<long long>(0)));
    n->setType(types_.intType());
    ExprPtr test(new Binary(BinOp::Ne, std::move(probe), convert(std::move(n), ptr)));
    test->setType(types_.intType());

    ExprPtr one(new Num(static_cast<long long>(1)));
    one->setType(types_.intType());
    ExprPtr ran(new Comma(std::move(body), std::move(one)));
    ran->setType(types_.intType());

    ExprPtr skipped(new Num(static_cast<long long>(0)));
    skipped->setType(types_.intType());

    ExprPtr guarded(new Conditional(std::move(test), std::move(ran),
                                    std::move(skipped)));
    guarded->setType(types_.intType());
    return guarded;
}
