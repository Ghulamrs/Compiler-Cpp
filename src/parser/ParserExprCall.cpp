// The parser: calls, and what a call has to do to its arguments.
//
// Reading an argument list, filling in defaults, copying a by-value class
// parameter, and the full expression's temporaries. Member calls are here as
// well, with the access checks they answer to: `obj.f(1)` is an ordinary call
// once the object's address is in front of the written arguments, and this is
// where that happens.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


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
    // **A by-value parameter is initialised from the argument, so anything
    // that is not an lvalue moves into it.** [dcl.init]/17 makes this
    // ordinary initialisation and overload resolution over the constructors,
    // which picks the move for an xvalue and for a prvalue alike; this path
    // predates rvalue references and reaches for the copy by name, so the
    // choice is made here instead. Without it `take(static_cast<S &&>(e))`
    // copies, silently, and `e` is left untouched where C++ says it has been
    // emptied - and `take(make())`, a temporary, insists on the copy
    // constructor of a class that may not have one to spare.
    const Signature *cc = nullptr;
    if (!isLvalue(*arg)) cc = moveConstructorOf(cls);
    if (cc == nullptr) cc = copyConstructorOf(cls);

    // **[dcl.init]/17 makes this copy-initialization, so the constructor it
    // picks may not be `explicit`.** A by-value parameter is the third place
    // that rule bites, after `S b = a;` and `return s;` - and the least
    // obvious of the three, since nothing at the call site is written with an
    // `=` in it. The check is here rather than in overload resolution because
    // the copy is not a candidate set: this path reaches for the copy or move
    // constructor by name.
    if (cc != nullptr && cc->isExplicit)
        src_.fail(pos, what + " is a '" + cls->describe() + "' passed by "
                       "value, which copy-initialises it from the argument - "
                       "and that may not pick the 'explicit' constructor this "
                       "class copies with. Take it by reference, or make the "
                       "copy constructor not explicit");

    // **A class that declares a move constructor cannot be passed by value
    // from an lvalue.** [class.copy]/7 deletes its implicit copy constructor,
    // and here that deletion is an absence - no copy was ever declared - so
    // without this the byte path below would answer for it, silently, and
    // two objects would own one resource. An xvalue or a temporary took the
    // move above and never reaches this.
    if (cc == nullptr) {
        const Signature *mv = moveConstructorOf(cls);
        if (mv != nullptr && !mv->implicit)
            src_.fail(pos, what + " is a '" + cls->describe() + "' passed by "
                           "value, and this class declares a move constructor "
                           "- so its copy constructor is deleted and an lvalue "
                           "cannot be copied into the parameter. Write "
                           "'static_cast<" + cls->describe() + " &&>(...)' to "
                           "move out of it, or give the class a copy "
                           "constructor");
    }

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
    if (cc->access != Access::Public && currentClass_ != cls && !isFriendOf(cls))
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
        if (made->type() == cls && returnsIndirectly(cls, made->hasThis())) {
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
                             std::vector<ExprPtr> args, bool hasThis) {
    if (variadic ? args.size() < params.size() : args.size() != params.size())
        src_.fail(pos, "'" + name + "' takes " + (variadic ? "at least " : "") +
                       std::to_string(params.size()) + " argument(s), given " +
                       std::to_string(args.size()));

    // **A call through a pointer promises nothing.** The specification is not
    // part of the type in C++11, so a `int (*)()` says nothing about whether
    // what it points at throws - and `noexcept(p())` is false for every
    // function pointer, which is what clang answers too.
    if (callee != nullptr) mayThrow_++;

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
    call->setHasThis(hasThis);
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
    std::vector<ExprPtr> args;
    parseArguments(args);
    return memberCallWith(std::move(object), cls, name, pos, std::move(args));
}

// The same call with its arguments already in hand. **An overloaded operator
// is what split this in two**: `a + b` has parsed its right operand long
// before it knows there is a call here at all, so the arguments cannot come
// off the token stream the way `a.f(b)` takes them.
ExprPtr Parser::memberCallWith(ExprPtr object, const Type *cls,
                               const std::string &name, std::size_t pos,
                               std::vector<ExprPtr> args) {
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

    const Signature &sig = resolveOverload(key, args, pos, cls);
    applyDefaults(sig, args, pos);

    // Now there IS an inside, and this is where it starts to mean something:
    // a private member is reachable from another member of the same class.
    if (sig.access != Access::Public && !insideAccessOf(plain) &&
        !insideAccessOf(owner) && !isFriendOf(plain) && !isFriendOf(owner)) {
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

    // The object's address went in front of the written arguments a few lines
    // up, which is what makes this an ordinary call from here down - and what
    // the Microsoft ABI has to be told about, since it puts a hidden return
    // pointer after `this` rather than in front of it.
    ExprPtr call = completeCall(name, sig.symbol, std::move(callee), sig.returns,
                                full, sig.variadic, pos, std::move(all), true);
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
// [class.friend]: is the function whose body is being read one this class
// granted access to? Asked by every access check, beside the question about
// being inside the class - the two are the only ways past a private member,
// and they are asked in the same breath everywhere.
bool Parser::isFriendOf(const Type *cls) const {
    if (cls == nullptr || currentFunction_.empty()) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friends_.find(cls->unqualified()->tag());
    if (it == friends_.end()) return false;
    for (std::size_t i = 0; i < it->second.size(); i++)
        if (it->second[i] == currentFunction_) return true;
    return false;
}

// **A lambda has the access of the function it was written in** -
// [expr.prim.lambda]/7 gives the closure's call operator the context's access.
// Inside one, `currentClass_` is the closure, so without this a lambda in a
// member function could not read its own class's privates. Both access checks
// ask this rather than comparing `currentClass_` themselves, because they had
// already drifted apart once: the data-member check learned about closures and
// the member-function one did not, so a private field was readable from a
// lambda and a private method was not.
bool Parser::insideAccessOf(const Type *cls) const {
    if (cls == nullptr || currentClass_ == nullptr) return false;
    const Type *want = cls->unqualified();
    if (currentClass_ == want) return true;
    std::map<std::string, const Type *>::const_iterator outer =
        closureOuter_.find(currentClass_->unqualified()->tag());
    return outer != closureOuter_.end() && outer->second == want;
}

void Parser::checkAccessible(const Type *object, const Member &m,
                             std::size_t pos) const {
    if (m.access == Access::Public) return;
    if (insideAccessOf(object)) return;
    if (isFriendOf(object)) return;
    const char *how = m.access == Access::Private ? "private" : "protected";
    src_.fail(pos, "'" + m.name + "' is " + how + " in '" + object->describe() +
                   "' - it can be named only from inside the class, and this "
                   "is outside it");
}
