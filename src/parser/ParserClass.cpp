// The parser: what a class needs written for it. Constructors and destructors,
// vtables and thunks, the implicit special members and the code that defines them
// when something calls one, and the static data members. Rungs 3 and 4.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

StmtPtr Parser::constructLocal(const Declared &d, int offset,
                               std::vector<ExprPtr> args, bool copyInit,
                               bool valueInit) {
    const std::string key = constructorKey(d.type->tag());
    const Signature &ctor = resolveOverload(key, args, d.pos);
    applyDefaults(ctor, args, d.pos);

    // **[dcl.init]/16: copy-initialization may not pick an `explicit` constructor.**
    // `S s(3);` and `S s = 3;` call the same function, and this is the whole of what
    // `explicit` does. Checked after resolution, so the reader is told which one.
    if (copyInit && ctor.isExplicit)
        src_.fail(d.pos, "'" + d.type->describe() + "' has a constructor "
                         "taking these arguments and it is 'explicit', so it "
                         "will not be chosen for '" + d.name + " = ...' - "
                         "write '" + d.type->describe() + " " + d.name +
                         "(...)', which asks for it by name");

    if (ctor.access != Access::Public && currentClass_ != d.type->unqualified() &&
        !isFriendOf(d.type))
        src_.fail(d.pos, "'" + d.type->describe() + "' has no public constructor "
                         "taking these arguments - the one that matches is " +
                         (ctor.access == Access::Private ? "private" : "protected"));

    const Type *thisType = types_.pointerTo(d.type->unqualified());
    ExprPtr object(Var::local(d.name, offset));
    object->setType(d.type);
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(thisType);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);

    ExprPtr call = completeCall(d.type->tag(), ctor.symbol, nullptr,
                                types_.get(Kind::Void), full, false, d.pos,
                                std::move(all));

    // **The zeroing half of [dcl.init]/8, spelt as the temporary path spells
    // it.** A constructor somebody wrote is the whole of the initialisation; an
    // implicit one leaves the members it does not name, so `{}` zeroes first.
    if (valueInit && ctor.implicit) {
        ExprPtr fresh(Var::local(d.name, offset));
        fresh->setType(d.type);
        if (ExprPtr chain = zeroChain(*fresh, d.type->unqualified())) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    }
    return StmtPtr(new ExprStmt(std::move(call)));
}

// The deleting destructor's name. Built through the manglers rather than by
// concatenation, because a nested class's is a whole nested-name -
// ??_GInner@Outer@@UEAAPEAXI@Z, not ??_GOuter::Inner@@...
bool Parser::overrides(const VSlot &s, const std::string &name,
                       const std::vector<const Type *> &params, bool constThis) {
    if (s.name != name || s.constThis != constThis) return false;
    return sameParameters(s.params, params);
}

std::string Parser::deletingDestructorSymbol(const std::string &cls) {
    return target_.microsoftNames()
         ? microsoftDeletingDestructorName(cls, findTypedef(cls))
         : itaniumDeletingDestructorName(cls, findTypedef(cls));
}

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");
    // A destructor is `noexcept` in C++11 whether or not it says so
    // ([except.spec]/14), so what is written here only has to be accepted - and
    // `~S() noexcept(false)` opts back out, which is why it is read at all.
    pendingNoexcept_ = exceptionSpecification();

    if (overloadsOf(destructorKey(cls)) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");
    registerDestructor(cls, pos, access, isVirtual, false);
}

// Everything a destructor needs in the tables, whether a program wrote it or
// the compiler did: the name, the entry, and the vtable slots a virtual one
// claims.
void Parser::registerDestructor(const std::string &cls, std::size_t pos,
                                Access access, bool isVirtual, bool implicit) {
    const std::vector<const Type *> params;
    const std::string key = destructorKey(cls);

    // **A base with a virtual destructor makes this one virtual**, keyword or not -
    // [class.dtor]. The base's slots are already in this class's table, so it is
    // answered by looking for the "~" entry, and before the name is built.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == "~") { slot = i; isVirtual = true; break; }

    // **A virtual destructor is U on Microsoft whatever its access**, the same
    // rule a virtual member function already followed - measured with cl,
    // which writes ??1VB@@UEAA@XZ where a non-virtual public one is QEAA.
    const char code = isVirtual                   ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out;
    if (target_.microsoftNames()) out = microsoftDestructorName(cls, findTypedef(cls), code);
    else                          itaniumDestructorName(cls, findTypedef(cls), true, &out);

    functionIndex_[key].push_back(functions_.size());
    functions_.push_back(Signature{ "~" + localOf(cls), out, types_.get(Kind::Void),
                                    params, false, false, pos, false, cls, false,
                                    access, isVirtual });
    functions_.back().implicit = implicit;

    // **A virtual destructor claims slots where it is declared**, and how many
    // depends on the ABI: Itanium two and adjacent, Microsoft one holding only the
    // deleting form. A derived class overrides in place, matching on "~".
    if (!isVirtual) return;
    const bool ms = target_.microsoftNames();
    const std::string deleting = deletingDestructorSymbol(cls);

    std::vector<const Type *> none;
    if (slot < slots.size()) {
        slots[slot].symbol = ms ? deleting : out;       // the complete form
        if (!ms && slot + 1 < slots.size() &&
            slots[slot + 1].name == "~$deleting")
            slots[slot + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
}

// **Setting the vptr, for whoever is building the object**, pulled out of the
// constructor path when implicit constructors arrived. What is stored is the table's
// address plus the header: Itanium's is two pointers, and Microsoft has none.
std::vector<StmtPtr> Parser::storeVptrs(const std::string &cls,
                                        const Type *memberOf, int thisSlot) {
    const bool ms = target_.microsoftNames();
    const std::string table = vtableSymbol(cls, ms);
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *entries = types_.pointerTo(entry);

    // **The table's ADDRESS, not its contents.** A global Var is an lvalue and
    // reading one loads from it, which stored the table's first word in the vptr and
    // crashed on the first call. Giving it the array type and decaying it is it.
    std::size_t entryCount = vtables_[cls].size() + (ms ? 0 : 2);
    {
        const std::vector<Type::BaseSpec> &all = memberOf->bases();
        for (std::size_t bi = 1; bi < all.size(); bi++)
            if (all[bi].type->polymorphic())
                entryCount += vtables_[all[bi].type->tag()].size() + (ms ? 0 : 2);
    }
    ExprPtr base(Var::global(table));
    base->setType(types_.arrayOf(entry, static_cast<long long>(entryCount)));
    ExprPtr value = decay(std::move(base));
    if (!ms) {
        // **In bytes, because this Add is not the parser's pointer arithmetic.**
        // Building the node by hand skips the scaling `p + n` normally gets, so
        // adding 2 added two bytes. The header is two pointers wide.
        const long long header = 2LL * entry->size(target_);
        ExprPtr skip(new Num(header));
        skip->setType(types_.intType());
        ExprPtr past(new Binary(BinOp::Add, std::move(value), std::move(skip)));
        past->setType(entries);
        value = std::move(past);
    }
    ExprPtr asVoid(new Cast(entry, std::move(value)));
    asVoid->setType(entry);

    ExprPtr self(Var::local("this", thisSlot));
    self->setType(entries);                        // the vptr lives at offset 0
    ExprPtr where(new Unary('*', std::move(self)));
    where->setType(entry);

    ExprPtr store(new Assign(std::move(where), std::move(asVoid)));
    store->setType(entry);

    std::vector<StmtPtr> withVptr;
    withVptr.push_back(StmtPtr(new ExprStmt(std::move(store))));

    // **A class with a polymorphic second base has a second vptr**, inside that
    // base's subobject, pointing at the secondary table laid down behind the primary
    // one. The first vptr is the object's own; this is the one a B * will read.
    const std::vector<Type::BaseSpec> &bs = memberOf->bases();
    for (std::size_t bi = 1; bi < bs.size(); bi++) {
        if (!bs[bi].type->polymorphic()) continue;
        std::map<std::string, int>::const_iterator where =
            secondaryVptr_.find(cls + "::" + bs[bi].type->tag());
        if (where == secondaryVptr_.end()) continue;

        ExprPtr t2(Var::global(table));
        t2->setType(types_.arrayOf(entry, static_cast<long long>(entryCount)));
        ExprPtr addr2 = decay(std::move(t2));
        ExprPtr skip2(new Num(static_cast<long long>(where->second)));
        skip2->setType(types_.intType());
        ExprPtr into(new Binary(BinOp::Add, std::move(addr2), std::move(skip2)));
        into->setType(entries);
        ExprPtr val2(new Cast(entry, std::move(into)));
        val2->setType(entry);

        ExprPtr self2(Var::local("this", thisSlot));
        self2->setType(types_.pointerTo(memberOf));
        ExprPtr atBase = convert(std::move(self2),
                                 types_.pointerTo(bs[bi].type));
        ExprPtr slotPtr(new Cast(types_.pointerTo(entry), std::move(atBase)));
        slotPtr->setType(types_.pointerTo(entry));
        ExprPtr there(new Unary('*', std::move(slotPtr)));
        there->setType(entry);

        ExprPtr store2(new Assign(std::move(there), std::move(val2)));
        store2->setType(entry);
        withVptr.push_back(StmtPtr(new ExprStmt(std::move(store2))));
    }

    return withVptr;
}

// **A function with no source behind it.** The deleting destructor runs the
// destructor and then gives the memory back, because `delete p` through a base
// pointer reaches both through one slot. Itanium's D0 and Microsoft's ??_G differ.
void Parser::synthesizeDeleting(const std::string &cls, const Type *type,
                                Access access, std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const Type *self = types_.pointerTo(type);
    const std::string symbol = deletingDestructorSymbol(cls);
    (void)access;

    // Its own frame: `this`, and on Windows the flag beside it.
    const int savedFrame = frameSize_;
    frameSize_ = 0;
    std::vector<Param> params;
    int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    int flagSlot = 0;
    const Type *flagType = types_.get(Kind::UInt);
    if (ms) {
        flagSlot = allocateFrameSlot(flagType);
        params.push_back(Param{ flagType, flagSlot });
    }

    std::vector<StmtPtr> body;

    const Signature *dtor = destructorOf(type);
    if (dtor != nullptr) {
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(me), *dtor, pos))));
    }

    // operator delete(this)
    ExprPtr again(Var::local("this", thisSlot));
    again->setType(self);
    const Type *vp = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr raw(new Cast(vp, std::move(again)));
    raw->setType(vp);
    StmtPtr freeIt(new ExprStmt(callAllocator("_ZdlPv", "??3@YAXPEAX@Z",
                                              types_.get(Kind::Void),
                                              std::move(raw), pos)));

    if (ms) {
        // if (flags & 1) operator delete(this);
        ExprPtr flags(Var::local("flags", flagSlot));
        flags->setType(flagType);
        ExprPtr one(new Num(1LL));
        one->setType(flagType);
        ExprPtr test(new Binary(BinOp::BitAnd, std::move(flags), std::move(one)));
        test->setType(flagType);
        body.push_back(StmtPtr(new If(std::move(test), std::move(freeIt), nullptr)));

        ExprPtr back(Var::local("this", thisSlot));
        back->setType(self);
        ExprPtr asVoid(new Cast(vp, std::move(back)));
        asVoid->setType(vp);
        body.push_back(StmtPtr(new Return(std::move(asVoid))));
    } else {
        body.push_back(std::move(freeIt));
        body.push_back(StmtPtr(new Return(nullptr)));
    }

    const Type *returns = ms ? vp : types_.get(Kind::Void);
    current_->functions.push_back(Function(cls + "::deleting", returns,
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos,
                                           std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    frameSize_ = savedFrame;
}

const Parser::Signature *Parser::destructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(destructorKey(cls->tag()));
    return set == nullptr ? nullptr : &functions_[(*set)[0]];
}

// One destructor call, given the address of what to destroy. A destructor
// takes nothing but `this`, so this is the smallest call the compiler makes.
ExprPtr Parser::destructorCall(ExprPtr address, const Signature &dtor,
                               std::size_t pos) {
    // Calling one is what asks for a body, which is the only thing that makes
    // an implicit destructor a function at all.
    markUsed(&dtor);
    std::vector<ExprPtr> args;
    args.push_back(std::move(address));
    std::vector<const Type *> params;
    params.push_back(args[0]->type());
    return completeCall("~" + dtor.owner, dtor.symbol, nullptr,
                        types_.get(Kind::Void), params, false, pos,
                        std::move(args));
}

// **RAII is this function**: everything constructed since `from` is destroyed, last
// first. **One region per stretch, and the stretches do not overlap** - which is what
// lets a call-site table hold them, and what the Microsoft chain walks backwards.
std::vector<StmtPtr> Parser::wrapMsCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    const int helpSlot = allocateFrameSlot(voidPtr);
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        std::vector<StmtPtr> guarded;
        for (std::size_t i = from; i < to; i++) guarded.push_back(std::move(body[i]));
        if (guarded.empty()) continue;

        std::vector<StmtPtr> steps;
        emitDestructors(steps, k == 0 ? aliveAtEntry : built[k - 1].second,
                        pos, -1, built[k].second);
        Block *b = new Block(std::move(steps));
        b->setScope(-1);

        Try *t = new Try(std::move(guarded), nullptr, pointerSlot,
                         selectorSlot, std::vector<std::string>());
        t->setCleanup(StmtPtr(b));
        t->setUnwindHelpSlot(helpSlot);
        out.push_back(StmtPtr(t));
    }
    return out;
}

std::vector<StmtPtr> Parser::wrapCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        std::vector<StmtPtr> guarded;
        for (std::size_t i = from; i < to; i++) guarded.push_back(std::move(body[i]));
        if (guarded.empty()) continue;
        out.push_back(StmtPtr(new Try(
            std::move(guarded),
            cleanupPad(aliveAtEntry, built[k].second, pointerSlot, pos),
            pointerSlot, selectorSlot, std::vector<std::string>())));
    }
    return out;
}

// **What an exception has to do on its way out of a scope.** The objects are the ones
// a `return` unwinds - `alive_` holds them and nothing new had to track them - and
// the difference is where the code runs from: a pad, ending in _Unwind_Resume.
StmtPtr Parser::cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                           std::size_t pos) {
    // **Bounded rather than truncated.** Resizing `alive_` down and back up would
    // default-construct what it had thrown away, and the second pad would then
    // destroy an object with no class - silently one destructor short.
    std::vector<StmtPtr> steps;
    emitDestructors(steps, from, pos, -1, to);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
    ptr->setType(voidPtr);
    args.push_back(std::move(ptr));
    steps.push_back(StmtPtr(new ExprStmt(
        runtimeCall("_Unwind_Resume", types_.get(Kind::Void), std::move(args)))));

    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

void Parser::emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                             std::size_t pos, int except, std::size_t to) {
    if (to > alive_.size()) to = alive_.size();
    for (std::size_t i = to; i > from; i--) {
        const Alive &a = alive_[i - 1];
        if (except >= 0 && a.offset == except && !a.byAddress) continue;
        destroyObject(into, a, pos);
    }
}

// One object's destructor call, for the end of a scope and for every jump
// that leaves one early - a goto fills these in after its label is known,
// which is why this takes the record and not an index into alive_.
void Parser::destroyObject(std::vector<StmtPtr> &into, const Alive &a,
                           std::size_t pos) {
    const Signature *dtor = destructorOf(a.cls);
    if (dtor == nullptr) return;

    ExprPtr addr;
    if (a.byAddress) {
        // The slot holds the caller's pointer, and that pointer IS the
        // object's address.
        addr = ExprPtr(Var::local(a.name, a.offset));
        addr->setType(types_.pointerTo(a.cls));
    } else {
        ExprPtr object(Var::local(a.name, a.offset));
        object->setType(a.cls);
        addr = ExprPtr(new Unary('&', std::move(object)));
        addr->setType(types_.pointerTo(a.cls));
    }
    into.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(addr),
                                                       *dtor, pos))));
}

// The vtable: one pointer per virtual function, in the order the base declared them,
// an override replacing an entry. **The two ABIs differ in the header** - Itanium 16
// bytes with a zero typeinfo, Microsoft none - and a thunk walks `this` back.
std::string Parser::synthesizeThunk(const std::string &cls, const Type *type,
                                    const VSlot &slot, int offset,
                                    std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const std::string name = ms
        ? slot.symbol + "$adj" + std::to_string(offset)
        // _ZThn16_N1C1gEv - the prefix, the offset, then the mangled name with
        // its own "_Z" removed and its N kept. substr(3) dropped the N and
        // gave _ZThn16_1C1gEv, which clang does not write.
        : "_ZThn" + std::to_string(offset) + "_" + slot.symbol.substr(2);

    const Type *self = types_.pointerTo(type);
    const int savedFrame = frameSize_;
    frameSize_ = 0;

    std::vector<Param> params;
    int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });
    std::vector<int> argSlots;
    for (std::size_t i = 0; i < slot.params.size(); i++)
        argSlots.push_back(allocateFrameSlot(slot.params[i]));
    for (std::size_t i = 0; i < slot.params.size(); i++)
        params.push_back(Param{ slot.params[i], argSlots[i] });

    // (C *)((char *)this - offset)
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(self);
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(me)));
    asChars->setType(chars);
    ExprPtr back(new Num(static_cast<long long>(-offset)));
    back->setType(types_.intType());
    ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(back)));
    moved->setType(chars);
    ExprPtr whole(new Cast(self, std::move(moved)));
    whole->setType(self);

    std::vector<ExprPtr> args;
    args.push_back(std::move(whole));
    std::vector<const Type *> full;
    full.push_back(self);
    for (std::size_t i = 0; i < slot.params.size(); i++) {
        ExprPtr a(Var::local("a" + std::to_string(i), argSlots[i]));
        a->setType(slot.params[i]);
        args.push_back(std::move(a));
        full.push_back(slot.params[i]);
    }

    const Signature *target = nullptr;
    if (const std::vector<std::size_t> *set = overloadsOf(cls + "::" + slot.name))
        for (std::size_t k = 0; k < set->size(); k++)
            if (functions_[(*set)[k]].symbol == slot.symbol) target = &functions_[(*set)[k]];
    const Type *returns = target != nullptr ? target->returns : types_.get(Kind::Void);

    // A thunk forwards a member call and carries the same `this`, which is
    // what decides where the Microsoft ABI puts a hidden return pointer.
    ExprPtr call = completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args), true);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new Return(returns->isVoid() ? nullptr : std::move(call))));
    if (returns->isVoid()) body.insert(body.begin(), StmtPtr(new ExprStmt(std::move(call))));

    current_->functions.push_back(Function(name, returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(name);
    frameSize_ = savedFrame;
    return name;
}

void Parser::markSymbolUsed(const std::string &symbol) {
    if (symbol.empty()) return;
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].symbol == symbol) { functions_[i].used = true; return; }
}

void Parser::markUsed(const Signature *f) {
    functions_[static_cast<std::size_t>(f - &functions_[0])].used = true;
}

bool Parser::sameParameters(const std::vector<const Type *> &a,
                            const std::vector<const Type *> &b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i]) return false;
    return true;
}

bool Parser::memberFromBase(const Type *cls, const Member &m) {
    const std::vector<Type::BaseSpec> &bs = cls->bases();
    for (std::size_t k = 0; k < bs.size(); k++)
        if (m.offset >= bs[k].offset &&
            m.offset < bs[k].offset + bs[k].type->dataSize())
            return true;
    return false;
}

// **[dcl.init]/7 refuses a const object that nothing would initialise**, and the
// line is CWG 253's rather than the paragraph's letter - which is what clang
// applies and what this was measured against: `const S s;` is refused for a
// plain struct and accepted where every member has an initialiser of its own.
bool Parser::constDefaultInitialisable(const Type *t) const {
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (!u->isStructOrUnion()) return false;      // `const int n;` and its kind

    // A constructor somebody wrote initialises whatever it means to; one the
    // compiler wrote initialises only what the members ask for, which is the
    // whole of this question.
    if (const Signature *ctor = defaultConstructorOf(u))
        if (!ctor->implicit) return true;

    const std::vector<Type::BaseSpec> &bs = u->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (!constDefaultInitialisable(bs[i].type)) return false;

    const std::vector<Member> &ms = u->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (memberFromBase(u, ms[i])) continue;   // its own base answered for it
        if (memberInit_.count(u->tag() + "::" + ms[i].name)) continue;
        if (!constDefaultInitialisable(ms[i].type)) return false;
    }
    return true;
}

void Parser::requireConstInitialised(const Type *t, const std::string &name,
                                     std::size_t pos) {
    if (!t->isConst() || constDefaultInitialisable(t)) return;
    const Type *u = t->unqualified();
    while (u->isArray()) u = u->pointee()->unqualified();
    if (u->isStructOrUnion())
        // The suggestion is spelled with the tag and not with describe(), which
        // says "struct S" - and `struct S()` is not something anybody can write.
        src_.fail(pos, "'" + name + "' is const and nothing here would "
                       "initialise it - '" + u->describe() + "' has no "
                       "constructor of its own and leaves a member unset. Write "
                       "'const " + u->tag() + " " + name + " = " +
                       u->tag() + "();'");
    src_.fail(pos, "'" + name + "' is const and has no initialiser, so it would "
                   "hold whatever was there - [dcl.init]/7 refuses that. Give it "
                   "a value where it is declared");
}

ExprPtr Parser::thisMember(int thisSlot, const Type *cls, const Member &m) {
    ExprPtr me(Var::local("this", thisSlot));
    me->setType(types_.pointerTo(cls));
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(cls);
    ExprPtr acc(new MemberAccess(std::move(obj), m.name, m.offset,
                                 m.width, m.bitOffset));
    acc->setType(m.type);
    return acc;
}

// **The type_info beside a vtable, and the name string beside that.** Two
// objects per class and a third slot filled: `_ZTS4Base` holds the text "4Base",
// `_ZTI4Base` points at it behind a vtable pointer that says which *kind* of
// type_info this is, and the vtable's second word - a plain zero until now -
// points at `_ZTI`. Measured from clang: a class with no base is
// `__class_type_info`, one with a single public base is `__si_class_type_info`
// and carries the base's `_ZTI` as a third word, and both are the library's
// objects reached at +16, past their own two header words.
//
// The bases are walked, not just named: a chain's every link needs its own pair
// or the runtime has nothing to walk, which is why this recurses.
std::string Parser::emitClassTypeInfo(const Type *cls, const std::string &tag,
                                      std::size_t pos) {
    const std::string ti = itaniumClassTypeInfoSymbol(tag);
    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == ti) return ti;      // one per class

    // **A class this cannot describe gets none, and that is not an error here.**
    // More than one base wants `__vmi_class_type_info`, a third shape carrying
    // the bases' offsets and flags, which is not built. Such a class kept
    // working before there was any type_info at all, so it keeps working now:
    // the vtable's slot stays the zero it always held, and the only thing that
    // cannot be done is a `dynamic_cast` naming it - which is refused there, by
    // name, where the reader is asking for the thing that is missing.
    const std::vector<Type::BaseSpec> &bases = cls->bases();
    if (bases.size() > 1) return std::string();

    // **The base first, and nothing is laid down until it answers.** A chain is
    // only as describable as its links - with no `_ZTI` for the base there is
    // nothing to point the third word at - and giving up after emitting the
    // name string would leave a `_ZTS` in the object that nothing refers to.
    std::string baseTypeInfo;
    if (!bases.empty()) {
        baseTypeInfo = emitClassTypeInfo(bases[0].type, bases[0].type->tag(), pos);
        if (baseTypeInfo.empty()) return std::string();
    }

    const std::string ts = itaniumClassTypeNameSymbol(tag);
    const std::string text = itaniumClassNameString(tag);
    std::vector<GlobalPiece> letters;
    for (std::size_t i = 0; i <= text.size(); i++)             // the NUL too
        letters.push_back(GlobalPiece{ static_cast<int>(i), 1,
                                       i < text.size() ? text[i] : 0,
                                       std::string() });
    const Type *chars = types_.arrayOf(types_.get(Kind::Char),
                                       static_cast<long long>(text.size() + 1));
    current_->globals.push_back(Global{ ts, ts, chars, std::move(letters),
                                        true, false, true });

    std::vector<GlobalPiece> pieces;
    pieces.push_back(GlobalPiece{
        0, 8, 16,
        bases.empty() ? "_ZTVN10__cxxabiv117__class_type_infoE"
                      : "_ZTVN10__cxxabiv120__si_class_type_infoE" });
    pieces.push_back(GlobalPiece{ 8, 8, 0, ts });
    if (!baseTypeInfo.empty())
        pieces.push_back(GlobalPiece{ 16, 8, 0, baseTypeInfo });

    const Type *word = types_.pointerTo(types_.get(Kind::Void));
    const Type *object = types_.arrayOf(word,
                                        static_cast<long long>(pieces.size()));
    current_->globals.push_back(Global{ ti, ti, object, std::move(pieces),
                                        true, false, true });
    return ti;
}

void Parser::emitVtable(const Type *cls, const std::string &tag,
                        std::size_t pos) {
    if (tag.empty())
        src_.fail(pos, "a class with a virtual function needs a name - its "
                       "vtable is a symbol, and an anonymous class has none");

    const std::vector<VSlot> &slots = vtables_[tag];
    const bool ms = target_.microsoftNames();
    const std::string symbol = vtableSymbol(tag, ms);

    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == symbol) return;   // one per class

    // **The table holding a function's address is a use of it.** The `used` flag came
    // only from calls, so a class with an implicit virtual destructor got a table
    // pointing at a `~D` nothing emitted. Marked during the class's own completion.
    for (std::size_t i = 0; i < slots.size(); i++)
        markSymbolUsed(slots[i].symbol);
    // **And the destructor itself, which the Microsoft table does not name.** Itanium
    // has two slots, so marking the slots covers both; MSVC has one holding the
    // deleting destructor, whose body calls an ordinary one nothing else names.
    if (const Signature *dtor = destructorOf(cls)) markSymbolUsed(dtor->symbol);

    // **The typeinfo slot is filled now**, where it held a plain zero. Itanium
    // only: the Microsoft ABI puts a complete-object locator in front of the
    // table instead, and that is its own measurement.
    const std::string typeInfo = ms ? std::string()
                                    : emitClassTypeInfo(cls, tag, pos);

    std::vector<GlobalPiece> pieces;
    int at = 0;
    if (!ms) {
        pieces.push_back(GlobalPiece{ at, 8, 0, std::string() });  // offset-to-top
        at += 8;
        pieces.push_back(GlobalPiece{ at, 8, 0, typeInfo });       // typeinfo
        at += 8;
    }
    for (std::size_t i = 0; i < slots.size(); i++) {
        pieces.push_back(GlobalPiece{ at, 8, 0, slots[i].symbol });
        at += 8;
    }

    // **A secondary table for every polymorphic base after the first**, laid down
    // behind the primary one in the same symbol - _ZTV1C holds both, the second
    // beginning with an offset-to-top of -16. An override there is a thunk.
    const std::vector<Type::BaseSpec> &bases = cls->bases();
    for (std::size_t bi = 1; bi < bases.size(); bi++) {
        const Type *b = bases[bi].type;
        if (!b->polymorphic()) continue;
        const int off = bases[bi].offset;

        // **The Microsoft ABI arranges this differently, and it is not the same thing
        // under other names.** Measured with clang: two vftable symbols rather than
        // one table in two parts, and no thunk. Whether cl agrees is unmeasured.
        if (ms)
            src_.fail(pos, "'" + tag + "' has virtual functions in a base that "
                           "is not the first, and the Microsoft ABI lays that "
                           "out differently - two vftable symbols rather than "
                           "one table in two parts. Not supported yet; it is "
                           "measured for Itanium only");
        secondaryVptr_[tag + "::" + b->tag()] = at + (ms ? 0 : 16);

        if (!ms) {
            pieces.push_back(GlobalPiece{ at, 8, -static_cast<long long>(off),
                                          std::string() });
            at += 8;
            // A secondary table names the *complete* object's type_info, the
            // same one the primary does - it is one object with two tables in
            // it, not two objects.
            pieces.push_back(GlobalPiece{ at, 8, 0, typeInfo });
            at += 8;
        }
        const std::vector<VSlot> &theirs = vtables_[b->tag()];
        for (std::size_t i = 0; i < theirs.size(); i++) {
            std::string entry = theirs[i].symbol;
            // Did this class override it? Its own slot list has the answer.
            for (std::size_t k = 0; k < slots.size(); k++) {
                if (!overrides(slots[k], theirs[i].name, theirs[i].params,
                               theirs[i].constThis)) continue;
                if (slots[k].symbol != theirs[i].symbol)
                    entry = synthesizeThunk(tag, cls, slots[k], off, pos);
                break;
            }
            pieces.push_back(GlobalPiece{ at, 8, 0, entry });
            at += 8;
        }
    }

    // **The Microsoft locator goes in front of the table, not behind it.** A
    // class with more than one base has no description this compiler can write
    // - the same limit the Itanium half has - so it gets no locator and its
    // table is what it always was; only a `dynamic_cast` naming it is refused.
    std::string locatorWord;
    if (ms && cls->bases().size() <= 1) {
        MicrosoftRtti names;
        std::string why;
        if (microsoftClassRttiNames(cls, &names, &why)) {
            locatorWord = names.locator;
            bool had = false;
            for (std::size_t i = 0; i < current_->rtti.size(); i++)
                if (current_->rtti[i] == cls) had = true;
            if (!had) current_->rtti.push_back(cls);
        }
    }

    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *table = types_.arrayOf(entry, static_cast<long long>(pieces.size()));
    current_->globals.push_back(Global{ symbol, symbol, table, std::move(pieces),
                                        true, false, true, locatorWord });
}

// A constructor, read at the point its '(' was seen: a member function whose name is
// the class and whose return type is nothing at all, keyed under "Point::Point" so
// every piece of overload machinery applies to it unchanged.
void Parser::declareConstructor(const std::string &cls, std::size_t pos,
                                Access access, bool isExplicit) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (variadic)
        src_.fail(pos, "a constructor cannot take '...'");
    // Read here rather than at the call site: this is where the parameter list
    // was consumed, so this is where what follows it can be seen.
    pendingNoexcept_ = exceptionSpecification();

    // A constructor returns nothing, and saying so as void is what lets the
    // rest of the compiler treat the call like any other.
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);

    std::string key = constructorKey(cls);
    std::vector<std::size_t> &set = functionIndex_[key];
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (sameParameters(f.params, params))
            src_.fail(pos, "'" + cls + "::" + cls + "' is declared twice");
    }

    const char code = access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftConstructorName(cls, findTypedef(cls), fn, code, &out, &why)
            : itaniumConstructorName(cls, findTypedef(cls), fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + cls + "' cannot be given a name the "
                       "linker can hold: " + why);

    set.push_back(functions_.size());
    if (!pendingDefaults_.empty()) defaultArgs_[out] = pendingDefaults_;
    pendingDefaults_.clear();
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
    functions_.back().isExplicit = isExplicit;
    functions_.back().isNoexcept = pendingNoexcept_;
    pendingNoexcept_ = false;
}

// The class a member is built from: the element type when the member is an
// array, and nothing at all when it is not of class type.
static const Type *memberClass(const Type *t) {
    while (t != nullptr && t->isArray()) t = t->pointee();
    return (t != nullptr && t->isStructOrUnion()) ? t->unqualified() : nullptr;
}

// One element of an array member, by address: the member's own address, decayed, plus
// the index times the element's size. **In bytes, and deliberately** - a Binary built
// here is not the parser's pointer arithmetic and gets none of its scaling.
static ExprPtr indexBytes(TypeTable &types, ExprPtr decayed, const Type *elem,
                          int indexSlot, const Target &target) {
    const Type *idx = types.intType();
    ExprPtr i(Var::local("$i", indexSlot));
    i->setType(idx);
    ExprPtr size(new Num(static_cast<long long>(elem->size(target))));
    size->setType(idx);
    ExprPtr off(new Binary(BinOp::Mul, std::move(i), std::move(size)));
    off->setType(idx);
    const Type *ptr = types.pointerTo(elem);
    ExprPtr at(new Binary(BinOp::Add, std::move(decayed), std::move(off)));
    at->setType(ptr);
    return at;
}

// `S a[4];` where S has constructors - the default constructor once per element, in
// a loop. **This was the one construction that silently did not happen**: an array is
// not a struct, so it fell through to an uninitialised local. Every level at once.
StmtPtr Parser::constructLocalArray(const Declared &d, int offset,
                                    int indexSlot) {
    const Type *elem = d.type;
    long long count = 1;
    while (elem->isArray()) { count *= elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();

    const Signature *ctor = defaultConstructorOf(plain);
    if (ctor == nullptr)
        src_.fail(d.pos, "'" + plain->describe() + "' has constructors but none "
                         "that takes nothing, and an array of it has no way to "
                         "say what to pass");
    if (ctor->access != Access::Public && currentClass_ != plain &&
        !isFriendOf(plain))
        src_.fail(d.pos, "'" + plain->describe() + "' has no public default "
                         "constructor, and an array of it needs one");
    // **Marked used, or an implicit one is declared and never emitted.** Every other
    // path to a constructor goes through `resolveOverload`, which marks it; this one
    // looks the default up directly. `S a[2];` called `S::S()` and nothing defined it.
    markUsed(ctor);
    // **Copied before the defaults are read**: reading one can parse an expression
    // that grows `functions_` under the pointer just taken into it. The defaults sit
    // inside the statement the loop repeats, so they are evaluated once per element.
    const Signature chosen = *ctor;
    std::vector<ExprPtr> defaults;
    applyDefaults(chosen, defaults, d.pos);

    const Type *ptr = types_.pointerTo(plain);
    ExprPtr base(Var::local(d.name, offset));
    base->setType(d.type);
    ExprPtr at = indexBytes(types_, decay(std::move(base)), plain, indexSlot,
                            target_);

    std::vector<ExprPtr> args;
    args.push_back(std::move(at));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    for (std::size_t i = 0; i < defaults.size(); i++) {
        args.push_back(std::move(defaults[i]));
        ps.push_back(chosen.params[i]);
    }

    StmtPtr one(new ExprStmt(completeCall(plain->tag(), chosen.symbol, nullptr,
                                          types_.get(Kind::Void), ps, false,
                                          d.pos, std::move(args))));
    return eachElement(indexSlot, count, std::move(one));
}

// **A default constructor is one that can be called with no arguments, not one whose
// parameter list is empty** - [class.ctor]/5, so `S(int a = 1)` is one. Whoever calls
// this still supplies the defaults; two that both take nothing answer nullptr.
const Parser::Signature *Parser::defaultConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    const Signature *found = nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (leastArguments(f) != 0) continue;
        if (found != nullptr) return nullptr;
        found = &f;
    }
    return found;
}

const Parser::Signature *Parser::copyConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isReference()) continue;
        // **`S(S &&)` is not a copy constructor**, and until rung 7 there was no way
        // to write one, so isReference() alone was enough. Answering one here would
        // hand an lvalue to a constructor whose whole contract is that it gets none.
        if (f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

// `S(S &&)`, written by hand - the compiler does not yet write one. The
// mirror of copyConstructorOf and told apart from it by exactly one thing,
// which is the kind of reference the parameter is.
const Parser::Signature *Parser::moveConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isRValueReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

const Parser::Signature *Parser::copyAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(assignmentKey(cls->tag()));
    return set == nullptr ? nullptr : &functions_[(*set)[0]];
}

std::string Parser::baseConstructorSymbol(const Signature &ctor, const Type *base) {
    if (target_.microsoftNames()) return ctor.symbol;
    const Type *fnType = types_.functionType(types_.get(Kind::Void), ctor.params,
                                             false);
    std::string sub, why;
    if (itaniumConstructorName(base->tag(), base, fnType, false, &sub, &why))
        return sub;
    return ctor.symbol;
}

// **A trivial special member is not a function**, and that is measured rather than
// reasoned: cl and clang emit no symbol for one with no work to do. So an implicit
// member is declared only where it has some - a vptr, or a base or member to build.
void Parser::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty() || type->kind() == Kind::Union) return;
    // Asked before the copy constructor is declared, because declaring one would
    // answer it yes. A class that writes any constructor gets no implicit default
    // one, and still gets an implicit copy constructor.
    const bool wroteConstructor = overloadsOf(constructorKey(tag)) != nullptr;
    // **Read now, for the same reason and at the same moment.** After the three calls
    // below, every one of these answers yes for a class that wrote nothing at all,
    // and [class.copy]/9 is a question about what the *user* declared.
    const bool wroteCopyOrDtor = copyConstructorOf(type) != nullptr ||
                                 moveConstructorOf(type) != nullptr ||
                                 overloadsOf(destructorKey(tag)) != nullptr;
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    // Before the `wroteConstructor` return below: writing a constructor of
    // your own costs you the implicit *default* one and nothing else.
    declareImplicitMoveCtor(tag, type, pos, wroteCopyOrDtor);
    if (wroteConstructor) return;

    // **An initialiser on a member is work**, and this is where a class with nothing
    // but `int x = 5;` gets a default constructor at all: without one there is no
    // function to put the store in, and `S s;` would leave x holding the stack.
    bool work = type->polymorphic();
    for (std::size_t i = 0; i < type->members().size() && !work; i++)
        if (memberInit_.find(tag + "::" + type->members()[i].name) !=
            memberInit_.end())
            work = true;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (!bs[i].type->tag().empty() &&
            overloadsOf(constructorKey(bs[i].type->tag())) != nullptr)
            work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc != nullptr && !mc->tag().empty() &&
            overloadsOf(constructorKey(mc->tag())) != nullptr)
            work = true;
    }
    if (!work) return;

    const std::vector<const Type *> params;
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a default constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
}

// **The destructor the class did not write**, which becomes a function exactly when a
// base or a member has one of its own to run - measured with cl. **A virtual function
// does not make it non-trivial**; a base whose destructor is virtual makes it virtual.
void Parser::declareImplicitDestructor(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(destructorKey(tag)) != nullptr) return;

    bool work = false;
    bool isVirtual = false;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (const Signature *d = destructorOf(bs[i].type)) {
            work = true;
            if (d->isVirtual) isVirtual = true;
        }
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (destructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    registerDestructor(tag, pos, Access::Public, isVirtual, true);
}

// Its body: the members this class added, in the reverse of the order they were
// declared, then the bases in the reverse of theirs. A base's own destructor deals
// with the members it brought, which is why they are skipped here.
void Parser::synthesizeDestructor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const bool isVirtual = functions_[which].isVirtual;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    const std::vector<Member> &ms = type->members();

    for (std::size_t n = ms.size(); n-- > 0; ) {
        if (memberFromBase(type, ms[n])) continue;

        const Type *mt = ms[n].type;
        const Type *elem = mt->isArray() ? mt->pointee() : mt;
        const Signature *dtor = destructorOf(memberClass(mt));
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of '" +
                           memberClass(mt)->tag() + "', the type of '" +
                           ms[n].name + "', is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = mt->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[n].name + "' has no length, "
                               "so the destructor the compiler would write does "
                               "not know how many elements to destroy");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        ExprPtr acc = thisMember(thisSlot, type, ms[n]);

        ExprPtr address;
        if (mt->isArray()) {
            // **Backwards**, because an array is destroyed in the reverse of
            // the order it was built: the index counts up and the element it
            // reaches is (count - 1 - i).
            const Type *idx = types_.intType();
            ExprPtr last(new Num(count - 1));
            last->setType(idx);
            ExprPtr i(Var::local("$i", indexSlot));
            i->setType(idx);
            ExprPtr back(new Binary(BinOp::Sub, std::move(last), std::move(i)));
            back->setType(idx);
            ExprPtr size(new Num(static_cast<long long>(elem->size(target_))));
            size->setType(idx);
            ExprPtr off(new Binary(BinOp::Mul, std::move(back), std::move(size)));
            off->setType(idx);
            const Type *ptr = types_.pointerTo(elem->unqualified());
            ExprPtr at(new Binary(BinOp::Add, decay(std::move(acc)),
                                  std::move(off)));
            at->setType(ptr);
            address = std::move(at);
        } else {
            address = ExprPtr(new Unary('&', std::move(acc)));
            address->setType(types_.pointerTo(elem->unqualified()));
        }

        StmtPtr one(new ExprStmt(destructorCall(std::move(address), *dtor, pos)));
        body.push_back(mt->isArray()
                       ? eachElement(indexSlot, count, std::move(one))
                       : std::move(one));
    }

    for (std::size_t n = bs.size(); n-- > 0; ) {
        const Type *base = bs[n].type;
        const Signature *dtor = destructorOf(base);
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of its "
                           "base '" + base->tag() + "' is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));
        // The base-subobject form, D2, which is what a derived class calls -
        // the same name a written destructor reaches for.
        std::string sym = dtor->symbol;
        if (!target_.microsoftNames())
            itaniumDestructorName(base->tag(), base, false, &sym);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[n].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall("~" + base->tag(), sym, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args)))));
    }

    current_->functions.push_back(Function(cls + "::~" + localOf(cls),
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    if (!target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(cls, type, false, &d2);
        current_->functions.back().setAlias(d2);
    }
    frameSize_ = savedFrame;

    // A virtual one carries the deleting form into the vtable beside it, the
    // same as a written virtual destructor does.
    if (isVirtual) synthesizeDeleting(cls, type, Access::Public, pos);
}

// The copy assignment operator the class did not write. The trivial line is drawn
// where the others are and measured the same way with cl. A polymorphic class is
// non-trivial even though the body leaves the vptr alone: it writes into its own.
void Parser::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(assignmentKey(tag)) != nullptr) return;
    // [class.copy]/23: a user-declared move constructor **deletes** the implicit copy
    // assignment, the same sentence that deletes the implicit copy constructor two
    // rules earlier. Called before the implicit move, so this sees what the user wrote.
    if (moveConstructorOf(type) != nullptr) return;

    // **A const member has no assignment to give**, so the operator the compiler would
    // write is deleted rather than non-trivial and none is declared. Asked over every
    // member first: the search below stops at the first that gives it work.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyAssignOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyAssignOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *self = types_.referenceTo(type);
    const Type *fn = types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy assignment the compiler would "
                       "write, and it cannot be given a name the linker can "
                       "hold: " + why);

    functionIndex_[assignmentKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public,
                                    false });
    functions_.back().implicit = true;
}

// The copy constructor the class did not write, on the same measured line: a class
// writing any constructor still gets one and only writing a copy takes it away. The
// implicit *move* needs all five of [class.copy]/9's absences, two of them vacuous.
void Parser::declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                     std::size_t pos, bool userDeclared) {
    if (userDeclared) return;
    if (moveConstructorOf(type) != nullptr) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (moveConstructorOf(bs[i].type) != nullptr ||
            copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (moveConstructorOf(mc) != nullptr ||
            copyConstructorOf(mc) != nullptr) work = true;
    }
    if (!work) return;

    std::vector<const Type *> params;
    // Not const, and that is the whole point of it: the source is going to be
    // taken apart, so the member moves below need to be able to write to it.
    params.push_back(types_.rvalueReferenceTo(type));
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a move constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
}

void Parser::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;
    // [class.copy]/7: a user-declared move constructor **deletes** the implicit copy
    // constructor. Not declaring one is how that is said here, and the effect is the
    // same - a copy is refused, and the message shows the move that took its place.
    if (moveConstructorOf(type) != nullptr) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyConstructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
}

// The body of a default constructor nobody wrote: the bases in the order they were
// written, then the vptrs, then the members with constructors of their own. Scalars
// are left alone, which is what [dcl.init] means by default-initialisation.
void Parser::synthesizeDefaultCtor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;
        if (overloadsOf(constructorKey(base->tag())) == nullptr) continue;
        const Signature *ctor = defaultConstructorOf(base);
        if (ctor == nullptr)
            src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its base '" +
                           base->tag() + "', which has no constructor taking "
                           "nothing - write a constructor for '" + cls + "' with "
                           "': " + base->tag() + "(...)' in its initialiser list");
        if (ctor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of its base '" +
                           base->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        markUsed(ctor);
        // Copied before the defaults are read - see constructLocalArray.
        const Signature chosen = *ctor;
        std::vector<ExprPtr> defaults;
        applyDefaults(chosen, defaults, pos);
        const std::string sym = baseConstructorSymbol(chosen, base);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        for (std::size_t k = 0; k < defaults.size(); k++) {
            args.push_back(std::move(defaults[k]));
            ps.push_back(chosen.params[k]);
        }
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, types_.get(Kind::Void), ps,
                         false, pos, std::move(args)))));
    }

    if (type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    // The members, in declaration order, each by the one rule that applies to it: the
    // initialiser the class wrote on it, or else its own default constructor. **One
    // walk, not two** - `M m = M(2);` used to be stored and then built over.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        StmtPtr one = memberInitialiser(cls, type, ms[i], thisSlot, pos);
        std::vector<ExprPtr> none;
        if (one == nullptr && type->kind() != Kind::Union)
            one = constructMember(cls, type, ms[i], thisSlot, none, pos, true);
        if (one != nullptr) body.push_back(std::move(one));
    }

    current_->functions.push_back(Function(cls + "::" + cls, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    // The same two names a written constructor is emitted under: C1 for a
    // complete object and C2 for a base subobject, the second a label in front
    // of the first. Microsoft has one name and wants no alias.
    if (!target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(), false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why))
            current_->functions.back().setAlias(c2);
    }
    frameSize_ = savedFrame;
}

StmtPtr Parser::eachElement(int indexSlot, long long count, StmtPtr one) {
    const Type *idx = types_.intType();

    ExprPtr i0(Var::local("$i", indexSlot));
    i0->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr init(new Assign(std::move(i0), std::move(zero)));
    init->setType(idx);

    ExprPtr i1(Var::local("$i", indexSlot));
    i1->setType(idx);
    ExprPtr n(new Num(count));
    n->setType(idx);
    ExprPtr cond(new Binary(BinOp::Lt, std::move(i1), std::move(n)));
    cond->setType(idx);

    ExprPtr i2(Var::local("$i", indexSlot));
    i2->setType(idx);
    ExprPtr step1(new Num(1LL));
    step1->setType(idx);
    ExprPtr sum(new Binary(BinOp::Add, std::move(i2), std::move(step1)));
    sum->setType(idx);
    ExprPtr i3(Var::local("$i", indexSlot));
    i3->setType(idx);
    ExprPtr step(new Assign(std::move(i3), std::move(sum)));
    step->setType(idx);

    std::vector<StmtPtr> inner;
    inner.push_back(std::move(one));
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));

    std::vector<StmtPtr> all;
    all.push_back(StmtPtr(new ExprStmt(std::move(init))));
    all.push_back(StmtPtr(new While(std::move(cond),
                                    StmtPtr(new Block(std::move(inner))))));
    return StmtPtr(new Block(std::move(all)));
}


// The body of a copy constructor nobody wrote: the bases with one of their own, then
// the vptrs, then every member no base copied. **The vptr is set and not copied** -
// measured in cl's listing. Members go one at a time, for the tail-padding rule.
void Parser::synthesizeCopy(std::size_t which, bool assigning) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *srcRef = functions_[which].params[0];
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    // **The signature says which of the three this is**, so nothing that calls
    // this had to learn about moves: an implicit constructor whose parameter
    // is `X &&` is the move constructor and there is nothing else it could be.
    const bool moving = srcRef->isRValueReference();
    const char *kind = assigning ? "copy assignment"
                     : moving    ? "move constructor"
                                 : "copy constructor";

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    const Type *srcPtr = types_.pointerTo(srcRef->referent());
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    const int thatSlot = allocateFrameSlot(srcPtr);
    params.push_back(Param{ self, thisSlot });
    params.push_back(Param{ srcPtr, thatSlot });

    std::vector<StmtPtr> body;

    // What a base's own copy constructor has already dealt with. Its members
    // are in this class's member list too - they were copied down - and
    // copying them again would run past a base that did the work itself.
    std::vector<std::pair<int, int> > taken;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        // **A member or base without a move constructor is copied, not refused.**
        // [class.copy]/15: the implicit move moves each subobject, and moving
        // something that has only a copy is what its copy constructor does.
        const Signature *cc = nullptr;
        if (assigning) cc = copyAssignOf(base);
        else {
            if (moving) cc = moveConstructorOf(base);
            if (cc == nullptr) cc = copyConstructorOf(base);
        }
        if (cc == nullptr) continue;              // trivial: its members copy below
        if (cc->access != Access::Public)
            src_.fail(pos, std::string("'") + cls + "' cannot be built by the " +
                           kind + " the compiler would write: the " + kind +
                           " of its base '" + base->tag() + "' is " +
                           (cc->access == Access::Private ? "private" : "protected"));
        markUsed(cc);
        const std::string sym = assigning ? cc->symbol
                                          : baseConstructorSymbol(*cc, base);
        const Type *basePtr = types_.pointerTo(base);

        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        from = convert(std::move(from),
                       types_.pointerTo(moving ? base : types_.withConst(base)));
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(base);
        if (moving) fromObj->setXvalue();

        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        args.push_back(std::move(fromObj));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        ps.push_back(cc->params[0]);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, cc->returns, ps,
                         false, pos, std::move(args)))));
        taken.push_back(std::make_pair(bs[i].offset,
                                       bs[i].offset + base->dataSize()));
    }

    // **A copy constructor sets the vptr; a copy assignment leaves it alone.** That is
    // the whole difference between the two bodies, measured in cl's own listing:
    // assignment writes into an object that already exists and is already this class.
    if (!assigning && type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        bool done = false;
        for (std::size_t k = 0; k < taken.size() && !done; k++)
            if (ms[i].offset >= taken[k].first && ms[i].offset < taken[k].second)
                done = true;
        if (done) continue;

        const Type *mt = ms[i].type;
        const Type *elem = mt->isArray() ? mt->pointee() : mt;
        const Signature *cc = nullptr;
        if (assigning) cc = copyAssignOf(memberClass(mt));
        else {
            if (moving) cc = moveConstructorOf(memberClass(mt));
            if (cc == nullptr) cc = copyConstructorOf(memberClass(mt));
        }
        if (cc != nullptr) {
            if (cc->access != Access::Public)
                src_.fail(pos, std::string("'") + cls + "' cannot be built by "
                               "the " + kind + " the compiler would write: the " +
                               kind + " of '" + memberClass(mt)->tag() +
                               "', the type of '" + ms[i].name + "', is " +
                               (cc->access == Access::Private ? "private"
                                                              : "protected"));
            markUsed(cc);
        }

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = mt->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the copy constructor the compiler would write "
                               "does not know how much to copy");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        // Both sides of the copy, as lvalues: this->m and that->m, or one
        // element of each when the member is an array.
        ExprPtr dst = thisMember(thisSlot, type, ms[i]);

        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(srcRef->referent());
        if (moving) fromObj->setXvalue();
        ExprPtr src(new MemberAccess(std::move(fromObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        src->setType(mt);

        if (mt->isArray()) {
            ExprPtr dstAt = indexBytes(types_, decay(std::move(dst)), elem,
                                       indexSlot, target_);
            ExprPtr srcAt = indexBytes(types_, decay(std::move(src)), elem,
                                       indexSlot, target_);
            dst = ExprPtr(new Unary('*', std::move(dstAt)));
            dst->setType(elem);
            src = ExprPtr(new Unary('*', std::move(srcAt)));
            src->setType(elem);
        }

        // After the array unwrap, so that it lands on the element actually handed
        // over. `static_cast<T &&>(other.m)` for every member is what [class.copy]/15
        // says the body is - harmless on a scalar, where the move is an assignment.
        if (moving) src->setXvalue();

        StmtPtr one;
        if (cc != nullptr) {
            ExprPtr addr(new Unary('&', std::move(dst)));
            addr->setType(types_.pointerTo(elem->unqualified()));
            std::vector<ExprPtr> args;
            args.push_back(std::move(addr));
            args.push_back(std::move(src));
            std::vector<const Type *> ps;
            ps.push_back(types_.pointerTo(elem->unqualified()));
            ps.push_back(cc->params[0]);
            one = StmtPtr(new ExprStmt(
                completeCall(elem->unqualified()->tag(), cc->symbol, nullptr,
                             cc->returns, ps, false, pos, std::move(args))));
        } else {
            ExprPtr store(new Assign(std::move(dst), std::move(src)));
            store->setType(elem);
            one = StmtPtr(new ExprStmt(std::move(store)));
        }

        body.push_back(mt->isArray() ? eachElement(indexSlot, count, std::move(one))
                                     : std::move(one));
    }

    // **`a = b` is an expression and has to have a value**, and the value is the object
    // assigned to. The declared return type is `X &`, and a reference is a pointer
    // everywhere below the parser, so what the function actually returns is `this`.
    const Type *returns = types_.get(Kind::Void);
    if (assigning) {
        returns = types_.pointerTo(type);
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new Return(std::move(me))));
    }

    current_->functions.push_back(Function(cls + "::" + (assigning ? "operator="
                                                                  : cls),
                                           returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    // A constructor is emitted under both of Itanium's names; an operator has
    // one name in either ABI.
    if (!assigning && !target_.microsoftNames()) {
        std::vector<const Type *> ps;
        ps.push_back(srcRef);
        const Type *fnType = types_.functionType(types_.get(Kind::Void), ps, false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why))
            current_->functions.back().setAlias(c2);
    }
    frameSize_ = savedFrame;
}

// **To a fixed point, because a body can be what first calls another.** Giving
// Owner its constructor is what calls Held's, and Held's may not have been
// wanted by anything the program wrote.
void Parser::defineImplicitFunctions() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < functions_.size(); i++) {
            if (!functions_[i].implicit || !functions_[i].used ||
                functions_[i].defined)
                continue;
            functions_[i].defined = true;
            if (!functions_[i].name.empty() && functions_[i].name[0] == '~')
                                                    synthesizeDestructor(i);
            else if (functions_[i].name == "operator=") synthesizeCopy(i, true);
            else if (functions_[i].params.empty())   synthesizeDefaultCtor(i);
            else                                    synthesizeCopy(i, false);
            again = true;
        }
    }
}

std::string Parser::staticMemberSymbol(const std::string &cls,
                                       const std::string &name, const Type *t,
                                       Access access, std::size_t pos) {
    if (!target_.microsoftNames()) return itaniumStaticMemberName(cls, findTypedef(cls), name);
    // Microsoft writes the access as a digit where a member function writes a letter,
    // so a static member that changes from private to public changes its symbol on
    // Windows and keeps it on Linux - the same asymmetry, measured the same way.
    const char code = access == Access::Public    ? '2'
                    : access == Access::Protected ? '1'
                                                  : '0';
    std::string out, why;
    if (!microsoftStaticMemberName(cls, findTypedef(cls), name, t, code, &out, &why))
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// `static int total;` inside a class: one object shared by every object of the class,
// taking no room in any of them, so nothing here touches the layout. What it needs is
// a name the linker can hold and a definition outside the class to go with it.
void Parser::declareStaticMember(const std::string &cls, Type *owner,
                                 const Declared &d, Access access) {
    if (cls.empty())
        src_.fail(d.pos, "a static member needs a class with a name - this one "
                         "is anonymous");
    if (owner->findMember(d.name) != nullptr)
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' is a static member and "
                         "an ordinary one, and it can only be one of them");
    for (const Type::StaticMember &had : owner->staticMembers())
        if (had.name == d.name)
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is declared twice");

    Type::StaticMember s;
    s.name = d.name;
    s.type = d.type;
    s.access = access;

    // **`static const int k = 5;` written in the class needs no definition**, measured
    // rather than assumed: cl emits no symbol for one and folds the value in wherever
    // it is read. Anything else with an initialiser here is refused.
    if (consume("=")) {
        if (!d.type->isConst() || !d.type->isInteger())
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is initialised "
                             "inside the class, and only a 'static const' of "
                             "integer type may be - write the value on the "
                             "definition outside the class instead");
        s.folded = true;
        s.value = constantExpression("a static member's value");
    } else if (d.type->isArray() && d.type->length() < 0) {
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' has no length, and a "
                         "static member cannot take one from its definition - "
                         "the class is what says how big it is");
    }

    s.symbol = staticMemberSymbol(cls, d.name, d.type, access, d.pos);
    owner->addStaticMember(s);
}

// `int Counter::total = 0;` at file scope - the definition the declaration inside the
// class asked for. An ordinary global the class gave its name to, so all this adds is
// finding which member it is and taking the symbol from it.
void Parser::defineStaticMember(Declared &d, Program &program) {
    const Type *owner = findTypedef(d.qualifier);
    if (owner == nullptr || !owner->isStructOrUnion())
        src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
    const Type::StaticMember *s = owner->findStaticMember(d.name);
    if (s == nullptr)
        src_.fail(d.pos, "'" + d.qualifier + "' declares no static member '" +
                         d.name + "'");
    if (s->type->unqualified() != d.type->unqualified() ||
        s->type->isConst() != d.type->isConst())
        src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was declared '" +
                         s->type->describe() + "' and this defines it as '" +
                         d.type->describe() + "'");
    for (const Global &g : program.globals)
        if (g.symbol == s->symbol)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is defined "
                             "twice");

    // **A static member of class type has to be constructed before main**, which is the
    // mechanism a static local with a constructor needs and is not here yet. Refused
    // where the storage is made. A class with no constructor is an aggregate.
    if (const Type *cls = memberClass(s->type))
        if (!cls->tag().empty() &&
            overloadsOf(constructorKey(cls->tag())) != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is a static "
                             "member of '" + cls->tag() + "', which has a "
                             "constructor - running one before main is not "
                             "supported yet");

    std::vector<GlobalPiece> pieces;
    bool hasInit = false;
    if (consume("=") || atBracedInitialiser(d.name)) {
        Init in = parseInitialiser();
        flattenInit(s->type, in, 0, pieces);
        hasInit = true;
    }
    expect(";");

    program.globals.push_back(Global{ d.qualifier + "::" + d.name, s->symbol,
                                      s->type, std::move(pieces), hasInit, false,
                                      s->type->isConst() });
}

// Naming a static member, however it was reached. A folded one is its value
// and has no storage at all; every other is the one global the class named.
ExprPtr Parser::staticMemberRef(const Type *owner, const Type::StaticMember &s,
                                const std::string &cls, std::size_t pos) {
    if (s.access != Access::Public && currentClass_ != owner->unqualified() &&
        !isFriendOf(owner))
        src_.fail(pos, "'" + cls + "::" + s.name + "' is " +
                       (s.access == Access::Private ? "private" : "protected"));
    if (s.folded) {
        ExprPtr n(new Num(s.value));
        n->setType(s.type);
        return n;
    }
    Var *v = Var::global(cls + "::" + s.name);
    v->setSymbol(s.symbol);
    ExprPtr n(v);
    n->setType(s.type);
    return n;
}

// A member function declaration, keyed under "Class::name" in the one table every
// function lives in. Nothing about overload resolution had to be told that members
// exist: two members with different parameters are two entries under that key.
void Parser::declareMember(const std::string &cls, const Declared &d,
                           bool constThis, Access access, bool inUnion,
                           bool isVirtual, bool isStatic) {
    if (inUnion)
        src_.fail(d.pos, "a member function of a union is not supported yet");

    const Type *fn = d.type;
    checkOperatorDeclarable(d.name, fn->params().size(), true, d.pos);
    std::string key = cls + "::" + d.name;
    std::vector<std::size_t> &set = functionIndex_[key];

    const std::vector<const Type *> &params = fn->params();
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (f.constThis == constThis && sameParameters(f.params, params))
            src_.fail(d.pos, "'" + key + "' is declared twice");
    }

    if (inUnion && isVirtual)
        src_.fail(d.pos, "a union cannot have a virtual function");

    // **A static member takes no slot and overrides nothing.** It is not part
    // of an object, so there is no object to dispatch on; [class.static]/1 says
    // it can be neither `virtual` nor cv-qualified, and both are refused where
    // they are written rather than quietly dropped here.
    if (isStatic) {
        const std::string sym = memberSymbol(cls, d.name, fn, access, false,
                                             d.pos, false, true);
        if (!pendingDefaults_.empty()) defaultArgs_[sym] = pendingDefaults_;
        pendingDefaults_.clear();
        set.push_back(functions_.size());
        functions_.push_back(Signature{
            d.name, sym,
            fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
            cls, false, access, false });
        functions_.back().isStaticMember = true;
        functions_.back().isNoexcept = pendingNoexcept_;
        pendingNoexcept_ = false;
        return;
    }

    // **A slot is taken once and then kept**: an override replaces the entry the base
    // put there, matching on the signature minus the return type. **And finding that
    // slot is what makes this virtual**, keyword or not - so the search runs first.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (!overrides(slots[i], d.name, params, constThis)) continue;
        slot = i;
        isVirtual = true;
        break;
    }

    // **The slots that came down are the *first* base's**, and a class may override a
    // virtual of any of them: one overriding a second base's was found nowhere,
    // declared non-virtual and dispatched statically. The keyword hid it..
    if (!isVirtual)
        if (const Type *self = findTypedef(cls)) {
            const std::vector<Type::BaseSpec> &bs = self->bases();
            for (std::size_t bi = 1; bi < bs.size() && !isVirtual; bi++) {
                std::map<std::string, std::vector<VSlot> >::const_iterator it =
                    vtables_.find(bs[bi].type->tag());
                if (it == vtables_.end()) continue;
                for (std::size_t i = 0; i < it->second.size(); i++)
                    if (overrides(it->second[i], d.name, params, constThis)) {
                        isVirtual = true;
                        break;
                    }
            }
        }

    const std::string symbol = memberSymbol(cls, d.name, fn, access, constThis,
                                            d.pos, isVirtual);
    if (!pendingDefaults_.empty()) defaultArgs_[symbol] = pendingDefaults_;
    pendingDefaults_.clear();
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });
    functions_.back().isNoexcept = pendingNoexcept_;
    pendingNoexcept_ = false;

    if (!isVirtual) return;
    if (slot < slots.size()) { slots[slot].symbol = symbol; return; }
    slots.push_back(VSlot{ d.name, symbol, params, constThis });
}

// A member function's linkage name. Never plain, and never affected by
// `extern "C"`: a member cannot have C linkage, so the two ABIs are the only
// choice here.
std::string Parser::memberSymbol(const std::string &cls, const std::string &name,
                                 const Type *fn, Access access, bool constThis,
                                 std::size_t pos, bool isVirtual, bool isStatic) {
    // Q public, I protected, A private - the Microsoft ABI puts the access in the name
    // and Itanium does not, both measured. **And a virtual member is U on Microsoft
    // whatever its access**: ?who@Base@@UEAAHXZ against ?plain@Base@@QEAAHXZ.
    // **A static member is S, K or C** - public, protected, private - where a
    // non-static is Q, I or A. Itanium spells a static member exactly as it
    // spells any other, `_ZN1S3pubEi`, so only this half of the pair moves.
    const char code = isStatic && access == Access::Public    ? 'S'
                    : isStatic && access == Access::Protected ? 'K'
                    : isStatic                                ? 'C'
                    : isVirtual        ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    // A class defined inside a function body: both ABIs wrap the enclosing
    // function's whole name round the ordinary member name, which is what
    // keeps two functions' `struct L` from being one symbol.
    const std::string *owner = localOwnerOf(cls);

    std::string out, why;
    bool ok = target_.microsoftNames()
            ? (owner != nullptr
                   ? microsoftLocalMemberName(*owner, cls, findTypedef(cls), name,
                                              fn, code, constThis, &out, &why)
                   : microsoftMemberName(cls, findTypedef(cls), name, fn, code,
                                         constThis, &out, &why))
            : (owner != nullptr
                   ? itaniumLocalMemberName(*owner, cls, findTypedef(cls), name,
                                            fn, constThis, &out, &why)
                   : itaniumMemberName(cls, findTypedef(cls), name, fn, constThis,
                                       &out, &why));
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// A variable at namespace scope is mangled by the Microsoft ABI and left
// alone by Itanium. A static one is nobody else's business either way, so it
// keeps the name it was written with.
std::string Parser::dataSymbol(const std::string &name, const Type *type,
                               bool isStatic, std::size_t pos) {
    if (cLinkage_ > 0) return name;
    if (!target_.microsoftNames()) return itaniumDataName(name, isStatic);
    // Microsoft mangles a variable only where something outside could name
    // it. An internal one keeps what it was written with - measured against
    // clang, which spells it the same way.
    if (isStatic) return name;
    std::string out, why;
    if (!microsoftDataName(name, type, &out, &why))
        src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

// **The parameter list is what identifies a function now, not the name.** A difference
// in the parameters declares a *second* function and only an identical list is a
// redeclaration. The return type is deliberately not part of that search.
void Parser::declareFunction(const std::string &name, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, bool defining, std::size_t pos,
                             bool internal) {
    // While a specialization is being replayed the function it declares is that
    // specialization, keyed as "twice<int>" from when the call asked for it. **And the
    // namespace goes on here, once**, every table below being keyed by the whole name.
    const std::string plain = instantiationName(name);
    const std::string qualified =
        (cLinkage_ > 0 || plain == "main" || namespaceStack_.empty())
            ? plain : namespacePrefix() + plain;
    const std::string &key = qualified;
    checkOperatorDeclarable(key, params.size(), false, pos);
    const bool cName = cLinkage_ > 0 || key == "main";
    std::vector<std::size_t> &set = functionIndex_[key];

    for (std::size_t k = 0; k < set.size(); k++) {
        Signature &f = functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if (f.variadic != variadic || !sameParameters(f.params, params)) continue;

        if (f.returns != returns)
            src_.fail(pos, "'" + key + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        // **A declaration and its definition must agree about `noexcept`.** Measured
        // both ways: clang refuses a definition that drops it and one that adds it.
        // The specification is not part of the type, so this is what holds them together.
        if (f.isNoexcept != pendingNoexcept_)
            src_.fail(pos, "'" + key + "' was declared " +
                           (f.isNoexcept ? "'noexcept'" : "without 'noexcept'") +
                           " and this says " +
                           (pendingNoexcept_ ? "'noexcept'" : "nothing") +
                           " - a declaration and its definition have to make "
                           "the same promise");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
            f.defined = true;
        }
        pendingNoexcept_ = false;

        // **[dcl.fct.default]/4: a later declaration adds defaults, it does not discard
        // them.** This path cleared them, so `g`'s were re-read as the next function's
        // and `h()` returned 50. Merged rather than replaced, the union a suffix.
        if (!pendingDefaults_.empty()) {
            std::vector<std::size_t> &have = defaultArgs_[f.symbol];
            if (have.size() < pendingDefaults_.size())
                have.resize(pendingDefaults_.size(), 0);
            for (std::size_t i = 0; i < pendingDefaults_.size(); i++) {
                if (pendingDefaults_[i] == 0) continue;
                if (have[i] != 0)
                    src_.fail(pos, "'" + key + "' already has a default for "
                                   "parameter " + std::to_string(i + 1) +
                                   " from an earlier declaration, and a later "
                                   "one may add a default where there was none "
                                   "but may not give a second");
                have[i] = pendingDefaults_[i];
            }
            requireDefaultsAreASuffix(have, pos);
        }
        pendingDefaults_.clear();
        return;
    }

    // A new parameter list, so a new function - unless the name can only hold
    // one. Both halves of that are refused here rather than at the link, where
    // the report would be about a duplicate symbol in a file nobody wrote.
    if (!set.empty()) {
        const Signature &first = functions_[set[0]];
        if (cName || first.cLinkage)
            src_.fail(pos, "'" + key + "' cannot be overloaded - " +
                           (key == "main" ? std::string("'main' is one function")
                                           : std::string("a name with C linkage "
                                             "carries one symbol")));
    }

    set.push_back(functions_.size());
    functions_.push_back(Signature{ key,
                                    functionSymbol(key, returns, params, variadic,
                                                   internal, pos),
                                    returns, params, variadic, defining, pos,
                                    cName, std::string(), false,
                                    Access::Public });
    functions_.back().isNoexcept = pendingNoexcept_;
    pendingNoexcept_ = false;
    if (!pendingDefaults_.empty())
        defaultArgs_[functions_.back().symbol] = pendingDefaults_;
    pendingDefaults_.clear();
}

const std::vector<std::size_t> *
Parser::overloadsOf(const std::string &name) const {
    auto it = functionIndex_.find(name);
    if (it == functionIndex_.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// The sole function of that name, or nothing when the name is overloaded. Every caller
// wants one function without having any arguments to choose by, so "there are several"
// is not an answer it can use - each one says so in its own words instead.
const Parser::Signature *Parser::findFunction(const std::string &name) const {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr || set->size() != 1) return nullptr;
    return &functions_[(*set)[0]];
}

// The one function of this name with these parameters - the only question a definition
// can ask, since a definition IS a parameter list. Going through lookupFunction broke
// the moment a name could hold two: it answers "which one", and a definition knows.
const Parser::Signature &
Parser::lookupSignature(const std::string &name,
                        const std::vector<const Type *> &params,
                        bool variadic, std::size_t pos) const {
    // The same qualification the declaration used, or a definition written
    // inside a namespace cannot find the prototype it is defining.
    const std::string key = name.find("::") != std::string::npos
                          ? name
                          : qualifyForLookup(instantiationName(name),
                                             &Parser::hasFunctionNamed);
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        for (std::size_t k = 0; k < set->size(); k++) {
            const Signature &f = functions_[(*set)[k]];
            if (f.variadic == variadic && sameParameters(f.params, params))
                return f;
        }
    }
    src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

const Parser::Signature &Parser::lookupFunction(const std::string &name,
                                                std::size_t pos) const {
    if (const Signature *s = findFunction(name)) return *s;
    src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
}

void Parser::blockFunctionDeclaration(const Declared &d) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    declareFunction(d.name, d.type, params, variadic, false, d.pos);
}

// **`Ts... rest` - one thing written, several parameters made.** In a pattern the pack
// stands for itself, which Itanium spells `DpT0_`; in an instantiation it is as many
// parameters as members, named `rest$0` and `rest$1`, which `rest...` expands to.
bool Parser::packParameter(std::vector<const Type *> *types,
                           std::vector<std::string> *names) {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("...")) return false;
    auto pk = packs_.find(peek().text);
    if (pk == packs_.end()) return false;

    const std::vector<const Type *> members = pk->second.types;
    const bool pattern = members.size() == 1 &&
                         members[0]->kind() == Kind::TemplateParam;
    at_ += 2;
    std::string base;
    if (peek().kind == TokenKind::Ident) { base = peek().text; at_++; }

    if (pattern) {
        types->push_back(types_.packExpansion(members[0]));
        if (names != nullptr) names->push_back(base);
        return true;
    }
    std::vector<std::string> made;
    for (std::size_t i = 0; i < members.size(); i++) {
        types->push_back(types_.withoutConst(members[i]));
        made.push_back(base + "$" + std::to_string(i));
        if (names != nullptr) names->push_back(made.back());
    }
    // Recorded under the *written* name as well, so `rest...` at a call and
    // `sizeof...(rest)` both find it beside `sizeof...(Ts)`.
    if (!base.empty()) {
        PackBinding pb;
        pb.types = members;
        pb.names = made;
        packs_[base] = pb;
    }
    pk->second.names = made;
    return true;
}

// To the ',' or ')' that ends a default argument, counting brackets so that a call or
// a subscript written inside one keeps its own commas. `<` is not counted: such a
// default is refused where it is read rather than mis-parsed here.
void Parser::skipDefaultArgument() {
    int depth = 0;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this default argument never ends");
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;         // the ')' closing the parameters
            depth--;
        } else if (t.is(",") && depth == 0) return;
        at_++;
    }
}

// To the ',' or ';' that ends a member's own initialiser, counting brackets so
// that a call or a braced list written inside one keeps its own commas.
void Parser::skipMemberInitialiser() {
    int depth = 0;
    for (;;) {
        const Token &t = peek();
        if (t.kind == TokenKind::End)
            src_.fail(t.pos, "this member initialiser never ends");
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return;
            depth--;
        } else if (depth == 0 && (t.is(",") || t.is(";"))) return;
        at_++;
    }
}

// **[class.base.init]/9: a member the constructor did not name is initialised by the
// initialiser the class gave it.** Read again at each constructor that needs it, an
// initialiser being evaluated once per construction; the locals are put aside.
bool Parser::hasMemberInitialiser(const std::string &tag) const {
    const std::string prefix = tag + "::";
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.lower_bound(prefix);
    return it != memberInit_.end() &&
           it->first.compare(0, prefix.size(), prefix) == 0;
}

std::vector<StmtPtr> Parser::memberInitialisers(const std::string &tag,
                                                const Type *type, int thisSlot,
                                                const std::set<std::string> &already,
                                                std::size_t pos) {
    std::vector<StmtPtr> out;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        if (already.find(ms[i].name) != already.end()) continue;
        StmtPtr one = memberInitialiser(tag, type, ms[i], thisSlot, pos);
        if (one != nullptr) out.push_back(std::move(one));
    }
    return out;
}

StmtPtr Parser::memberInitialiser(const std::string &tag, const Type *type,
                                  const Member &m, int thisSlot,
                                  std::size_t pos) {
    std::map<std::string, std::size_t>::const_iterator it =
        memberInit_.find(tag + "::" + m.name);
    if (it == memberInit_.end()) return nullptr;

    // Read where it was written, with the constructor's locals put aside:
    // an initialiser on a member is in the class's scope, not the body's.
    const std::size_t resume = at_;
    std::vector<Local> outer;
    outer.swap(locals_);
    at_ = it->second;
    ExprPtr value = decay(assign());

    // **A class-typed member is *built* from its initialiser, not assigned one.**
    // `struct E { M m = M(2); };` used to construct a temporary, move its bytes
    // into storage nothing had constructed, and then destroy the temporary - one
    // constructor and two destructors, and for a class that owns anything, the
    // member holding what the temporary's destructor had just given back. It is
    // copy-initialisation, [dcl.init]/17, so it goes through the same overload
    // resolution `: m(x)` does and reaches the copy or the move constructor.
    StmtPtr made;
    if (memberClass(m.type) != nullptr && !m.type->isReference() &&
        overloadsOf(constructorKey(memberClass(m.type)->tag())) != nullptr) {
        std::vector<ExprPtr> one;
        one.push_back(std::move(value));
        made = constructMember(tag, type, m, thisSlot, one, pos, false);
        if (made == nullptr) value = std::move(one[0]);
    }
    if (made == nullptr) {
        checkAssignable(*value, m.type, pos, "'" + m.name + "'");
        value = convert(std::move(value), m.type);
        ExprPtr field = thisMember(thisSlot, type, m);
        ExprPtr store(new Assign(std::move(field), std::move(value)));
        store->setType(m.type);
        made = StmtPtr(new ExprStmt(std::move(store)));
    }
    locals_.swap(outer);
    at_ = resume;

    // **The initialiser is a full expression, and its temporaries die at the end
    // of it** - [class.temporary]/4, which here means before the next member is
    // built and not at the end of the constructor. They were left on the pending
    // list until something else flushed them, which made `M m = M(2);` destroy
    // its temporary late; through the copy constructor it would not have
    // destroyed it at all.
    std::vector<StmtPtr> all;
    all.push_back(std::move(made));
    flushTemporaries(all);
    if (all.size() == 1) return std::move(all[0]);
    return StmtPtr(new Block(std::move(all)));
}

// **A class-typed member is built, not left.** [class.base.init]/8: one the list does
// not name is default-initialised, and a written constructor used to leave it holding
// the stack. An array member is built by a loop, N being a property of the type.
StmtPtr Parser::constructMember(const std::string &cls, const Type *type,
                                const Member &m, int thisSlot,
                                std::vector<ExprPtr> &args, std::size_t pos,
                                bool implicit) {
    const Type *mc = memberClass(m.type);
    if (mc == nullptr || mc->tag().empty() || m.type->isReference()) return nullptr;
    const std::string key = constructorKey(mc->tag());
    if (overloadsOf(key) == nullptr) return nullptr;
    if (!args.empty() && m.type->isArray())
        src_.fail(pos, "'" + m.name + "' is an array, and an initialiser list "
                       "cannot say what to pass to each element of it");

    // Held by value: reading a default argument can grow `functions_`.
    Signature chosen;
    if (!args.empty()) {
        chosen = resolveOverload(key, args, pos);
    } else {
        const Signature *ctor = defaultConstructorOf(mc);
        if (ctor == nullptr)
            src_.fail(pos, implicit
                ? "'" + cls + "' has no constructor of its own, and the one "
                  "the compiler would write cannot build its member '" +
                  m.name + "': '" + mc->tag() + "' has no constructor taking "
                  "nothing"
                : "this constructor of '" + cls + "' does not name '" + m.name +
                  "' in its initialiser list, and '" + mc->tag() + "' has no "
                  "constructor taking nothing - add ': " + m.name + "(...)' "
                  "to the list");
        chosen = *ctor;
    }
    if (chosen.access != Access::Public && currentClass_ != mc && !isFriendOf(mc))
        src_.fail(pos, implicit
            ? "'" + cls + "' cannot be built by the constructor the compiler "
              "would write: the constructor of '" + mc->tag() + "' taking "
              "nothing is " + (chosen.access == Access::Private ? "private"
                                                                 : "protected")
            : "'" + mc->tag() + "' has no public constructor taking these "
              "arguments for '" + m.name + "' - the one that matches is " +
              (chosen.access == Access::Private ? "private" : "protected"));
    for (std::size_t k = 0; k < functions_.size(); k++)
        if (functions_[k].symbol == chosen.symbol) functions_[k].used = true;
    applyDefaults(chosen, args, pos);

    int indexSlot = 0;
    long long count = 0;
    if (m.type->isArray()) {
        count = m.type->length();
        if (count < 0)
            src_.fail(pos, "'" + cls + "::" + m.name + "' has no length, so a "
                           "constructor does not know how many elements to "
                           "build");
        indexSlot = allocateFrameSlot(types_.intType());
    }

    ExprPtr acc = thisMember(thisSlot, type, m);

    ExprPtr addr;
    if (m.type->isArray()) {
        addr = indexBytes(types_, decay(std::move(acc)), mc, indexSlot, target_);
    } else {
        addr = ExprPtr(new Unary('&', std::move(acc)));
        addr->setType(types_.pointerTo(mc));
    }

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    std::vector<const Type *> ps;
    ps.push_back(types_.pointerTo(mc));
    for (std::size_t k = 0; k < args.size(); k++) {
        all.push_back(std::move(args[k]));
        ps.push_back(chosen.params[k]);
    }
    StmtPtr one(new ExprStmt(
        completeCall(mc->tag(), chosen.symbol, nullptr, types_.get(Kind::Void),
                     ps, false, pos, std::move(all))));
    if (m.type->isArray()) return eachElement(indexSlot, count, std::move(one));
    return one;
}

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    pendingDefaults_.clear();
    std::size_t closed = peek().pos;
    if (consume(")")) return;
    if (peek().is("void") && peekAt(1).is(")")) { at_ += 2; return; }

    for (;;) {
        if (consume("...")) { variadic = true; expect(")"); break; }
        if (packParameter(&params, nullptr)) {
            if (consume(")")) break;
            expect(",");
            continue;
        }
        StorageClass psc;
        Qualifiers pquals;
        const Type *pt = specifiers(&psc, &pquals);
        Declared pd = declarator(pt, true);
        if (mentionsDeduced(pd.type))
            src_.fail(pd.pos, "a parameter's type cannot be deduced - `auto` "
                              "there is C++14, and this compiler is C++11");
        if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
        if (pd.type->isVoid())
            src_.fail(pd.pos, "'void' is only a parameter list on its own");
        params.push_back(types_.withoutConst(pd.type));

        // `int b = 3`. The tokens are left where they are and their position
        // recorded; a call that omits the argument reads them again.
        pendingDefaults_.resize(params.size(), 0);
        if (consume("=")) {
            if (peek().is("{"))
                src_.fail(peek().pos, "a braced default argument is not "
                                      "supported yet - write the value");
            pendingDefaults_.back() = at_;
            skipDefaultArgument();
            if (at_ == pendingDefaults_.back())
                src_.fail(peek().pos, "this parameter says '=' and then gives "
                                      "no default");
        }
        closed = peek().pos;
        if (consume(")")) break;
        expect(",");
    }

    requireDefaultsAreASuffix(pendingDefaults_, closed);
}

// [dcl.fct.default]/4: once a parameter has a default, every one after it must have
// one too - a call fills them in from the right, so a parameter without one behind a
// parameter with one could never be reached. Said where the list is read.
void Parser::requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                       std::size_t pos) {
    bool seen = false;
    for (std::size_t i = 0; i < defaults.size(); i++) {
        if (defaults[i] != 0) { seen = true; continue; }
        if (seen)
            src_.fail(pos, "every parameter after one with a default needs a "
                           "default of its own - a call fills them in from the "
                           "right, so there would be no way to reach this one");
    }
}

