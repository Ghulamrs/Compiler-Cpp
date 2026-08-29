// The parser: what a class needs written for it.
//
// Constructors and destructors, vtables and thunks, the implicit special
// members and the code that defines them when something calls one, and the
// static data members that live outside the object. Rungs 3 and 4.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

StmtPtr Parser::constructLocal(const Declared &d, int offset,
                               std::vector<ExprPtr> args) {
    const std::string key = constructorKey(d.type->tag());
    const Signature &ctor = resolveOverload(key, args, d.pos);

    if (ctor.access != Access::Public && currentClass_ != d.type->unqualified())
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
    return StmtPtr(new ExprStmt(std::move(call)));
}

// The deleting destructor's name. Built through the manglers rather than by
// concatenation, because a nested class's is a whole nested-name -
// ??_GInner@Outer@@UEAAPEAXI@Z, not ??_GOuter::Inner@@...
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

    // **A base with a virtual destructor makes this one virtual**, keyword or
    // not - [class.dtor], and the same rule declareMember follows for an
    // ordinary override. The base's slots are already down in this class's
    // table, so the question is answered by looking for the "~" entry, and it
    // has to be answered here rather than after the name is built: the code
    // letter below reads it.
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
    // depends on the ABI: Itanium wants two, the complete-object destructor
    // and the deleting one, adjacent and in that order; Microsoft wants one,
    // holding only the deleting form. Measured from clang for both.
    //
    // A derived class overrides the base's entries in place, the way any
    // virtual does - matching on the name "~", not on the class's own name,
    // since ~Base and ~Derived are different spellings of the same slot.
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

// **Setting the vptr, for whoever is building the object.** Pulled out of the
// constructor path when implicit constructors arrived: an implicitly declared
// default or copy constructor stores exactly the same pointers a written one
// does, and a second copy of this would be a second place to get the header
// offset wrong.
//
// What is stored is the table's address plus the header: Itanium writes
// offset-to-top and typeinfo first, so the vptr points at table + 16 -
// measured from clang's own `addq $16` - and Microsoft has no header, so the
// address is the table's own.
std::vector<StmtPtr> Parser::storeVptrs(const std::string &cls,
                                        const Type *memberOf, int thisSlot) {
    const bool ms = target_.microsoftNames();
    const std::string table = ms ? "??_7" + cls + "@@6B@"
                                 : "_ZTV" + std::to_string(cls.size()) +
                                   cls;
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *entries = types_.pointerTo(entry);

    // **The table's ADDRESS, not its contents.** A global Var is an
    // lvalue and reading one loads from it - which stored the table's
    // first word in the vptr and crashed on the first call. Giving it the
    // array type and decaying it is what yields the address, the same road
    // any array name takes.
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
        // **In bytes, because this Add is not the parser's pointer
        // arithmetic.** Building the node by hand and typing it by hand
        // skips the scaling `p + n` normally gets, so adding 2 added two
        // bytes and the vptr pointed two bytes into the table's first
        // word. The header is two pointers wide; that is what to add.
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

    // **A class with a polymorphic second base has a second vptr**, inside
    // that base's subobject, pointing at the secondary table laid down
    // behind the primary one. The first vptr is the object's own; this is
    // the one a B * will read.
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

// **A function with no source behind it.** The deleting destructor is the one
// thing in the vtable that no program writes: it runs the destructor and then
// gives the memory back, and it exists because `delete p` through a base
// pointer has to reach both through one slot.
//
// Itanium's D0 takes `this` and returns nothing. Microsoft's ??_G takes `this`
// and a flag, returns `this`, and frees only when the low bit is set - which
// is how a non-heap object reaches the same slot safely. Both are built here
// as ordinary AST and emitted like any other function, so no backend knows
// this one was invented.
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
    functions_[static_cast<std::size_t>(&dtor - &functions_[0])].used = true;
    std::vector<ExprPtr> args;
    args.push_back(std::move(address));
    std::vector<const Type *> params;
    params.push_back(args[0]->type());
    return completeCall("~" + dtor.owner, dtor.symbol, nullptr,
                        types_.get(Kind::Void), params, false, pos,
                        std::move(args));
}

// **RAII is this function.** Everything constructed since `from` is destroyed,
// last first, which is the order the standard fixes and the only order that
// can be right when one object's destructor may read another that was built
// before it.
// **One region per stretch, and the stretches do not overlap.** Objects a, b
// and c built in that order give three ranges - after a, after b, after c -
// and each pad destroys exactly what exists by then. That is what lets a
// call-site table hold them: sorted and disjoint, where nesting them would
// not be.
//
// The statements that *do* the constructing are outside every region on
// purpose: an exception from a constructor leaves that object unbuilt, and
// the region before it destroys what came earlier.
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

// **What an exception has to do on its way out of a scope.** The objects are
// the same ones a `return` unwinds - `alive_` holds them and nothing new had
// to track them - and the only difference is where the code runs from: a
// landing pad rather than the return path, ending in _Unwind_Resume rather
// than in a return.
StmtPtr Parser::cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                           std::size_t pos) {
    // **Bounded rather than truncated.** Resizing `alive_` down and back up
    // would default-construct what it had thrown away, and the second pad
    // would then destroy an object with no class - silently one destructor
    // short.
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
        const Signature *dtor = destructorOf(a.cls);
        if (dtor == nullptr) continue;

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
}

// The vtable: one pointer per virtual function, in the order the base first
// declared them, an override replacing an entry rather than adding one.
//
// It is emitted as an ordinary global whose initialiser pieces are symbol
// addresses, which is machinery that already existed - no backend was told
// about vtables at all.
//
// **The two ABIs differ in the header and so in what the vptr holds.** Itanium
// writes offset-to-top and a typeinfo pointer before the functions, and the
// vptr points past them - table + 16, measured from the addq in clang's own
// constructor. The typeinfo slot is a plain 0 here: this compiler has no RTTI
// and refuses `typeid` by name, and clang under -fno-rtti writes 0 too.
// Microsoft has no header, so the vptr is the table's own address.
// The thunk a secondary table points at. `B *p` calling an overridden `g`
// passes `this` as the B subobject - the object's address plus B's offset -
// and C::g expects the object's address, so something has to walk it back.
// clang emits a tail jump; this is an ordinary call and return, which costs a
// frame and behaves identically, and needs nothing new from any backend.
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

    ExprPtr call = completeCall(slot.name, slot.symbol, nullptr, returns, full,
                                false, pos, std::move(args));
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

void Parser::emitVtable(const Type *cls, const std::string &tag,
                        std::size_t pos) {
    if (tag.empty())
        src_.fail(pos, "a class with a virtual function needs a name - its "
                       "vtable is a symbol, and an anonymous class has none");

    const std::vector<VSlot> &slots = vtables_[tag];
    const bool ms = target_.microsoftNames();
    const std::string symbol = ms ? "??_7" + tag + "@@6B@"
                                  : "_ZTV" + std::to_string(tag.size()) + tag;

    for (std::size_t i = 0; i < current_->globals.size(); i++)
        if (current_->globals[i].symbol == symbol) return;   // one per class

    std::vector<GlobalPiece> pieces;
    int at = 0;
    if (!ms) {
        pieces.push_back(GlobalPiece{ at, 8, 0, std::string() });  // offset-to-top
        at += 8;
        pieces.push_back(GlobalPiece{ at, 8, 0, std::string() });  // typeinfo
        at += 8;
    }
    for (std::size_t i = 0; i < slots.size(); i++) {
        pieces.push_back(GlobalPiece{ at, 8, 0, slots[i].symbol });
        at += 8;
    }

    // **A secondary table for every polymorphic base after the first**, laid
    // down behind the primary one in the same symbol - measured: _ZTV1C holds
    // both, and the second begins with an offset-to-top of -16 saying how far
    // back the complete object is.
    //
    // Each entry is the base's own function unless this class overrides it, in
    // which case it is a thunk: a call through a B * arrives with `this`
    // pointing at the B subobject, and the override expects the whole object.
    const std::vector<Type::BaseSpec> &bases = cls->bases();
    for (std::size_t bi = 1; bi < bases.size(); bi++) {
        const Type *b = bases[bi].type;
        if (!b->polymorphic()) continue;
        const int off = bases[bi].offset;

        // **The Microsoft ABI arranges this differently, and it is not the
        // same thing under other names.** Measured with clang: it emits two
        // separate vftable symbols - ??_7C@@6BA@@@ for the A view and
        // ??_7C@@6BB@@@ for the B one - rather than one table in two sections,
        // and the second points straight at ?g@C@@UEAAHXZ with no thunk in
        // sight, where Itanium needs _ZThn16_N1C1gEv. Whether cl agrees with
        // clang there has not been measured, and guessing at an ABI is the one
        // thing this project does not do.
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
            pieces.push_back(GlobalPiece{ at, 8, 0, std::string() });
            at += 8;
        }
        const std::vector<VSlot> &theirs = vtables_[b->tag()];
        for (std::size_t i = 0; i < theirs.size(); i++) {
            std::string entry = theirs[i].symbol;
            // Did this class override it? Its own slot list has the answer.
            for (std::size_t k = 0; k < slots.size(); k++) {
                if (slots[k].name != theirs[i].name) continue;
                if (slots[k].constThis != theirs[i].constThis) continue;
                if (slots[k].params.size() != theirs[i].params.size()) continue;
                bool same = true;
                for (std::size_t q = 0; q < slots[k].params.size(); q++)
                    if (slots[k].params[q] != theirs[i].params[q]) { same = false; break; }
                if (!same) continue;
                if (slots[k].symbol != theirs[i].symbol)
                    entry = synthesizeThunk(tag, cls, slots[k], off, pos);
                break;
            }
            pieces.push_back(GlobalPiece{ at, 8, 0, entry });
            at += 8;
        }
    }

    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *table = types_.arrayOf(entry, static_cast<long long>(pieces.size()));
    current_->globals.push_back(Global{ symbol, symbol, table, std::move(pieces),
                                        true, false, true });
}

// A constructor, read at the point its '(' was seen. It is a member function
// whose name is the class and whose return type is nothing at all - so it is
// keyed under "Point::Point" and every piece of overload machinery applies to
// it unchanged, which is what makes Point() and Point(int,int) two entries
// that a construction chooses between.
void Parser::declareConstructor(const std::string &cls, std::size_t pos,
                                Access access) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (variadic)
        src_.fail(pos, "a constructor cannot take '...'");

    // A constructor returns nothing, and saying so as void is what lets the
    // rest of the compiler treat the call like any other.
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);

    std::string key = constructorKey(cls);
    std::vector<std::size_t> &set = functionIndex_[key];
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (f.params.size() != params.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < params.size(); i++)
            if (f.params[i] != params[i]) { same = false; break; }
        if (same) src_.fail(pos, "'" + cls + "::" + cls + "' is declared twice");
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
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
}

// The class a member is built from: the element type when the member is an
// array, and nothing at all when it is not of class type.
static const Type *memberClass(const Type *t) {
    while (t != nullptr && t->isArray()) t = t->pointee();
    return (t != nullptr && t->isStructOrUnion()) ? t->unqualified() : nullptr;
}

// One element of an array member, by address: the member's own address,
// decayed, plus the index times the element's size.
//
// **In bytes, and deliberately.** A Binary built here is not the parser's
// pointer arithmetic and gets none of its scaling - the same trap the vptr
// store hit, where `+ 2` added two bytes rather than two entries.
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

const Parser::Signature *Parser::defaultConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++)
        if (functions_[(*set)[k]].params.empty()) return &functions_[(*set)[k]];
    return nullptr;
}

const Parser::Signature *Parser::copyConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isReference()) continue;
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

// **A trivial special member is not a function**, and that is measured rather
// than reasoned: cl emits no symbol at all for the default constructor,
// copy constructor or copy assignment of a class with no virtual function and
// no member that needs building, and clang emits none either, on both Itanium
// targets. A class like that leaves its storage alone and `X x;` is a frame
// slot and no call - which is exactly what this compiler already did for a C
// struct, and why the old path is left to handle it untouched.
//
// So an implicit member is declared only where it has work to do. What makes
// work: a virtual function, whose vptr somebody has to store, or a base or
// member that has a constructor of its own to run.
//
// A class that writes any constructor gets no implicit default one - that is
// [class.ctor], and it is also what makes `Point p;` still an error for a
// class whose only constructor takes arguments.
void Parser::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty() || type->kind() == Kind::Union) return;
    // Asked before the copy constructor is declared, because declaring one
    // would answer it yes. A class that writes any constructor gets no
    // implicit default one; a class that writes any constructor still gets an
    // implicit copy constructor.
    const bool wroteConstructor = overloadsOf(constructorKey(tag)) != nullptr;
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    if (wroteConstructor) return;

    bool work = type->polymorphic();
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

// **The destructor the class did not write.** It becomes a function exactly
// when a base or a member has one of its own to run - measured with cl, which
// emits `??1Has@@QEAA@XZ` for a class holding members with destructors and no
// destructor symbol at all for a class of plain members.
//
// **A virtual function does not make it non-trivial**, which is the one that
// would have been guessed wrong: cl emits nothing for a class with a virtual
// `f()` and no destructor anywhere. What makes it *virtual* is a base whose
// destructor is virtual, and then it takes over that slot and gets a deleting
// form beside it like any other virtual destructor.
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

// Its body: the members this class added, in the reverse of the order they
// were declared, and then the bases in the reverse of the order they were
// written. A base's own destructor deals with the members it brought, which is
// why they are skipped here - they are in this class's member list too,
// because data members are copied down.
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
        bool fromBase = false;
        for (std::size_t k = 0; k < bs.size() && !fromBase; k++)
            if (ms[n].offset >= bs[k].offset &&
                ms[n].offset < bs[k].offset + bs[k].type->dataSize())
                fromBase = true;
        if (fromBase) continue;

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

        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr obj(new Unary('*', std::move(me)));
        obj->setType(type);
        ExprPtr acc(new MemberAccess(std::move(obj), ms[n].name, ms[n].offset));
        acc->setType(mt);

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

// The copy assignment operator the class did not write - which is every class,
// since `operator` is refused by name until operator overloading arrives, so
// nothing can write one yet. The trivial line is drawn in the same place and
// was measured the same way: cl emits ??4Poly@@QEAAAEAU0@AEBU0@@Z for a
// polymorphic class, and nothing at all for a class of plain members, where
// `a = b` is the struct assignment this compiler has always emitted.
//
// A polymorphic class is non-trivial here even though the body does not touch
// the vptr. That is [class.copy] and it is what cl does; the vptr is not
// copied because assignment writes into an object that is already of this
// class.
void Parser::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(assignmentKey(tag)) != nullptr) return;

    // **A const member has no assignment to give**, so the operator the
    // compiler would write is deleted rather than non-trivial and none is
    // declared - which is what makes `a = b` say there is no such function
    // rather than quietly writing through a const. Asked over every member
    // before anything else, because the search below stops at the first
    // member that gives the operator work to do and a const one after it
    // would never be reached. A reference member is refused where it is
    // declared and cannot get this far.
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

// The copy constructor the class did not write. The trivial/non-trivial line
// is the same one and measured the same way - cl emits `??0Poly@@QEAA@AEBU0@@Z`
// for a polymorphic class and nothing at all for a class of plain members -
// but what it is drawn on is different. A class writing *any* constructor
// still gets an implicit copy constructor; only writing a copy constructor
// takes it away.
//
// What makes one non-trivial: a virtual function, because the new object's
// vptr is its own and not a copy of the source's, or a base or member whose
// own copy constructor has to run.
void Parser::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;

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

// The body of a default constructor nobody wrote: the bases built in the order
// they were written, then the vptrs, then the members that have constructors
// of their own. Scalars are left alone, which is what [dcl.init] means by
// default-initialisation and what makes an uninitialised `int` member still
// uninitialised here.
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
        functions_[static_cast<std::size_t>(ctor - &functions_[0])].used = true;
        const std::string sym = baseConstructorSymbol(*ctor, base);

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
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, types_.get(Kind::Void), ps,
                         false, pos, std::move(args)))));
    }

    if (type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc == nullptr || mc->tag().empty()) continue;
        if (overloadsOf(constructorKey(mc->tag())) == nullptr) continue;
        const Signature *ctor = defaultConstructorOf(mc);
        if (ctor == nullptr)
            src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its member '" +
                           ms[i].name + "': '" + mc->tag() + "' has no "
                           "constructor taking nothing");
        if (ctor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of '" +
                           mc->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        functions_[static_cast<std::size_t>(ctor - &functions_[0])].used = true;

        // An array of them is built one element at a time, in order - a loop
        // rather than N calls, because N is a property of the type.
        int indexSlot = 0;
        long long count = 0;
        if (ms[i].type->isArray()) {
            count = ms[i].type->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the constructor the compiler would write does "
                               "not know how many elements to build");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr obj(new Unary('*', std::move(me)));
        obj->setType(type);
        ExprPtr acc(new MemberAccess(std::move(obj), ms[i].name, ms[i].offset));
        acc->setType(ms[i].type);

        ExprPtr addr;
        if (ms[i].type->isArray()) {
            addr = indexBytes(types_, decay(std::move(acc)), mc, indexSlot,
                              target_);
        } else {
            addr = ExprPtr(new Unary('&', std::move(acc)));
            addr->setType(types_.pointerTo(mc));
        }

        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        std::vector<const Type *> ps;
        ps.push_back(types_.pointerTo(mc));
        StmtPtr one(new ExprStmt(
            completeCall(mc->tag(), ctor->symbol, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args))));
        body.push_back(ms[i].type->isArray()
                       ? eachElement(indexSlot, count, std::move(one))
                       : std::move(one));
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


// The body of a copy constructor nobody wrote: the bases that have one of
// their own, then the vptrs, then every member that no base already copied.
//
// **The vptr is set and not copied**, which is the whole difference between
// this and the copy assignment beside it: a copy constructor is making a new
// object, and the new object is of *this* class whatever the source was.
// Measured, in cl's own listing: it stores `OFFSET FLAT:??_7Poly@@6B@` and
// then moves the members across.
//
// Members are copied one at a time rather than the whole object at once. A
// base subobject occupies its data size and not its sizeof, so a derived
// class may have put a member of its own in this class's tail padding - and a
// copy of `sizeof` bytes through the C2 form would take that member with it.
void Parser::synthesizeCopy(std::size_t which, bool assigning) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *srcRef = functions_[which].params[0];
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

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
        const Signature *cc = assigning ? copyAssignOf(base)
                                        : copyConstructorOf(base);
        if (cc == nullptr) continue;              // trivial: its members copy below
        if (cc->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be copied by the " +
                           (assigning ? "assignment" : "constructor") +
                           " the compiler would write: the copy " +
                           (assigning ? "assignment" : "constructor") +
                           " of its base '" + base->tag() + "' is " +
                           (cc->access == Access::Private ? "private" : "protected"));
        functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;
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
        from = convert(std::move(from), types_.pointerTo(types_.withConst(base)));
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(base);

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

    // **A copy constructor sets the vptr; a copy assignment leaves it alone.**
    // That is the whole difference between the two bodies, and it is measured:
    // cl's ??0Poly stores OFFSET FLAT:??_7Poly@@6B@ before moving the members
    // and its ??4Poly moves the members and nothing else. The reason is that
    // assignment writes into an object that already exists and is already of
    // this class, where a constructor is making one.
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
        const Signature *cc = assigning ? copyAssignOf(memberClass(mt))
                                        : copyConstructorOf(memberClass(mt));
        if (cc != nullptr) {
            if (cc->access != Access::Public)
                src_.fail(pos, "'" + cls + "' cannot be copied by the " +
                               (assigning ? "assignment" : "constructor") +
                               " the compiler would write: the copy " +
                               (assigning ? "assignment" : "constructor") +
                               " of '" + memberClass(mt)->tag() + "', the type of '" +
                               ms[i].name + "', is " +
                               (cc->access == Access::Private ? "private"
                                                              : "protected"));
            functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;
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
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr meObj(new Unary('*', std::move(me)));
        meObj->setType(type);
        ExprPtr dst(new MemberAccess(std::move(meObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        dst->setType(mt);

        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(srcRef->referent());
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

    // **`a = b` is an expression and has to have a value**, and the value is
    // the object assigned to. The declared return type is `X &`, and a
    // reference is a pointer everywhere below the parser - so what the
    // function actually returns is `this`.
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
    // Microsoft writes the access as a digit where a member function writes a
    // letter, so a static member that changes from private to public changes
    // its symbol on Windows and keeps it on Linux - the same asymmetry member
    // functions already have, measured the same way.
    const char code = access == Access::Public    ? '2'
                    : access == Access::Protected ? '1'
                                                  : '0';
    std::string out, why;
    if (!microsoftStaticMemberName(cls, findTypedef(cls), name, t, code, &out, &why))
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// `static int total;` inside a class. It declares one object shared by every
// object of the class and takes no room in any of them, so nothing here
// touches the layout - what it needs is a name the linker can hold and a
// definition outside the class to go with it.
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

    // **`static const int k = 5;` written in the class needs no definition**,
    // and that is measured rather than assumed: cl emits no symbol for one and
    // folds the value in wherever it is read. Anything else with an
    // initialiser here is refused, because the definition outside the class is
    // where the storage comes from and the value belongs with it.
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

// `int Counter::total = 0;` at file scope - the definition the declaration
// inside the class asked for. It is an ordinary global that the class gave its
// name to, so all this adds to the global path is finding which member it is
// and taking the symbol from it.
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

    // **A static member of class type has to be constructed before main**,
    // which is the mechanism a static local with a constructor needs and is
    // not here yet. Refused where the storage is made, which is the line that
    // has to change, rather than where it is read. A class with no
    // constructor is an aggregate and initialises like any other global.
    if (const Type *cls = memberClass(s->type))
        if (!cls->tag().empty() &&
            overloadsOf(constructorKey(cls->tag())) != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is a static "
                             "member of '" + cls->tag() + "', which has a "
                             "constructor - running one before main is not "
                             "supported yet");

    std::vector<GlobalPiece> pieces;
    bool hasInit = false;
    if (consume("=")) {
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
    if (s.access != Access::Public && currentClass_ != owner->unqualified())
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

// A member function declaration, keyed under "Class::name" in the one table
// every function lives in. Nothing about overload resolution had to be told
// that members exist: two members of one class with different parameters are
// two entries under that key, exactly as two free functions would be.
void Parser::declareMember(const std::string &cls, const Declared &d,
                           bool constThis, Access access, bool inUnion,
                           bool isVirtual) {
    if (inUnion)
        src_.fail(d.pos, "a member function of a union is not supported yet");

    const Type *fn = d.type;
    std::string key = cls + "::" + d.name;
    std::vector<std::size_t> &set = functionIndex_[key];

    const std::vector<const Type *> &params = fn->params();
    for (std::size_t k = 0; k < set.size(); k++) {
        const Signature &f = functions_[set[k]];
        if (f.params.size() != params.size() || f.constThis != constThis) continue;
        bool same = true;
        for (std::size_t i = 0; i < params.size(); i++)
            if (f.params[i] != params[i]) { same = false; break; }
        if (same)
            src_.fail(d.pos, "'" + key + "' is declared twice");
    }

    if (inUnion && isVirtual)
        src_.fail(d.pos, "a union cannot have a virtual function");

    // **A slot is taken once and then kept.** An override replaces the entry
    // the base put there rather than adding one, which is what makes a
    // Base * and a Derived * agree about where to look. Matching is by name,
    // parameters and constness - the signature, minus the return type, which
    // is what [class.virtual] calls overriding.
    //
    // **Finding that slot is itself what makes this function virtual.**
    // [class.virtual]: a function that overrides one is virtual whether or
    // not the keyword is written again, and the derived class's slots have
    // already come down from the base by the time any member is declared. So
    // the search runs before the name is built rather than after - the
    // Microsoft ABI spells a virtual member U and a plain one Q, and a
    // silently-non-virtual override would have been given the wrong name as
    // well as the wrong dispatch.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (slots[i].name != d.name || slots[i].constThis != constThis) continue;
        if (slots[i].params.size() != params.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < params.size(); k++)
            if (slots[i].params[k] != params[k]) { same = false; break; }
        if (!same) continue;
        slot = i;
        isVirtual = true;
        break;
    }

    const std::string symbol = memberSymbol(cls, d.name, fn, access, constThis,
                                            d.pos, isVirtual);
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });

    if (!isVirtual) return;
    if (slot < slots.size()) { slots[slot].symbol = symbol; return; }
    slots.push_back(VSlot{ d.name, symbol, params, constThis });
}

// A member function's linkage name. Never plain, and never affected by
// `extern "C"`: a member cannot have C linkage, so the two ABIs are the only
// choice here.
std::string Parser::memberSymbol(const std::string &cls, const std::string &name,
                                 const Type *fn, Access access, bool constThis,
                                 std::size_t pos, bool isVirtual) {
    // Q public, I protected, A private - the Microsoft ABI puts access in the
    // name and Itanium does not, both measured against clang.
    //
    // **A virtual member is U on Microsoft whatever its access**, measured:
    // ?who@Base@@UEAAHXZ where the non-virtual ?plain@Base@@QEAAHXZ. Itanium
    // spells a virtual function exactly like any other.
    const char code = isVirtual        ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftMemberName(cls, findTypedef(cls), name, fn, code, constThis, &out, &why)
            : itaniumMemberName(cls, findTypedef(cls), name, fn, constThis,
                                &out, &why);
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

// **The parameter list is what identifies a function now, not the name.** In C
// a second declaration of a name was always the same function and any
// difference was an error; in C++ a difference in the parameters declares a
// *second* function, and only an identical parameter list is a redeclaration.
// So the same-parameters search comes first and everything the C version
// checked is what happens when it finds one.
//
// The return type is deliberately not part of that search: two functions
// differing only in return type are the same function declared twice and
// disagreeing, which is the error the old code already worded well.
void Parser::declareFunction(const std::string &name, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, bool defining, std::size_t pos,
                             bool internal) {
    // While a specialization is being replayed the function it declares is
    // the specialization, keyed and mangled as "twice<int>". Its entry was
    // made when the call asked for it, so this finds that one and marks it
    // defined rather than computing a second symbol.
    const std::string &key = instantiationName(name);
    const bool cName = cLinkage_ > 0 || key == "main";
    std::vector<std::size_t> &set = functionIndex_[key];

    for (std::size_t k = 0; k < set.size(); k++) {
        Signature &f = functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if (f.params.size() != params.size() || f.variadic != variadic) continue;
        bool same = true;
        for (std::size_t i = 0; i < params.size(); i++)
            if (f.params[i] != params[i]) { same = false; break; }
        if (!same) continue;

        if (f.returns != returns)
            src_.fail(pos, "'" + key + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
            f.defined = true;
        }
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
}

const std::vector<std::size_t> *
Parser::overloadsOf(const std::string &name) const {
    auto it = functionIndex_.find(name);
    if (it == functionIndex_.end() || it->second.empty()) return nullptr;
    return &it->second;
}

// The sole function of that name, or nothing when the name is overloaded.
// Every caller of this wants one function without having any arguments to
// choose by, so "there are several" is not an answer it can use - each one
// says so in its own words instead.
const Parser::Signature *Parser::findFunction(const std::string &name) const {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr || set->size() != 1) return nullptr;
    return &functions_[(*set)[0]];
}

// The one function of this name with these parameters - which is the only
// question a definition can ask, since a definition IS a parameter list. Going
// through lookupFunction instead is what broke the moment a name could hold
// two functions: it answers "which one" and a definition already knows.
const Parser::Signature &
Parser::lookupSignature(const std::string &name,
                        const std::vector<const Type *> &params,
                        bool variadic, std::size_t pos) const {
    if (const std::vector<std::size_t> *set = overloadsOf(instantiationName(name))) {
        for (std::size_t k = 0; k < set->size(); k++) {
            const Signature &f = functions_[(*set)[k]];
            if (f.params.size() != params.size() || f.variadic != variadic) continue;
            bool same = true;
            for (std::size_t i = 0; i < params.size(); i++)
                if (f.params[i] != params[i]) { same = false; break; }
            if (same) return f;
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

// **`Ts... rest` - one thing written, several parameters made.**
//
// In a pattern the pack stands for itself and this is one parameter of type
// `Ts...`, which is what Itanium spells `DpT0_` and says at every size. In a
// real instantiation it is as many parameters as the pack has members, named
// `rest$0`, `rest$1` - and those names are what `rest...` expands to at a
// call, which is the whole mechanism.
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

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
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
        if (consume(")")) break;
        expect(",");
    }
}

