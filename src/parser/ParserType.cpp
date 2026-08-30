// The parser: types as they are written.
//
// Class, struct, union and enum definitions, the declaration specifiers in
// front of a declarator and the declarator itself - which is the part of C++
// grammar that reads inside out, and the reason this file is not smaller.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Operator.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// `T::type`, `Value<T>::type`, `Holder<int>::value` - a member type reached
// through something that is already a type.
//
// **In a pattern the answer is a dependent member and not a lookup.** The
// owner is a template parameter or a class made from one, so there is nothing
// to look in yet; Itanium wants the pattern spelled anyway, and
// `N5ValueIT_E4typeE` is what it wants. Everywhere else the member is looked
// up for real - and *not finding it is the failure SFINAE is made of*, which
// is why this says so through src_.fail rather than answering null.
const Type *Parser::memberTypeWalk(const Type *t) {
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const std::string member = peekAt(1).text;
        if (patternOnly_ &&
            (t->kind() == Kind::TemplateParam || t->kind() == Kind::DependentMember ||
             (t->isStructOrUnion() && t->isSpecialization()))) {
            at_ += 2;
            t = types_.dependentMember(t, member);
            continue;
        }
        if (!t->isStructOrUnion()) break;
        const Type *found = lookupInClass(t, member);
        if (found == nullptr)
            src_.fail(peekAt(1).pos, "'" + t->tag() + "' has no member type "
                                     "called '" + member + "'");
        at_ += 2;
        t = found;
    }
    return t;
}

const Type *Parser::structOrUnionSpecifier(Kind kind, bool isClass) {
    const char *what = isClass ? "class" : (kind == Kind::Struct ? "struct" : "union");
    std::size_t pos = peek().pos;

    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }

    // **A class written inside another is named through it.** The tag every
    // table here is keyed by becomes "Outer::Inner", so a nested class cannot
    // collide with a global of the same name, and the single component is
    // kept beside it because that is what both ABIs actually spell.
    //
    // A *mention* rather than a definition names whatever is already in
    // scope - `struct Node *p;` inside a class is the global Node - so the
    // qualification only happens where a body follows.
    const Type *within = classStack_.empty() ? nullptr : classStack_.back();
    std::string local = tag;
    const bool defining = peek().is("{") || peek().is(":");

    // **A specialization is named by its whole argument list.** The tag every
    // table here is keyed by becomes "Box<int,3>", which nested classes
    // already made possible: the tag was an arbitrary qualified string with
    // localName() and enclosing() beside it before templates needed one.
    std::string specializationOf;
    if (!classInstantiationTag_.empty() && defining) {
        // An explicit specialization has already had `Box<int>` read off it,
        // so there is no identifier here and the template's name comes along
        // beside the tag. An implicit one still has its `Box` in hand.
        specializationOf = classInstantiationOf_.empty() ? tag
                                                         : classInstantiationOf_;
        tag = classInstantiationTag_;
        local = specializationOf;
        classInstantiationTag_.clear();
        classInstantiationOf_.clear();
    }

    if (within != nullptr && !tag.empty()) {
        if (!defining) {
            if (const Type *had = findTypedef(tag))
                if (had->isStructOrUnion()) return had;
        }
        tag = within->tag() + "::" + tag;
    }

    Type *type = tag.empty() ? types_.anonymousStruct(kind)
                             : types_.structType(kind, tag);
    if (!specializationOf.empty()) {
        type->setLocalName(tag);
        type->setSpecialization(specializationOf, instantiatingArgs_);
        // **The injected class name.** Inside `Holder`'s own body the word
        // `Holder` means this specialization, not the template - which is
        // what makes `const Holder &` a legal parameter there. Registered as
        // a member type name, so the walk a nested class already needs finds
        // it: from inside the body through classStack_, and from a member's
        // own body through currentClass_.
        declareTypeName(tag + "::" + specializationOf, type);
    }
    if (within != nullptr && !local.empty()) {
        type->setLocalName(local);
        type->setEnclosing(within);
    }
    // Only a definition decides this. `class X;` followed by `struct X { };`
    // is one type written two ways - the standard allows the mix and says the
    // keywords are interchangeable here - so the body is what sets it and a
    // mere mention never unsets it.
    if (peek().is("{") || peek().is(":")) type->setDeclaredClass(isClass);
    if (!tag.empty()) declareTypeName(tag, type);

    // `class Derived : public Base {` - the base-clause. Default access is
    // private for a class and public for a struct, the same split as members.
    // The base-clause, which may now name more than one. They are laid down in
    // the order written, each at the offset the one before it ended at, and
    // that order is also the order their constructors run in.
    struct WrittenBase { const Type *type; Access access; };
    std::vector<WrittenBase> written;
    if (peek().is(":")) {
        at_++;
        for (;;) {
            Access how = isClass ? Access::Private : Access::Public;
            if (peek().is("virtual"))
                src_.fail(peek().pos, "a virtual base is not supported yet");
            if (peek().is("public"))         { how = Access::Public;    at_++; }
            else if (peek().is("protected")) { how = Access::Protected; at_++; }
            else if (peek().is("private"))   { how = Access::Private;   at_++; }

            std::size_t bpos = peek().pos;
            // **Read the base as a type rather than as a name.** A base may
            // be written `A<T>`, and a template-id is not something
            // findTypedef can answer: the class does not exist until it is
            // instantiated. specifiers() is the code that already knows how
            // to turn one into a type, and it is reached here for the same
            // reason a declaration reaches it.
            //
            // Inside a template this costs nothing extra, because a pattern
            // is never parsed: instantiation replays the tokens with the
            // parameters bound, so `A<T>` is read as `A<int>` and instantiated
            // then, at the point where T is known.
            std::string baseName = peek().kind == TokenKind::Ident
                                       ? peek().text : std::string();
            const Type *b = nullptr;
            if (atTypeName()) {
                StorageClass bsc;
                Qualifiers bquals;
                b = specifiers(&bsc, &bquals);
                if (b != nullptr) b = b->unqualified();
            } else {
                baseName = expectIdent("a base class name");
            }
            if (b == nullptr || !b->isStructOrUnion())
                src_.fail(bpos, "'" + baseName + "' is not a class, so it "
                                "cannot be a base");
            if (!b->isComplete())
                src_.fail(bpos, "'" + baseName + "' is not defined yet - a base "
                                "class has to be complete, because the derived "
                                "object contains one");
            written.push_back(WrittenBase{ b, how });
            if (!consume(",")) break;
        }
    }
    const Type *base = written.empty() ? nullptr : written[0].type;

    if (!peek().is("{")) {
        if (tag.empty()) src_.fail(pos, std::string(what) + " needs a tag or a body");
        return type;
    }
    at_++;

    if (type->isComplete())
        src_.fail(pos, std::string(what) + " " + tag + " is defined twice");

    // From here to the '}' this class is the innermost scope, which is what
    // makes `Inner` inside it mean `Outer::Inner`.
    classStack_.push_back(type);

    std::vector<Member> members;
    int widest = 1;
    long long bitCursor = 0;
    long long widestBits = 0;

    // **The base subobject is laid down first, at offset 0**, and its members
    // are copied in at the offsets they already have. That is not a shortcut
    // around lookup - it is what the layout IS, and it means `d.b` finds an
    // inherited member with no second search. What the copy loses is which
    // class declared the member, which nothing needs yet.
    //
    // Access travels through the inheritance: a public member of a private
    // base is private in the derived class, and a private member of any base
    // stays out of reach either way.
    for (std::size_t bi = 0; bi < written.size(); bi++) {
        const Type *b = written[bi].type;
        const Access how = written[bi].access;

        // Each base starts where the last one's data ended, aligned to its own
        // requirement. The first therefore sits at 0 and the rest do not.
        long long byteCursor = (bitCursor + 7) / 8;
        const int at = static_cast<int>(alignTo(byteCursor, b->align(target_)));

        const std::vector<Member> &inherited = b->members();
        for (std::size_t i = 0; i < inherited.size(); i++) {
            Member m = inherited[i];
            m.offset += at;
            if (m.access == Access::Private) m.access = Access::Private;
            else if (how == Access::Private) m.access = Access::Private;
            else if (how == Access::Protected) m.access = Access::Protected;
            members.push_back(m);
        }
        type->addBase(b, at, how);

        // The base's DATA size, not its sizeof - see Type::dataSize.
        bitCursor = static_cast<long long>(at + b->dataSize()) * 8;
        if (b->align(target_) > widest) widest = b->align(target_);
        // The first base's slots come down in order, and an override in this
        // class replaces one rather than appending - declareMember does that.
        if (!tag.empty() && bi == 0 && b->polymorphic())
            vtables_[tag] = vtables_[b->tag()];
    }

    // **A polymorphic object carries a vptr at offset 0**, so its members
    // start after it - measured: a class with one int and one virtual is 16
    // bytes with the int at 8. The pointer is reserved before any member is
    // placed, and a derived class inherits the base's rather than adding a
    // second: one class, one vptr, however deep the chain.
    const bool inheritsVptr = base != nullptr && base->polymorphic();
    const std::size_t firstOwnMember = members.size();

    // **The one difference between the two keywords.** [class.access]: a class
    // starts private and a struct starts public, and everything else about
    // them is the same - which is why they share this function rather than
    // having one each.
    Access access = isClass ? Access::Private : Access::Public;

    while (!peek().is("}")) {
        if (peek().kind == TokenKind::End) src_.fail(pos, "unclosed '{'");

        if (peek().is("template"))
            src_.fail(peek().pos, "a member template is not supported yet");

        // Where this member's declaration begins. A body written here is
        // replayed from exactly this token, so the replay re-reads the return
        // type and parameters rather than trying to rebuild them.
        std::size_t itemStart = at_;

        // Set when a body was held and stepped over: a definition ends at its
        // '}' and has no ';' to consume, unlike every other member.
        bool heldBody = false;

        // A constructor has the class's own name and no return type, so it
        // has to be seen before specifiers() is asked for one - the name is a
        // registered type name by now and would be read as the type.
        if (!tag.empty() && peek().kind == TokenKind::Ident &&
            peek().text == local && peekAt(1).is("(")) {
            std::size_t cpos = peek().pos;
            at_++;
            declareConstructor(tag, cpos, access);
            if (peek().is("{") || peek().is(":")) {
                pendingBodies_.push_back(PendingBody{ tag, itemStart, local,
                                                      constructorKey(tag) });
                skipBracedBlock();
                continue;
            }
            expect(";");
            continue;
        }

        // **The replay must not start at `virtual`.** A held body is re-read
        // through the ordinary out-of-line path, and out of line the keyword
        // is not written - C++ puts it on the declaration inside the class
        // and nowhere else. So it is stepped over here and the replay begins
        // at the return type, which is what specifiers() is able to read.
        bool isVirtual = false;
        if (peek().is("virtual")) { isVirtual = true; at_++; itemStart = at_; }

        // `~Point();` - a destructor, recognised the same way and for the same
        // reason as a constructor: it has no return type and its name is the
        // class, so specifiers() must not be asked for one.
        if (!tag.empty() && peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
            peekAt(1).text == local && peekAt(2).is("(")) {
            std::size_t dpos = peek().pos;
            at_ += 2;
            declareDestructor(tag, dpos, access, isVirtual);
            if (peek().is("{")) {
                pendingBodies_.push_back(PendingBody{ tag, itemStart, local,
                                                      destructorKey(tag) });
                skipBracedBlock();
                continue;
            }
            expect(";");
            continue;
        }

        if ((peek().is("public") || peek().is("private") || peek().is("protected")) &&
            peekAt(1).is(":")) {
            access = peek().is("public")  ? Access::Public
                   : peek().is("private") ? Access::Private
                                          : Access::Protected;
            at_ += 2;
            continue;
        }

        // `friend int peek(const Account &a);` - a declaration written inside
        // a class that declares nothing in it.
        //
        // **[class.friend]: the function belongs to the enclosing namespace
        // and what the class gives it is access.** So this reads an ordinary
        // function declaration, hands it to the same `declareFunction` a file-
        // scope one goes to, and then records the grant. Nothing about it is a
        // member: it has no `this`, it is not in the class's function table,
        // it is not mangled into the class, and `private:` above it changes
        // nothing - [class.friend]/9 says the access specifier a friend
        // declaration sits under is ignored, which falls out here rather than
        // needing a rule, because `access` is never read on this path.
        if (peek().is("friend")) {
            const std::size_t fpos = peek().pos;
            at_++;
            if (tag.empty())
                src_.fail(fpos, "an anonymous class has no name to grant "
                                "friendship with");
            if (peek().is("class") || peek().is("struct") || peek().is("union"))
                src_.fail(fpos, "'friend class' is not supported yet - it "
                                "grants every member function of another class "
                                "access at once, where this grants one named "
                                "function");
            StorageClass fsc;
            Qualifiers fquals;
            const Type *fbase = specifiers(&fsc, &fquals);
            if (fsc != StorageNone)
                src_.fail(fpos, "a friend declaration takes no storage class - "
                                "the function it names is somebody else's");
            Declared fd = declarator(fbase);
            if (!fd.qualifier.empty())
                src_.fail(fd.pos, "befriending one member function of another "
                                  "class is not supported yet - '" +
                                  fd.qualifier + "::" + fd.name + "' would have "
                                  "to be found before that class is complete");
            if (!peek().is("("))
                src_.fail(fd.pos, "a friend declaration declares a function, "
                                  "and '" + fd.name + "' is not one - a friend "
                                  "gets access, and only something that runs "
                                  "can use it");
            std::vector<const Type *> fparams;
            bool fvariadic = false;
            parameterTypes(fparams, fvariadic);
            if (peek().is("const"))
                src_.fail(peek().pos, "'const' here would say the function has "
                                      "a 'this' to leave alone, and a friend "
                                      "is not a member function");
            if (peek().is("{"))
                src_.fail(peek().pos, "a friend function defined inside the "
                                      "class is not supported yet - declare it "
                                      "here and define it outside, where its "
                                      "body is parsed like any other");
            declareFunction(fd.name, fd.type, fparams, fvariadic, false, fd.pos,
                            false);
            // **The grant is to this function, not to its name.** Recording
            // the name would befriend every overload of it, including ones
            // declared later that the class never saw.
            friends_[tag].push_back(
                lookupSignature(fd.name, fparams, fvariadic, fd.pos).symbol);
            expect(";");
            continue;
        }

        StorageClass msc;
        Qualifiers mquals;
        const Type *base = specifiers(&msc, &mquals);

        // **A typedef inside a class names a type and declares no member.**
        // It is keyed "S::value", which is the same qualified key a nested
        // class already uses - so it is found from inside the class through
        // classStack_, from a member's body through currentClass_, and from
        // outside as `S::value` through the walk that reads `Outer::Inner`.
        if (msc == StorageTypedef) {
            if (tag.empty())
                src_.fail(peek().pos, "a typedef needs a class with a name - "
                                      "this one is anonymous");
            do {
                Declared td = declarator(base);
                typedefFunctionSuffix(td);
                if (td.name.empty())
                    src_.fail(td.pos, "this typedef names nothing");
                declareTypeName(tag + "::" + td.name, td.type);
            } while (consume(","));
            expect(";");
            continue;
        }

        if (msc != StorageNone && msc != StorageStatic)
            src_.fail(peek().pos, "'static' is the only storage class a member "
                                  "may have");

        // **`struct Inner { ... };` declares a type and no member.** A nested
        // class takes no room in the enclosing object, so there is nothing to
        // lay out and nothing to name - the specifier was the whole
        // declaration.
        if (peek().is(";")) {
            if (!base->isStructOrUnion() || base->tag().empty())
                src_.fail(peek().pos, "this declares nothing - a member needs a "
                                      "name");
            if (base->enclosing() != nullptr)
                types_.structType(base->kind(), base->tag())
                      ->setNestedAccess(access);
            at_++;
            continue;
        }
        for (;;) {
            if (peek().is(":")) {
                std::size_t cpos = peek().pos;
                at_++;
                long w = constantExpression("a bit-field width");
                if (!base->isInteger())
                    src_.fail(cpos, "a bit-field must have an integer type, not '" +
                                    base->describe() + "'");
                long long unitBits = base->size(target_) * 8;
                if (w < 0 || w > unitBits)
                    src_.fail(cpos, "a bit-field of " + std::to_string(w) +
                                    " bits does not fit in '" + base->describe() + "'");
                int a = base->align(target_);
                if (a > widest) widest = a;
                if (w == 0) {
                    bitCursor = alignTo(bitCursor, unitBits);
                } else if (kind != Kind::Union) {
                    if (bitCursor % unitBits + w > unitBits)
                        bitCursor = alignTo(bitCursor, unitBits);
                    bitCursor += w;
                }
                if (kind == Kind::Union && w > unitBits) w = unitBits;
                if (kind == Kind::Union && w > widestBits) widestBits = w;
                if (!consume(",")) break;
                continue;
            }

            Declared d = declarator(base);

            // A reference member has to be bound when the object is made,
            // which means a constructor, which is rung 3. Refusing it by name
            // is better than laying it out as if it were a pointer and having
            // every use of it read the wrong thing.
            //
            // **Only when it is a member at all.** `int &get() { ... }` is a
            // member *function* returning a reference, and at this point
            // d.type is still the return type - the '(' has not been read -
            // so without asking, a perfectly ordinary accessor was reported
            // as a reference data member. The same three ways of being a
            // function as everywhere else: the '(' ahead, parameters recorded
            // to re-read, or a function type reached through a typedef.
            const bool memberIsFunction = peek().is("(") || d.paramsAt != 0 ||
                                          d.type->isFunction();

            // **`constexpr` on a member function, taken off the return type
            // here as well.** The out-of-line path does the same for a free
            // function; a member written inside the class is declared here and
            // *defined* through that path when its held body is replayed, so
            // without this the two disagree and the class refuses its own
            // member - "declared to return 'const int' and this says 'int'".
            if (mquals.isConstexpr && memberIsFunction &&
                !d.type->isFunction())
                d.type = types_.withoutConst(d.type);

            if (!memberIsFunction && d.type->isReference())
                src_.fail(d.pos, "'" + d.name + "' is a reference member, and "
                                 "binding one needs a constructor - not "
                                 "supported yet; a pointer member works now");

            // **A static member is not part of the object**, so it leaves the
            // layout untouched and the cursor where it was.
            if (msc == StorageStatic) {
                if (peek().is("("))
                    src_.fail(d.pos, "'" + d.name + "' is a static member "
                                     "function, which is not supported yet - a "
                                     "static data member works now");
                declareStaticMember(tag, type, d, access);
                if (!consume(",")) break;
                continue;
            }

            if (peek().is(":")) {
                std::size_t cpos = peek().pos;
                at_++;
                long w = constantExpression("a bit-field width");
                if (!d.type->isInteger())
                    src_.fail(d.pos, "a bit-field must have an integer type, not '" +
                                     d.type->describe() + "'");
                long long unitBits = d.type->size(target_) * 8;
                if (w < 0)
                    src_.fail(cpos, "'" + d.name + "' has a bit-field width of " +
                                    std::to_string(w) + ", which cannot be negative");
                if (w == 0)
                    src_.fail(cpos, "'" + d.name + "' has a bit-field width of 0; "
                                    "only an unnamed bit-field may be zero, and it "
                                    "means 'start the next storage unit'");
                if (w > unitBits)
                    src_.fail(cpos, "'" + d.name + "' is " + std::to_string(w) +
                                    " bits, which does not fit in '" +
                                    d.type->describe() + "'");

                int a = d.type->align(target_);
                if (a > widest) widest = a;

                long long at, bitOff;
                if (kind == Kind::Union) {
                    at = 0;
                    bitOff = 0;
                    if (w > widestBits) widestBits = w;
                } else {
                    if (bitCursor % unitBits + w > unitBits)
                        bitCursor = alignTo(bitCursor, unitBits);
                    at = (bitCursor / unitBits) * d.type->size(target_);
                    bitOff = bitCursor % unitBits;
                    bitCursor += w;
                }
                members.push_back(Member{ d.name, d.type, static_cast<int>(at),
                                          static_cast<int>(w),
                                          static_cast<int>(bitOff), access });
                if (!consume(",")) break;
                continue;
            }

            // A '(' after the name is a member function, not a member. The
            // declarator leaves the parameter list for its caller to read -
            // that is how a free function is parsed too - so this reads it and
            // builds the function type from it.
            //
            // It goes in the same table free functions use, under
            // "Point::get", which is what gives members overload resolution
            // with no second implementation of it.
            if (peek().is("(")) {
                std::vector<const Type *> mparams;
                bool mvariadic = false;
                parameterTypes(mparams, mvariadic);
                d.type = types_.functionType(d.type, std::move(mparams), mvariadic);
                bool constThis = false;
                if (consume("const")) constThis = true;
                // **A `constexpr` member function is implicitly const in
                // C++11**, and that is a mangling difference rather than a
                // nicety: clang spells this one `_ZNK1B5twiceEi` and cxx1
                // spelled it `_ZN1B5twiceEi`, so an object file from each
                // would not have found the other's. Measured, not read.
                //
                // C++14 removed the rule, which is why clang warns about it
                // under -std=c++11 rather than being silent - and why a
                // compiler pinned to C++11, as this one is, has to keep it.
                if (mquals.isConstexpr) constThis = true;

                // **The body is held, not parsed.** It has to be able to see
                // members declared after it, so nothing in it can be read
                // until the class is closed - which is why this is a delayed
                // parse rather than a recursion.
                if (peek().is("{")) {
                    if (tag.empty())
                        src_.fail(d.pos, "a member function needs a class with "
                                         "a name - this one is anonymous");
                    declareMember(tag, d, constThis, access,
                                  kind == Kind::Union, isVirtual);
                    pendingBodies_.push_back(PendingBody{ tag, itemStart, local,
                                                          tag + "::" + d.name });
                    skipBracedBlock();
                    heldBody = true;
                    break;
                }
                if (tag.empty())
                    src_.fail(d.pos, "a member function needs a class with a "
                                     "name - this one is anonymous");
                declareMember(tag, d, constThis, access, kind == Kind::Union,
                              isVirtual);
                if (!consume(",")) break;
                continue;
            }

            if (isVirtual)
                src_.fail(d.pos, "'virtual' describes a function, and '" +
                                 d.name + "' is a data member");

            if (!d.type->isComplete())
                src_.fail(d.pos, "'" + d.name + "' has an incomplete type");
            int a = d.type->align(target_);
            if (a > widest) widest = a;
            long long byteCursor = (bitCursor + 7) / 8;
            long long at = (kind == Kind::Union) ? 0 : alignTo(byteCursor, a);
            members.push_back(Member{ d.name, d.type, static_cast<int>(at), 0, 0,
                                      access });
            long long endBits = (at + d.type->size(target_)) * 8;
            if (kind == Kind::Union) { if (endBits > widestBits) widestBits = endBits; }
            else bitCursor = endBits;
            if (!consume(",")) break;
        }
        if (!heldBody) expect(";");
    }
    expect("}");
    classStack_.pop_back();

    // The class is polymorphic if it declared a virtual or inherited one, and
    // that is only knowable now - so the vptr is made room for here rather
    // than before the body, by moving this class's own members up by a
    // pointer. Inherited members are already where the base put them, and a
    // base that was polymorphic already counted its own vptr in its size.
    const bool anyVirtual = !tag.empty() && !vtables_[tag].empty();
    if (anyVirtual || inheritsVptr) type->setPolymorphic(true);

    if (anyVirtual && !inheritsVptr && kind != Kind::Union) {
        const int slot = 8;
        for (std::size_t i = firstOwnMember; i < members.size(); i++)
            members[i].offset += slot;
        bitCursor += static_cast<long long>(slot) * 8;
        if (widest < slot) widest = slot;
    }

    long long totalBits = (kind == Kind::Union) ? widestBits : bitCursor;

    // **An empty class is legal in C++ and has size 1**, where C required at
    // least one member. That rule arrived with member functions rather than
    // before them: a class holding only member functions has no data members
    // at all, and refusing it would have refused the ordinary shape of a class
    // that carries behaviour and no state. The size is one byte so that two
    // objects of it have different addresses, which is what the standard asks
    // for and not an arbitrary choice.
    //
    // **It changes the numbers and nothing else.** This used to return here,
    // which meant a class with no data members never reached the lines below:
    // its held member bodies were dropped, its implicit special members were
    // never declared, and a vtable would not have been emitted. A class
    // carrying only behaviour is the ordinary shape of one, and calling a
    // member of it linked to nothing. Found while a class template with two
    // type parameters would not link, which is what an empty one happened to
    // be.
    int size = static_cast<int>(alignTo((totalBits + 7) / 8, widest));
    int align = widest;
    if (members.empty() && totalBits == 0) { size = 1; align = 1; }
    // **An empty class has sizeof 1 and a data size of 0**, and the two are
    // different numbers on purpose. The 1 is so that two objects of it have
    // different addresses; the 0 is what every user of dataSize() wants,
    // which in each case is "how far into an object does this base's data
    // reach". For an empty base that is nowhere, so a derived class puts its
    // own members at offset 0 and the base costs nothing - the empty base
    // optimisation, which the Itanium ABI requires and clang and cl both do.
    //
    // Written as 1 here, `struct D : E { int x; };` was 8 bytes where clang
    // says 4, and every class with an empty base had a layout that agreed
    // with nothing. A recursive variadic class feels it hardest: it bottoms
    // out in an empty specialization and pays for it at every level.
    //
    // **The one case this does not handle** is two subobjects of the same
    // empty type in one object, which Itanium requires to have different
    // addresses and this would place both at 0. That needs `struct C : A, B`
    // where B also derives from A, and it is not reachable while a repeated
    // base is refused for its own reasons.
    type->setDataSize(members.empty() && totalBits == 0
                          ? 0 : static_cast<int>((totalBits + 7) / 8));
    type->complete(members, size, align);
    // Held bodies are read now, with the class complete: every member exists,
    // so a body may name one declared below it. Taken out of the vector first,
    // because a body may itself define a class with held bodies of its own.
    std::vector<PendingBody> mine;
    for (std::size_t i = 0; i < pendingBodies_.size(); i++)
        if (pendingBodies_[i].tag == tag) mine.push_back(pendingBodies_[i]);
    if (!mine.empty()) {
        std::vector<PendingBody> rest;
        for (std::size_t i = 0; i < pendingBodies_.size(); i++)
            if (pendingBodies_[i].tag != tag) rest.push_back(pendingBodies_[i]);
        pendingBodies_.swap(rest);
    }

    declareImplicitSpecials(tag, type, pos);
    // **Whether copying this is a call decides how it is passed**, and the
    // question is settled here because it is settled for both reasons at
    // once: a copy constructor exists at this point if the class wrote one or
    // if one was just declared for it, and one was just declared exactly when
    // a base or member made the copy non-trivial.
    if (copyConstructorOf(type) != nullptr || moveConstructorOf(type) != nullptr)
        type->setNonTrivialCopy(true);
    if (destructorOf(type) != nullptr) type->setHasDestructor(true);
    if (type->polymorphic()) emitVtable(type, tag, pos);
    // **A specialization's member bodies are not replayed here.** This is in
    // the middle of whatever asked for the class - which may be a declaration
    // inside a function - and a replay goes through topLevel, which clears
    // the locals of the function being parsed. They are handed back instead
    // and replayed by the same pass that defines function specializations.
    if (!specializationOf.empty() && deferSpecializationBodies_)
        heldForSpecialization_ = std::move(mine);
    else
        replayInlineBodies(std::move(mine));
    return type;
}

const Type *Parser::enumSpecifier() {
    std::size_t pos = peek().pos;
    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }

    // The tag names a type, as a class tag does. What it does not yet name is
    // a *distinct* type: an enumeration is still int here, so the conversions
    // C++ refuses in both directions are accepted. docs/CONFORMANCE.md has it.
    if (!tag.empty()) declareTypeName(tag, types_.intType());

    if (!peek().is("{")) return types_.intType();
    at_++;

    long long next = 0;
    while (!peek().is("}")) {
        std::size_t npos = peek().pos;
        std::string name = expectIdent("an enumerator");
        if (findEnum(name)) src_.fail(npos, "'" + name + "' is declared twice");
        if (consume("="))
            next = narrowTo(constantExpression("a constant"), types_.intType());
        enumIndex_[name] = enums_.size();
        enums_.push_back(EnumConst{ name, next });
        next = next + 1;
        if (!consume(",")) break;
    }
    expect("}");
    if (enums_.empty()) src_.fail(pos, "enum has no enumerators");
    return types_.intType();
}

// The specifiers are read without their qualifiers here, and specifiers()
// folds the const in afterwards. It reads 'const' in two places - before the
// type name and after it - and both must be collected before the type can be
// built, so this cannot be done as it goes.
const Type *Parser::specifiers(StorageClass *storage, Qualifiers *quals) {
    Qualifiers discard;
    if (quals == nullptr) quals = &discard;
    const Type *t = unqualifiedSpecifiers(storage, quals);
    return quals->isConst ? types_.withConst(t) : t;
}

const Type *Parser::unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals) {
    std::size_t start = peek().pos;
    *storage = StorageNone;

    for (;;) {
        if (consume("static"))  { *storage = StorageStatic; continue; }
        if (consume("extern"))  { *storage = StorageExtern; continue; }
        if (consume("typedef")) { *storage = StorageTypedef; continue; }
        if (consume("const"))    { quals->isConst = true; continue; }
        // **`constexpr` on an object is `const` plus a demand.** [dcl.constexpr]
        // makes the object const, and the rest of the compiler wants to know
        // nothing else about it - which is why this sets both and why almost
        // nothing downstream mentions constexpr at all.
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (consume("volatile")) { quals->isVolatile = true; continue; }
        if (consume("register")) { *storage = StorageRegister; continue; }
        if (consume("auto"))     { *storage = StorageAuto; continue; }
        break;
    }

    // wchar_t is a type of its own in C++, not the typedef C makes it. It is
    // spelled here rather than in <stddef.h>, which cannot declare it: the
    // name is a keyword, and a keyword is not something a typedef can name.
    if (consume("wchar_t")) return types_.get(target_.wcharType());
    // **`Point::Point(...)` has no type before the name, and the name is a
    // type.** So the specifier list has to decline it: it answers void and
    // consumes nothing, which leaves `Point::Point` for the declarator's
    // qualified-name path to read exactly as it reads `Point::get`. Every
    // other route would have meant a second copy of the definition machinery.
    // The same question has to be asked at every level once a class can be
    // written inside another - `Outer::Inner::Inner(` - which is the walk in
    // atUntypedMemberDefinition.
    if (atUntypedMemberDefinition()) return types_.get(Kind::Void);

    // Replaying an inline constructor or destructor: the tokens are `X(` or
    // `~X(` with no type in front, exactly as they were written in the class.
    if (!inlineOwner_.empty() &&
        ((peek().kind == TokenKind::Ident && peek().text == inlineOwnerName_ &&
          peekAt(1).is("(")) ||
         (peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
          peekAt(1).text == inlineOwnerName_)))
        return types_.get(Kind::Void);

    // **`typename` is a hint this compiler does not need, so it is read and
    // dropped.** It exists to tell a C++ parser that a dependent qualified
    // name is a type, which matters only where a template body is parsed
    // before its arguments are known - and this one replays a body at
    // instantiation, where the name is looked up like any other. Accepted
    // rather than refused so that a file written for clang compiles here too.
    if (consume("typename")) {
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'typename' introduces a qualified type "
                                  "name, and this is not one");
    }

    // A class template with its arguments *is* a type. A function template
    // named where a type was expected is not, and is refused by name.
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
        peekAt(1).is("<")) {
        const TemplateDecl decl = templates_[peek().text];
        if (!decl.isClass) refuseTemplateId();
        const std::size_t tpos = peek().pos;
        at_++;
        const Type *cls = instantiateClass(decl, tpos);

        return memberTypeWalk(cls);
    }

    if (peek().is("decltype")) return decltypeSpecifier();

    if (peek().is("struct")) { at_++; return structOrUnionSpecifier(Kind::Struct); }
    if (peek().is("class"))  { at_++; return structOrUnionSpecifier(Kind::Struct, true); }
    if (peek().is("union"))  { at_++; return structOrUnionSpecifier(Kind::Union); }
    if (peek().is("enum"))   { at_++; return enumSpecifier(); }
    if (peek().kind == TokenKind::Ident) {
        // **`Outer::Inner x;` - a nested class named from outside.** Asked
        // before the plain lookup, which would take only "Outer" and leave
        // "::Inner" for the declarator to read as the name being declared.
        // The longest prefix that names a type wins.
        if (peekAt(1).is("::") && peekAt(2).kind == TokenKind::Ident) {
            std::string q = peek().text;
            const Type *found = nullptr;
            std::size_t consumed = 0;
            for (std::size_t k = 1; peekAt(k).is("::") &&
                                    peekAt(k + 1).kind == TokenKind::Ident;
                 k += 2) {
                q += "::" + peekAt(k + 1).text;
                if (const Type *n = findTypedef(q)) {
                    found = n;
                    consumed = k + 2;
                }
            }
            if (found != nullptr) {
                // A nested class is a member, and `private:` reaches it.
                if (found->enclosing() != nullptr &&
                    found->nestedAccess() != Access::Public &&
                    !insideClass(found->enclosing()))
                    src_.fail(peek().pos, "'" + found->localName() + "' is " +
                                          (found->nestedAccess() == Access::Private
                                               ? "private" : "protected") +
                                          " in '" +
                                          found->enclosing()->tag() + "'");
                at_ += consumed;
                return found;
            }
        }
        if (const Type *t = findTypedef(peek().text)) {
            at_++;
            return memberTypeWalk(t);
        }
    }

    int isVoid = 0, isBool = 0, isChar = 0, isShort = 0, isInt = 0, isLong = 0;
    int isSigned = 0, isUnsigned = 0, isFloat = 0, isDouble = 0;

    while (atTypeName()) {
        // atTypeName() is also true for an identifier naming a typedef, and
        // nothing below consumes one - so without this the loop spins forever
        // on "typedef long T;" where T is already a typedef. A typedef name
        // used *as* the type was taken above, before this loop; reaching one
        // here means it is the declarator's name, or a mistake, and either way
        // the specifiers are finished.
        //
        // Inherited from Compiler-C, where it hangs too: no case in 425
        // refuses a redeclaration, so nothing ever reached it. A compiler that
        // loops on bad input is worse than one that says no.
        if (peek().kind == TokenKind::Ident) break;
        if (consume("const"))         { quals->isConst = true; continue; }
        if (consume("constexpr")) {
            quals->isConst = true;
            quals->isConstexpr = true;
            continue;
        }
        if (consume("volatile"))      { quals->isVolatile = true; continue; }
        if (consume("float"))         isFloat++;
        else if (consume("double"))   isDouble++;
        else if (consume("void"))     isVoid++;
        else if (consume("bool"))     isBool++;
        else if (consume("char"))     isChar++;
        else if (consume("short"))    isShort++;
        else if (consume("int"))      isInt++;
        else if (consume("long"))     isLong++;
        else if (consume("signed"))   isSigned++;
        else if (consume("unsigned")) isUnsigned++;
    }

    if (isBool && (isVoid || isChar || isShort || isInt || isLong ||
                   isSigned || isUnsigned || isFloat || isDouble))
        src_.fail(start, "'bool' cannot be combined with another specifier");
    if (isSigned && isUnsigned)
        src_.fail(start, "'signed' and 'unsigned' together is not a type");
    if (isVoid && (isChar || isShort || isInt || isLong || isSigned || isUnsigned))
        src_.fail(start, "'void' cannot be combined with another specifier");
    if (isChar && (isShort || isInt || isLong))
        src_.fail(start, "'char' cannot be combined with that");
    if (isShort && isLong) src_.fail(start, "'short long' is not a type");
    if (isLong > 2)        src_.fail(start, "'long long' is not a type");
    if ((isFloat || isDouble) && (isChar || isShort || isInt || isSigned || isUnsigned))
        src_.fail(start, "a floating type cannot be combined with that");
    if (isFloat && isDouble)
        src_.fail(start, "'float double' is not a type");
    if (isDouble && isLong > 1)
        src_.fail(start, "'long long double' is not a type");

    if (isBool)   return types_.get(Kind::Bool);
    if (isFloat)  return types_.get(Kind::Float);
    if (isDouble) return types_.get(isLong ? Kind::LongDouble : Kind::Double);
    if (isVoid)  return types_.get(Kind::Void);
    if (isChar)  return types_.get(isUnsigned ? Kind::UChar
                                  : isSigned ? Kind::SChar : Kind::Char);
    if (isShort) return types_.get(isUnsigned ? Kind::UShort : Kind::Short);
    if (isLong == 2) return types_.get(isUnsigned ? Kind::ULongLong : Kind::LongLong);
    if (isLong)  return types_.get(isUnsigned ? Kind::ULong : Kind::Long);
    if (isInt || isSigned || isUnsigned)
        return types_.get(isUnsigned ? Kind::UInt : Kind::Int);

    // In C++11 'auto' is a type specifier, not the storage class C90 made it.
    // This parser still reads it as one, so reaching here having consumed it
    // is exactly the case where a type was meant to be deduced.
    // **In C++11 `auto` is a type specifier, not the storage class C90 made
    // it.** This parser still reads it as one, so reaching here having
    // consumed it is exactly the case where a type was meant to be deduced -
    // and now it is: the storage class is dropped and a stand-in returned.
    if (*storage == StorageAuto) {
        *storage = StorageNone;
        const Type *deduced = types_.deducedType();
        return quals->isConst ? types_.withConst(deduced) : deduced;
    }
    // Same reason as in expectIdent, and this is the end a member declaration
    // reaches: `friend`, `mutable`, `explicit`, `using` and `static_assert`
    // all begin one in C++ and none of them begins one here, so without this
    // each is reported as a missing type at the keyword - which names the
    // right token and tells the reader nothing about it.
    // **A declaration whose *type* is `operator` is a conversion function**,
    // and nothing else: `operator int() const` says what it converts to where
    // every other declaration says what it is. Reaching here having found no
    // type is how that is recognised, so it is named here rather than being
    // handed to the generic refusal below, which would say only that the
    // keyword is unsupported and leave the reader to guess which half of it.
    if (peek().is("operator"))
        src_.fail(peek().pos, "a conversion function is not supported yet - "
                              "this declaration names a type to convert to "
                              "where every operator that can be overloaded "
                              "here is punctuation");
    if (const char *pending = notYetSupported(peek().text))
        src_.fail(peek().pos, std::string("'") + pending +
                              "' is not supported yet");
    if (*storage != StorageNone || quals->isConst || quals->isVolatile)
        src_.fail(start, "this declaration has no type; write one");
    src_.fail(start, "expected a type");
}

const Type *Parser::arraySuffix(const Type *base, std::size_t pos) {
    if (base->isReference() && peek().is("["))
        src_.fail(peek().pos, "there is no array of references - an array's "
                              "elements are objects, and a reference is not "
                              "one");
    std::vector<long long> dims;
    while (consume("[")) {
        if (consume("]")) { dims.push_back(-1); continue; }
        std::size_t dpos = peek().pos;
        long n = constantExpression("an array length");
        if (n <= 0)
            src_.fail(dpos, "an array length must be positive, not " +
                            std::to_string(n));
        dims.push_back(n);
        expect("]");
    }
    for (std::size_t i = 1; i < dims.size(); i++)
        if (dims[i] < 0)
            src_.fail(pos, "only the first dimension may be left empty - the "
                           "others decide how far one step moves");

    for (std::size_t i = dims.size(); i-- > 0; )
        base = types_.arrayOf(base, dims[i]);
    return base;
}

// `operator` and then the operator itself, read where a declarator wants a
// name. What comes back is the whole of it - "operator+", punctuation
// included - because that is the name the declaration carries from here on:
// the function tables key it exactly as they key `get`, and overload
// resolution, access and mangling all needed to learn nothing about operators
// in order to hold one.
//
// **Everything this will not take, it refuses by name.** Each is a real
// operator function, and a reader who wrote one is owed better than "expected
// a name" pointing at the punctuation after the keyword.
std::string Parser::operatorName() {
    const std::size_t pos = peek().pos;
    at_++;                                    // `operator`

    // The two that are written as a *pair* of tokens. `operator()` has to be
    // read here rather than left to the parameter list below, which would
    // take the `()` for an empty one and leave the declaration with no name.
    if (peek().is("(")) { at_++; expect(")"); return "operator()"; }
    if (peek().is("[")) { at_++; expect("]"); return "operator[]"; }

    const std::string spelling = peek().text;

    if (spelling == "new" || spelling == "delete")
        src_.fail(pos, "'operator " + spelling + "' is not supported yet - "
                       "a new-expression here calls the platform's '" +
                       spelling + "' by name, and replacing that one is more "
                       "than giving this a name");
    if (spelling == "->*")
        src_.fail(pos, "'operator->*' is not supported yet");
    if (peek().kind == TokenKind::Str)
        src_.fail(pos, "a user-defined literal is not supported yet");
    if (peek().kind != TokenKind::Punct)
        src_.fail(pos, "a conversion function is not supported yet - "
                       "'operator " + spelling + "' names a type to convert "
                       "to, where every operator this compiler can overload "
                       "is punctuation");
    if (findOperator(spelling) == nullptr)
        src_.fail(peek().pos, "'" + spelling + "' is not an operator, so "
                              "there is nothing here to overload");
    at_++;
    return "operator" + spelling;
}

// An operator this compiler can *name* but cannot yet reach from an
// expression, refused where it is declared.
//
// **The declaration is the right place and the name is the wrong one.** The
// mangler can spell every overloadable operator and does, checked against
// clang on all three targets - so what is missing here is the dispatch, and
// which dispatch is missing depends on how many operands the operator was
// written with: `operator-` with one parameter is a subtraction and reaches a
// class fine, and with none it is a negation and there is no path to it. That
// is a question about the parameter list, so it is asked once the parameter
// list has been read, and not back where the name was.
//
// Accepting one of these quietly would leave a function that links, has the
// name clang gives it, and can never be called - which is the shape of bug
// this project refuses by name everywhere else.
void Parser::checkOperatorDeclarable(const std::string &name, std::size_t params,
                                     bool member, std::size_t pos) {
    const std::string spelling = operatorSpelling(name);
    if (spelling.empty() || findOperator(spelling) == nullptr) return;

    // `this` is the first operand of a member operator and is not in the list.
    const std::size_t operands = params + (member ? 1 : 0);

    static const char *const binary[] = {
        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
        "==", "!=", "<", "<=", ">", ">="
    };
    if (operands == 2)
        for (const char *k : binary)
            if (spelling == k) return;

    static const char *const unary[] = {
        "+", "-", "*", "&", "!", "~", "++", "--"
    };
    if (operands == 1)
        for (const char *k : unary)
            if (spelling == k) return;

    // **The postfix increment is the one operator whose arity lies.** [over.inc]
    // gives it a dummy `int` that nobody passes and nobody names, so it counts
    // two operands here and is a unary operator all the same. Recognised by
    // that parameter being an int, which is the only shape the standard allows
    // it: a second parameter of any other type is not the postfix form.
    if (operands == 2 && (spelling == "++" || spelling == "--")) return;

    // The call operator has no arity to check: [over.call] lets it take
    // whatever it likes, and it has no non-member form to be confused with.
    if (spelling == "()" && member) return;
    if (spelling == "()")
        src_.fail(pos, "'operator()' has to be a non-static member function - "
                       "[over.call] gives it no non-member form, so there is no "
                       "class here for it to be the call operator of");

    const std::string how = operands == 1 ? "a unary " : "a binary ";
    src_.fail(pos, how + "'operator" + spelling + "' is not supported yet - "
                   "it can be given the name the linker wants, and there is no "
                   "path from an expression to it, so declaring one would make "
                   "a function nothing can call");
}

// `expectIdent` with the operator case in front of it. This stands wherever a
// declarator reads a name, which is three places: the plain one, the one
// after a `::`, and the one after a class template's argument list.
std::string Parser::declaredName(const char *what) {
    if (peek().is("operator")) return operatorName();
    return expectIdent(what);
}

Parser::Declared Parser::declarator(const Type *base, bool nameOptional,
                                    bool insideParens) {

    // The const after a star qualifies the pointer, not what it points at:
    // 'char * const p' is a const pointer to a writable char, and 'const char
    // *p' is the other way round. Both are now differences of type, so the
    // declarator has nothing left to remember about them.
    while (consume("*")) {
        base = types_.pointerTo(base);
        for (;;) {
            if (consume("const"))    { base = types_.withConst(base); continue; }
            if (consume("volatile")) continue;
            break;
        }
    }

    // A reference binds after every star - 'int *&r' is a reference to a
    // pointer - and there is nothing to write on the other side of it,
    // because a reference is not an object for a pointer to point at.
    // **`&&` binds like `&` and differs in what it will take.** The lowering
    // is the same - a slot holding an address, every mention a dereference -
    // so nothing below this line had to be told the difference.
    if (consume("&&")) {
        base = types_.rvalueReferenceTo(base);
        if (peek().is("&") || peek().is("&&"))
            src_.fail(peek().pos, "there is no reference to a reference");
        if (peek().is("*"))
            src_.fail(peek().pos, "there is no pointer to a reference");
        return declarator(base, nameOptional, insideParens);
    }
    if (consume("&")) {
        base = types_.referenceTo(base);
        if (peek().is("&") || peek().is("&&"))
            src_.fail(peek().pos, "there is no reference to a reference");
        if (peek().is("*"))
            src_.fail(peek().pos, "there is no pointer to a reference - a "
                                  "reference is not an object to point at");
        // [dcl.ref]/1: there is no const reference, only a reference to a
        // const. The distinction is worth keeping because the two are written
        // so nearly the same way.
        if (peek().is("const") || peek().is("volatile"))
            src_.fail(peek().pos, "a reference cannot be const or volatile "
                                  "itself - it never changes what it refers to "
                                  "anyway; 'const " +
                                  base->referent()->unqualified()->describe() +
                                  " &' is what qualifies what it refers to");
    }

    if (peek().is("(")) {
        std::size_t open = at_;
        at_++;
        bool wrapsAPointer = peek().is("*");

        declarator(types_.intType(), true, true);
        expect(")");

        std::size_t posOuter = peek().pos;
        const Type *outer;
        if (peek().is("(") && wrapsAPointer) {
            std::vector<const Type *> params;
            bool variadic = false;
            parameterTypes(params, variadic);
            outer = types_.functionType(base, std::move(params), variadic);
        } else {
            outer = arraySuffix(base, posOuter);
        }
        std::size_t after = at_;

        at_ = open + 1;
        Declared inner = declarator(outer, nameOptional, true);
        expect(")");
        at_ = after;
        return inner;
    }

    std::size_t pos = peek().pos;
    std::string name;
    std::string qualifier;

    // Replaying `~X() { ... }` from inside the class: the '~' belongs to the
    // name, and there is no '::' to hang it off.
    bool inlineDtor = false;
    if (!inlineOwner_.empty() && peek().is("~")) { at_++; inlineDtor = true; }

    // The operator test comes before the optional-name one: an abstract
    // declarator never says `operator`, so reaching it here is always a name.
    if (peek().is("operator")) name = operatorName();
    else if (nameOptional && peek().kind != TokenKind::Ident) name.clear();
    else name = expectIdent("a name");

    // **`Box<T, N>::size` - a class template's name where a class name goes.**
    // The plan said this rung was the qualified-name path with a template-id
    // in it, and this is the one place that had to be told: the name just
    // read is a class template, so what follows it is an argument list and
    // the class it makes is the qualifier. Everything after the `::` is read
    // by the loop below, unchanged.
    {
        auto tmpl = templates_.find(name);
        if (!name.empty() && peek().is("<") && tmpl != templates_.end() &&
            tmpl->second.isClass) {
            const std::size_t tpos = pos;
            const Type *cls = instantiateClass(tmpl->second, tpos);
            if (!peek().is("::"))
                src_.fail(peek().pos, "'" + cls->tag() + "' is a type here, "
                                      "and a declaration needs a name after "
                                      "it");
            at_++;
            qualifier = cls->tag();
            name = declaredName("a member name");
        }
    }

    if (!inlineOwner_.empty() && !name.empty() && !peek().is("::")) {
        qualifier = inlineOwner_;
        // **A specialization's constructor is written under the template's
        // name and keyed under the tag's.** The source says `Holder(`; the
        // table says "Holder<int>::Holder<int>", because that is what
        // constructorKey makes of the tag. Only the name that *is* the class
        // moves - an ordinary member keeps what it was written as.
        if (name == inlineOwnerName_ && inlineOwnerName_ != inlineOwner_)
            name = localOf(inlineOwner_);
        if (inlineDtor) name = "~" + name;
        inlineOwnerName_.clear();
        inlineOwner_.clear();     // one-shot: the body's own declarations are
                                  // ordinary locals, not members
    }

    // `int Point::get()` - the name before the '::' is the class, and what
    // follows is the member being defined. Only one level: a class inside a
    // class is not a thing this compiler has yet.
    // `int Point::get()` - the name before the '::' is the class and what
    // follows is the member. It repeats for a nested class, so that
    // `Outer::Inner::get` leaves the qualifier "Outer::Inner", which is the
    // qualified tag every table here is keyed by.
    while (!name.empty() && peek().is("::")) {
        at_++;
        qualifier = qualifier.empty() ? name : qualifier + "::" + name;
        bool destructor = consume("~");
        name = declaredName("a member name after '::'");
        if (destructor) name = "~" + name;
    }

    const Type *t = arraySuffix(base, pos);

    std::size_t paramsAt = 0;
    if (insideParens && peek().is("(")) {
        paramsAt = at_;
        std::vector<const Type *> ignored;
        bool ignoredVariadic = false;
        parameterTypes(ignored, ignoredVariadic);
    }

    return Declared{ name, t, pos, paramsAt, qualifier };
}

