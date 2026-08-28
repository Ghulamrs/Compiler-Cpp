#include "Parser.h"
#include "Mangle.h"
#include "Source.h"

#include <climits>
#include <cstring>

static int alignTo(int n, int a) { return (n + a - 1) / a * a; }

const Token &Parser::peekAt(std::size_t n) const {
    std::size_t i = at_ + n;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}

bool Parser::consume(const char *s) {
    if (!peek().is(s)) return false;
    at_++;
    return true;
}

void Parser::expect(const char *s) {
    if (!peek().is(s))
        src_.fail(peek().pos, std::string("expected '") + s + "'");
    at_++;
}

std::string Parser::expectIdent(const char *what) {
    if (peek().kind != TokenKind::Ident)
        src_.fail(peek().pos, std::string("expected ") + what);
    std::string name = peek().text;
    at_++;
    return name;
}

long long Parser::expectNumber(const char *what) {
    if (peek().kind != TokenKind::Num)
        src_.fail(peek().pos, std::string("expected ") + what);
    long long v = peek().value;
    at_++;
    return v;
}

const Type *Parser::findTypedef(const std::string &name) const {
    auto it = typedefIndex_.find(name);
    return it == typedefIndex_.end() ? nullptr : typedefs_[it->second].type;
}

// A class or enum name is a type name in C++, with no typedef written. The
// standard puts it that the name is inserted into the scope the definition
// appears in; here that is the one table this parser has for the purpose.
//
// What is not implemented is the C compatibility rule that lets an object of
// the same name hide the class name - "struct stat stat;" is legal C++ and is
// refused here. It costs a second lookup table to fix and no program in the
// corpus wants it.
void Parser::declareTypeName(const std::string &name, const Type *type) {
    if (findTypedef(name) != nullptr) return;
    typedefIndex_[name] = typedefs_.size();
    typedefs_.push_back(TypedefName{ name, type });
}

const Parser::EnumConst *Parser::findEnum(const std::string &name) const {
    auto it = enumIndex_.find(name);
    return it == enumIndex_.end() ? nullptr : &enums_[it->second];
}

bool Parser::atTypeName() const {
    static const char *const t[] = { "void", "bool", "char", "short", "int",
                                     "long", "signed", "unsigned", "wchar_t",
                                     "float", "double",
                                     "struct", "union", "enum",
                                     "const", "volatile" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    return peek().kind == TokenKind::Ident && findTypedef(peek().text) != nullptr;
}

bool Parser::atDeclarationStart() const {

    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef");
}

const Type *Parser::structOrUnionSpecifier(Kind kind, bool isClass) {
    const char *what = isClass ? "class" : (kind == Kind::Struct ? "struct" : "union");
    std::size_t pos = peek().pos;

    std::string tag;
    if (peek().kind == TokenKind::Ident) { tag = peek().text; at_++; }

    Type *type = tag.empty() ? types_.anonymousStruct(kind)
                             : types_.structType(kind, tag);
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
            std::string baseName = expectIdent("a base class name");
            const Type *b = findTypedef(baseName);
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

        // A constructor has the class's own name and no return type, so it
        // has to be seen before specifiers() is asked for one - the name is a
        // registered type name by now and would be read as the type.
        if (!tag.empty() && peek().kind == TokenKind::Ident && peek().text == tag &&
            peekAt(1).is("(")) {
            std::size_t cpos = peek().pos;
            at_++;
            declareConstructor(tag, cpos, access);
            if (peek().is("{"))
                src_.fail(peek().pos, "a constructor defined inside the class "
                                      "is not supported yet - define it outside "
                                      "with '" + tag + "::" + tag + "'");
            expect(";");
            continue;
        }

        bool isVirtual = false;
        if (peek().is("virtual")) { isVirtual = true; at_++; }

        // `~Point();` - a destructor, recognised the same way and for the same
        // reason as a constructor: it has no return type and its name is the
        // class, so specifiers() must not be asked for one.
        if (!tag.empty() && peek().is("~") && peekAt(1).kind == TokenKind::Ident &&
            peekAt(1).text == tag && peekAt(2).is("(")) {
            std::size_t dpos = peek().pos;
            at_ += 2;
            declareDestructor(tag, dpos, access, isVirtual);
            if (peek().is("{"))
                src_.fail(peek().pos, "a destructor defined inside the class is "
                                      "not supported yet - define it outside "
                                      "with '" + tag + "::~" + tag + "'");
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

        StorageClass msc;
        const Type *base = specifiers(&msc);
        if (msc != StorageNone)
            src_.fail(peek().pos, "a storage class on a member is not supported yet");
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
            if (d.type->isReference())
                src_.fail(d.pos, "'" + d.name + "' is a reference member, and "
                                 "binding one needs a constructor - not "
                                 "supported yet; a pointer member works now");

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
                if (peek().is("{"))
                    src_.fail(peek().pos,
                              "a member function defined inside the class is "
                              "not supported yet - the body would have to see "
                              "members declared after it, which means holding "
                              "it until the class is closed. Define it outside "
                              "with '" + (tag.empty() ? std::string("Class")
                                                      : tag) + "::" + d.name +
                              "'");
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
        expect(";");
    }
    expect("}");

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
    if (members.empty() && totalBits == 0) {
        type->setDataSize(1);
        type->complete(members, 1, 1);
        return type;
    }

    int size = static_cast<int>(alignTo((totalBits + 7) / 8, widest));
    type->setDataSize(static_cast<int>((totalBits + 7) / 8));
    type->complete(members, size, widest);
    if (type->polymorphic()) {
        // Nothing else sets the vptr, and an implicit default constructor is
        // not written yet - so a polymorphic class without one would hand out
        // objects whose vptr is whatever the frame held.
        if (!tag.empty() && overloadsOf(constructorKey(tag)) == nullptr)
            src_.fail(pos, "'" + tag + "' has a virtual function and no "
                           "constructor - the vptr is set by one, and an "
                           "implicit default constructor is not supported yet");
        emitVtable(type, tag, pos);
    }
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
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident && peekAt(2).text == peek().text &&
        peekAt(3).is("(") && findTypedef(peek().text) != nullptr)
        return types_.get(Kind::Void);

    // The same for a destructor, `Point::~Point(`, whose name carries a '~'.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).is("~") && peekAt(3).kind == TokenKind::Ident &&
        peekAt(3).text == peek().text && findTypedef(peek().text) != nullptr)
        return types_.get(Kind::Void);

    if (peek().is("struct")) { at_++; return structOrUnionSpecifier(Kind::Struct); }
    if (peek().is("class"))  { at_++; return structOrUnionSpecifier(Kind::Struct, true); }
    if (peek().is("union"))  { at_++; return structOrUnionSpecifier(Kind::Union); }
    if (peek().is("enum"))   { at_++; return enumSpecifier(); }
    if (peek().kind == TokenKind::Ident) {
        if (const Type *t = findTypedef(peek().text)) { at_++; return t; }
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
    if (*storage == StorageAuto)
        src_.fail(start, "'auto' as a deduced type is not supported yet - "
                         "write the type");
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
    if (peek().is("&&"))
        src_.fail(peek().pos, "an rvalue reference '&&' is not supported yet - "
                              "it comes with move semantics");
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
    if (nameOptional && peek().kind != TokenKind::Ident) name.clear();
    else name = expectIdent("a name");

    // `int Point::get()` - the name before the '::' is the class, and what
    // follows is the member being defined. Only one level: a class inside a
    // class is not a thing this compiler has yet.
    if (!name.empty() && peek().is("::")) {
        at_++;
        qualifier = name;
        bool destructor = consume("~");
        name = expectIdent("a member name after '::'");
        if (destructor) name = "~" + name;
        if (peek().is("::"))
            src_.fail(peek().pos, "a nested class is not supported yet - only "
                                  "'Class::member' names something here");
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

static bool isNullConstant(const Expr &e) {
    const Num *n = dynamic_cast<const Num *>(&e);
    return n != nullptr && n->type()->isInteger() && n->value() == 0;
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
const Parser::Signature &Parser::resolveOverload(const std::string &name,
                                                 const std::vector<ExprPtr> &args,
                                                 std::size_t pos) {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr)
        src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");

    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;

    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.variadic ? args.size() < f.params.size()
                       : args.size() != f.params.size()) continue;

        std::vector<Rank> r(args.size(), Rank::Ellipsis);
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            if (i >= f.params.size()) continue;      // reached by the ellipsis
            r[i] = rankArgument(*args[i], f.params[i]);
            if (r[i] == Rank::None) ok = false;
        }
        if (!ok) continue;
        viable.push_back((*set)[k]);
        ranks.push_back(r);
    }

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(functions_[(*set)[k]]);
        src_.fail(pos, why);
    }
    if (viable.size() == 1) return functions_[viable[0]];

    std::size_t best = 0;
    for (std::size_t k = 1; k < viable.size(); k++) {
        bool better = false, worse = false;
        for (std::size_t i = 0; i < args.size(); i++) {
            if (ranks[k][i] < ranks[best][i]) better = true;
            if (ranks[k][i] > ranks[best][i]) worse = true;
        }
        if (better && !worse) best = k;
    }
    for (std::size_t k = 0; k < viable.size(); k++) {
        if (k == best) continue;
        bool bestWins = false, bestLoses = false;
        for (std::size_t i = 0; i < args.size(); i++) {
            if (ranks[best][i] < ranks[k][i]) bestWins = true;
            if (ranks[best][i] > ranks[k][i]) bestLoses = true;
        }
        if (!(bestWins && !bestLoses)) {
            std::string why = "this call to '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < viable.size(); j++)
                why += "\n    candidate: " + describeSignature(functions_[viable[j]]);
            src_.fail(pos, why);
        }
    }
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

void Parser::enterScope() { scopeStarts_.push_back(locals_.size()); }

int Parser::enterBlock() {
    blocks_.push_back(currentBlock());
    int id = static_cast<int>(blocks_.size()) - 1;
    blockStack_.push_back(id);
    return id;
}

void Parser::leaveBlock() { blockStack_.pop_back(); }

void Parser::leaveScope() {
    locals_.resize(scopeStarts_.back());
    scopeStarts_.pop_back();
}

int Parser::allocateFrameSlot(const Type *type) {
    // What a reference occupies is a pointer, even though sizeof asks about
    // what it refers to. This is the one place the difference shows.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    frameSize_ += stored->size(target_);
    frameSize_ = alignTo(frameSize_, objectAlign(stored, target_));
    return frameSize_;
}

int Parser::declare(const std::string &name, const Type *type, std::size_t pos) {
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");

    int offset = allocateFrameSlot(type);
    locals_.push_back(Local{ name, offset, type, false, std::string() });
    // The debug record describes the storage, which for a reference is the
    // pointer it really is. DWARF has a tag for a reference and this does not
    // use it yet.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    fnVars_.push_back(::Local{ name, stored, offset, inParams_, std::string(),
                              currentBlock() });
    return offset;
}

void Parser::declareStaticLocal(const std::string &name, const Type *type,
                                std::size_t pos, const std::string &symbol) {
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");
    locals_.push_back(Local{ name, 0, type, false, symbol });
    fnVars_.push_back(::Local{ name, type, 0, false, symbol, currentBlock() });
}

const Parser::Local *Parser::findLocal(const std::string &name) const {
    for (std::size_t i = locals_.size(); i-- > 0; )
        if (locals_[i].name == name) return &locals_[i];
    return nullptr;
}

const Parser::GlobalSym *Parser::findGlobal(const std::string &name) const {
    auto it = globalIndex_.find(name);
    return it == globalIndex_.end() ? nullptr : &globals_[it->second];
}

Parser::GlobalSym *Parser::findGlobalToUpdate(const std::string &name) {
    auto it = globalIndex_.find(name);
    return it == globalIndex_.end() ? nullptr : &globals_[it->second];
}

const Type *Parser::composite(const Type *a, const Type *b) {
    if (a == b) return a;
    if (!a->isArray() || !b->isArray()) return nullptr;

    const Type *elem = composite(a->pointee(), b->pointee());
    if (elem == nullptr) return nullptr;

    long long la = a->length(), lb = b->length();
    if (la >= 0 && lb >= 0 && la != lb) return nullptr;
    return types_.arrayOf(elem, la >= 0 ? la : lb);
}

ExprPtr Parser::defaultPromote(ExprPtr e) {
    if (e->type()->kind() == Kind::Float)
        return convert(std::move(e), types_.doubleType());
    if (e->type()->isInteger()) {
        const Type *to = promote(e->type());
        return convert(std::move(e), to);
    }
    return e;
}

// The name the linker is given. 'main' keeps its own, by the rule that makes
// it findable at all; anything inside extern "C" keeps its own because that
// is what the linkage specification asked for; everything else is mangled in
// the ABI of the target being compiled for.
std::string Parser::functionSymbol(const std::string &name, const Type *returns,
                                   const std::vector<const Type *> &params,
                                   bool variadic, bool internal, std::size_t pos) {
    if (cLinkage_ > 0 || name == "main") return name;
    const Type *fn = types_.functionType(returns, params, variadic);
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftFunctionName(name, fn, internal, &out, &why)
            : itaniumFunctionName(name, fn, internal, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

// Building an object: the constructor is chosen from the arguments the way any
// overload is, and then called with the object's address in front of them. The
// object exists before the call - it is a frame slot like any other local - and
// what the constructor does is give its members values.
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

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");

    std::string key = destructorKey(cls);
    if (overloadsOf(key) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");

    const char code = access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out;
    if (target_.microsoftNames()) out = microsoftDestructorName(cls, code);
    else                          itaniumDestructorName(cls, true, &out);

    functionIndex_[key].push_back(functions_.size());
    functions_.push_back(Signature{ "~" + cls, out, types_.get(Kind::Void),
                                    params, false, false, pos, false, cls, false,
                                    access, isVirtual });

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
    const std::string deleting = ms
        ? "??_G" + cls + "@@UEAAPEAXI@Z"
        : "_ZN" + std::to_string(cls.size()) + cls + "D0Ev";

    std::vector<VSlot> &slots = vtables_[cls];
    std::vector<const Type *> none;
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (slots[i].name != "~") continue;
        slots[i].symbol = ms ? deleting : out;          // the complete form
        if (!ms && i + 1 < slots.size() && slots[i + 1].name == "~$deleting")
            slots[i + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
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
    const std::string symbol = ms
        ? "??_G" + cls + "@@UEAAPEAXI@Z"
        : "_ZN" + std::to_string(cls.size()) + cls + "D0Ev";
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
void Parser::emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                             std::size_t pos) {
    for (std::size_t i = alive_.size(); i > from; i--) {
        const Alive &a = alive_[i - 1];
        const Signature *dtor = destructorOf(a.cls);
        if (dtor == nullptr) continue;

        ExprPtr object(Var::local(a.name, a.offset));
        object->setType(a.cls);
        ExprPtr addr(new Unary('&', std::move(object)));
        addr->setType(types_.pointerTo(a.cls));
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
            ? microsoftConstructorName(cls, fn, code, &out, &why)
            : itaniumConstructorName(cls, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + cls + "' cannot be given a name the "
                       "linker can hold: " + why);

    set.push_back(functions_.size());
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
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

    const std::string symbol = memberSymbol(cls, d.name, fn, access, constThis,
                                            d.pos, isVirtual);
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });

    // **A slot is taken once and then kept.** An override replaces the entry
    // the base put there rather than adding one, which is what makes a
    // Base * and a Derived * agree about where to look. Matching is by name,
    // parameters and constness - the signature, minus the return type, which
    // is what [class.virtual] calls overriding.
    if (!isVirtual) return;
    std::vector<VSlot> &slots = vtables_[cls];
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (slots[i].name != d.name || slots[i].constThis != constThis) continue;
        if (slots[i].params.size() != params.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < params.size(); k++)
            if (slots[i].params[k] != params[k]) { same = false; break; }
        if (!same) continue;
        slots[i].symbol = symbol;
        return;
    }
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
            ? microsoftMemberName(cls, name, fn, code, constThis, &out, &why)
            : itaniumMemberName(cls, name, fn, constThis, &out, &why);
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
    const bool cName = cLinkage_ > 0 || name == "main";
    std::vector<std::size_t> &set = functionIndex_[name];

    for (std::size_t k = 0; k < set.size(); k++) {
        Signature &f = functions_[set[k]];
        if (f.params.size() != params.size() || f.variadic != variadic) continue;
        bool same = true;
        for (std::size_t i = 0; i < params.size(); i++)
            if (f.params[i] != params[i]) { same = false; break; }
        if (!same) continue;

        if (f.returns != returns)
            src_.fail(pos, "'" + name + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + name + "' is defined twice");
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
            src_.fail(pos, "'" + name + "' cannot be overloaded - " +
                           (name == "main" ? std::string("'main' is one function")
                                           : std::string("a name with C linkage "
                                             "carries one symbol")));
    }

    set.push_back(functions_.size());
    functions_.push_back(Signature{ name,
                                    functionSymbol(name, returns, params, variadic,
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
    if (const std::vector<std::size_t> *set = overloadsOf(name)) {
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

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    if (consume(")")) return;
    if (peek().is("void") && peekAt(1).is(")")) { at_ += 2; return; }

    for (;;) {
        if (consume("...")) { variadic = true; expect(")"); break; }
        StorageClass psc;
        Qualifiers pquals;
        const Type *pt = specifiers(&psc, &pquals);
        Declared pd = declarator(pt, true);
        if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
        if (pd.type->isVoid())
            src_.fail(pd.pos, "'void' is only a parameter list on its own");
        params.push_back(types_.withoutConst(pd.type));
        if (consume(")")) break;
        expect(",");
    }
}

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
        "constexpr", "const_cast", "decltype", "dynamic_cast",
        "explicit", "export", "friend", "inline", "mutable", "namespace",
        "noexcept", "not", "not_eq", "nullptr", "operator", "or",
        "or_eq", "reinterpret_cast",
        "static_assert", "static_cast", "template", "thread_local",
        "throw", "try", "typeid", "typename", "using", "virtual",
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
        ExprPtr e = expr();
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

    if (peek().kind == TokenKind::Ident) {
        std::string name = peek().text;
        std::size_t pos = peek().pos;

        const Local *l = findLocal(name);
        const GlobalSym *g = l != nullptr ? nullptr : findGlobal(name);
        const Type *held = l != nullptr ? l->type : (g != nullptr ? g->type : nullptr);
        bool callsThroughObject = held != nullptr && held->isFunctionPointer();

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

static bool foldDouble(const Expr &e, const Target &target, long double *out) {
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        *out = n->type()->isFloating()
                   ? inType(n->type(), target, n->dvalue())
                   : static_cast<long double>(n->value());
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (!foldDouble(c->value(), target, out)) return false;

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
        if (u->op() == '-' && foldDouble(u->operand(), target, out)) { *out = -*out; return true; }
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long double l, r;
        if (!foldDouble(b->lhs(), target, &l) ||
            !foldDouble(b->rhs(), target, &r)) return false;

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
        switch (b->op()) {
        case BinOp::Add: *out = l + r; return true;
        case BinOp::Sub: *out = l - r; return true;
        case BinOp::Mul: *out = l * r; return true;
        case BinOp::Div: if (r == 0) return false; *out = l / r; return true;
        default: return false;
        }
    }
    return false;
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
        if (!foldDouble(*value, target_, &d))
            src_.fail(in.pos, "expected a constant initialiser, and this is not "
                              "a constant");
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

static bool isLvalue(const Expr &e) {
    if (dynamic_cast<const Var *>(&e)) return true;
    if (dynamic_cast<const MemberAccess *>(&e)) return true;
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) return u->op() == '*';
    return false;
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
    if (!referent->isConst()) {
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

ExprPtr Parser::completeCall(const std::string &name, const std::string &symbol,
                             ExprPtr callee, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, std::size_t pos,
                             std::vector<ExprPtr> args) {
    if (variadic ? args.size() < params.size() : args.size() != params.size())
        src_.fail(pos, "'" + name + "' takes " + (variadic ? "at least " : "") +
                       std::to_string(params.size()) + " argument(s), given " +
                       std::to_string(args.size()));

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
    const Signature &sig = resolveOverload(key, args, pos);

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

ExprPtr Parser::mul() {
    ExprPtr n = castExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("*"))      n = arithmetic(BinOp::Mul, std::move(n), castExpr(), pos);
        else if (consume("/")) n = arithmetic(BinOp::Div, std::move(n), castExpr(), pos);
        else if (consume("%")) n = arithmetic(BinOp::Mod, std::move(n), castExpr(), pos);
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
        if (consume("<<"))      op = BinOp::Shl;
        else if (consume(">>")) op = BinOp::Shr;
        else return n;

        n = shiftOf(op, std::move(n), add());
    }
}

ExprPtr Parser::relational() {
    ExprPtr n = shift();
    for (;;) {
        if (consume("<"))       n = comparison(BinOp::Lt, std::move(n), shift());
        else if (consume("<=")) n = comparison(BinOp::Le, std::move(n), shift());
        else if (consume(">"))  n = comparison(BinOp::Gt, std::move(n), shift());
        else if (consume(">=")) n = comparison(BinOp::Ge, std::move(n), shift());
        else return n;
    }
}

ExprPtr Parser::equality() {
    ExprPtr n = relational();
    for (;;) {
        if (consume("=="))      n = comparison(BinOp::Eq, std::move(n), relational());
        else if (consume("!=")) n = comparison(BinOp::Ne, std::move(n), relational());
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
        node->setType(types_.intType());
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
        node->setType(types_.intType());
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

ExprPtr Parser::shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs) {
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

    if (ExprPtr readBack = clonePure(*target)) {
        ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
            ? shiftOf(op, std::move(readBack), std::move(value))
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
        ? shiftOf(op, std::move(through[0]), std::move(value))
        : arithmetic(op, std::move(through[0]), std::move(value), pos);
    ExprPtr store(new Assign(std::move(through[1]), convert(std::move(combined), to)));
    store->setType(to);

    ExprPtr node(new Comma(std::move(save), std::move(store)));
    node->setType(to);
    return node;
}

ExprPtr Parser::incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos) {
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
    if (!t->isScalar())
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

StmtPtr Parser::declaration() {
    std::size_t pos = peek().pos;
    StmtPtr s = declarationBody();
    if (s) s->setPos(pos);
    return s;
}

StmtPtr Parser::declarationBody() {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);

    if (peek().is(";")) { at_++; return StmtPtr(new Block({})); }

    if (sc == StorageTypedef) {
        do {
            Declared td = declarator(base);
            typedefFunctionSuffix(td);
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same
            // type, which is what makes the C idiom "typedef struct S S;"
            // legal now that the tag already names the type by itself. Only a
            // redeclaration to a *different* type is an error.
            if (const Type *had = findTypedef(td.name))
                if (had != td.type)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      had->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            typedefIndex_[td.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ td.name, td.type });
        } while (consume(","));
        expect(";");
        return StmtPtr(new Block({}));
    }

    if (sc == StorageExtern) {
        do {
            Declared d = declarator(base);
            if (peek().is("(")) { blockFunctionDeclaration(d); continue; }
            if (peek().is("="))
                src_.fail(d.pos, "'" + d.name + "' is extern, and an extern "
                                 "declaration cannot have an initialiser - the "
                                 "definition it names belongs at file scope");
            if (const GlobalSym *g = findGlobal(d.name))
                if (g->type != d.type)
                    src_.fail(d.pos, "'" + d.name + "' is declared '" +
                                     d.type->describe() + "' here and '" +
                                     g->type->describe() + "' at file scope");
            const GlobalSym *seen = findGlobal(d.name);
            declareStaticLocal(d.name, d.type, d.pos,
                               seen != nullptr ? seen->symbol
                                               : dataSymbol(d.name, d.type, false, d.pos));
        } while (consume(","));
        expect(";");
        return StmtPtr(new Block({}));
    }

    std::vector<StmtPtr> inits;
    do {
        Declared d = declarator(base);

        // An object of a class that declares constructors is built by calling
        // one, and that has to be asked before the branch below - `Point p(1)`
        // and a function declaration look the same until the type is known to
        // be a class with constructors.
        if (d.type->isStructOrUnion() && !d.type->tag().empty() &&
            overloadsOf(constructorKey(d.type->tag())) != nullptr) {
            if (sc == StorageStatic)
                src_.fail(d.pos, "'" + d.name + "' is static and has a "
                                 "constructor - running one before main is not "
                                 "supported yet");
            std::vector<ExprPtr> args;
            if (consume("(")) {
                if (peek().is(")"))
                    src_.fail(d.pos, "'" + d.name + "()' declares a function "
                                     "taking nothing and returning '" +
                                     d.type->describe() + "' - C++ reads it that "
                                     "way and not as a construction. Write '" +
                                     d.type->describe() + " " + d.name +
                                     ";' for the default constructor");
                parseArguments(args);
            } else if (peek().is("=")) {
                src_.fail(d.pos, "'" + d.name + " = ...' copy-initialises, which "
                                 "needs a copy constructor - not supported yet. "
                                 "Write '" + d.name + "(...)' instead");
            }

            int off = declare(d.name, d.type, d.pos);
            inits.push_back(constructLocal(d, off, std::move(args)));
            if (destructorOf(d.type) != nullptr)
                alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
            if (!consume(",")) break;
            continue;
        }

        if (peek().is("(")) {
            if (sc == StorageStatic)
                src_.fail(d.pos, "'" + d.name + "' is a function declared inside a "
                                 "block, and such a declaration is always extern - "
                                 "drop the 'static' or move it to file scope");
            blockFunctionDeclaration(d);
            continue;
        }
        if (d.type->isReference()) {
            if (sc == StorageStatic)
                src_.fail(d.pos, "'" + d.name + "' is a static reference, and "
                                 "that needs the binding to happen once before "
                                 "main - not supported yet");
            if (!peek().is("="))
                src_.fail(d.pos, "'" + d.name + "' is a reference and has to be "
                                 "initialised here - there is no later "
                                 "assignment that would bind it, only one that "
                                 "writes through it");
            at_++;
            ExprPtr init = assign();
            int off = declare(d.name, d.type, d.pos);
            const Type *slot = types_.pointerTo(d.type->referent());
            ExprPtr addr = bindReference(d.type, std::move(init), d.pos,
                                         "'" + d.name + "'");
            ExprPtr target(Var::local(d.name, off));
            target->setType(slot);
            ExprPtr bind(new Assign(std::move(target), std::move(addr)));
            bind->setType(slot);
            inits.push_back(StmtPtr(new ExprStmt(std::move(bind))));
            continue;
        }

        bool sizedByInitialiser = d.type->isArray() && d.type->length() < 0 &&
                                  peek().is("=");
        if (!d.type->isComplete() && !sizedByInitialiser)
            src_.fail(d.pos, "'" + d.name + "' has an incomplete type");

        if (sc == StorageStatic) {
            std::string symbol = functionName_ + "." + d.name;
            for (int n = 1; ; n++) {
                bool taken = false;
                for (const std::string &used : staticSymbols_)
                    if (used == symbol) { taken = true; break; }
                if (!taken) break;
                symbol = functionName_ + "." + d.name + "." + std::to_string(n);
            }
            staticSymbols_.push_back(symbol);
            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            if (consume("=")) {
                Init in = parseInitialiser();
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                flattenInit(d.type, in, 0, pieces);
                hasInit = true;
            } else if (d.type->isArray() && d.type->length() < 0) {
                src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                 "to take one from");
            }
            declareStaticLocal(d.name, d.type, d.pos, symbol);
            locals_.back().isConst = d.type->isConst();
            current_->globals.push_back(Global{ symbol, symbol, d.type,
                                                std::move(pieces), hasInit, true,
                                                locals_.back().isConst });
            continue;
        }

        bool hasInit = peek().is("=");
        Init in;
        if (hasInit) {
            at_++;
            in = parseInitialiser();
            if (d.type->isArray() && d.type->length() < 0)
                d.type = types_.arrayOf(d.type->pointee(),
                                        inferredLength(in, d.type->pointee(), d.pos));
        } else if (d.type->isArray() && d.type->length() < 0) {
            src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                             "to take one from");
        }

        declare(d.name, d.type, d.pos);
        locals_.back().isConst = d.type->isConst();
        locals_.back().isRegister = (sc == StorageRegister);

        if (hasInit) {
            std::vector<InitStep> path;
            emitInit(d.name, path, d.type, in, inits);
        }
    } while (consume(","));

    expect(";");
    return StmtPtr(new Block(std::move(inits)));
}

StmtPtr Parser::forStatement() {
    expect("for");
    expect("(");
    enterScope();
    int scope = enterBlock();

    StmtPtr init;
    if (!consume(";")) {
        if (atDeclarationStart()) init = declaration();
        else { ExprPtr e = expr(); expect(";"); init = StmtPtr(new ExprStmt(std::move(e))); }
    }

    ExprPtr cond;
    if (!peek().is(";")) cond = decay(expr());
    expect(";");

    ExprPtr step;
    if (!peek().is(")")) step = decay(expr());
    expect(")");

    loopDepth_++;
    StmtPtr body = statement();
    loopDepth_--;

    leaveBlock();
    leaveScope();
    For *f = new For(std::move(init), std::move(cond),
                     std::move(step), std::move(body));
    f->setScope(scope);
    return StmtPtr(f);
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    long long v;
    if (!fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

bool Parser::fold(const Expr &e, long long *out, std::size_t pos) const {
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

long long Parser::narrowTo(long long v, const Type *t) const {
    int bits = t->size(target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}

StmtPtr Parser::switchStatement() {
    std::size_t pos = peek().pos;
    expect("switch");
    expect("(");
    ExprPtr cond = decay(expr());
    if (!cond->type()->isInteger())
        src_.fail(pos, "a switch needs an integer, not '" +
                       cond->type()->describe() + "'");
    const Type *governing = promote(cond->type());
    cond = convert(std::move(cond), governing);
    expect(")");

    switches_.push_back(SwitchCtx{ {}, nullptr, governing });
    switchDepth_++;
    StmtPtr body = statement();
    switchDepth_--;

    SwitchCtx ctx = std::move(switches_.back());
    switches_.pop_back();
    return StmtPtr(new Switch(std::move(cond), std::move(body),
                              std::move(ctx.cases), ctx.deflt));
}

StmtPtr Parser::caseLabel() {
    std::size_t pos = peek().pos;
    bool isDefault = consume("default");
    if (!isDefault) expect("case");

    if (switches_.empty())
        src_.fail(pos, isDefault ? "'default' is not inside a switch"
                                 : "'case' is not inside a switch");

    long long value = 0;
    if (isDefault) {
        if (switches_.back().deflt)
            src_.fail(pos, "a switch has only one 'default'");
    } else {
        value = narrowTo(constantExpression("a case value"),
                         switches_.back().governing);
        for (const Case *c : switches_.back().cases)
            if (c->value() == value)
                src_.fail(pos, "duplicate case value " + std::to_string(value));
    }
    expect(":");

    if (atDeclarationStart())
        src_.fail(peek().pos, "a label cannot be followed by a declaration - "
                              "put it in a block");
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    StmtPtr body = statement();

    Case *node = new Case(value, isDefault, caseIds_++, std::move(body));
    StmtPtr owned(node);
    SwitchCtx &sw = switches_.back();
    if (isDefault) sw.deflt = node;
    else sw.cases.push_back(node);
    return owned;
}

StmtPtr Parser::gotoLabel() {
    std::size_t pos = peek().pos;
    std::string name = expectIdent("a label");
    expect(":");

    for (const LabelDef &l : labels_)
        if (l.name == name)
            src_.fail(pos, "label '" + name + "' is defined twice in this function");
    labels_.push_back(LabelDef{ name, pos });

    if (atDeclarationStart())
        src_.fail(peek().pos, "a label cannot be followed by a declaration - "
                              "put it in a block");
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    return StmtPtr(new Label(std::move(name), statement()));
}

void Parser::resolveGotos() {
    for (const LabelDef &g : gotos_) {
        bool found = false;
        for (const LabelDef &l : labels_)
            if (l.name == g.name) { found = true; break; }
        if (!found)
            src_.fail(g.pos, "no label '" + g.name + "' in this function");
    }
    labels_.clear();
    gotos_.clear();
}

StmtPtr Parser::block() {
    std::size_t pos = peek().pos;
    expect("{");
    enterScope();
    const std::size_t aliveAtEntry = alive_.size();
    bool isBody = atFunctionBody_;
    atFunctionBody_ = false;
    int scope = isBody ? 0 : enterBlock();
    std::vector<StmtPtr> body;
    while (!peek().is("}")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "unclosed '{'");
        body.push_back(atDeclarationStart() ? declaration() : statement());
    }
    // Everything this block constructed is destroyed here, last first. The
    // objects are found by where they are in `alive_` rather than by walking
    // the block again: what a scope built is exactly what it added.
    emitDestructors(body, aliveAtEntry, peek().pos);
    alive_.resize(aliveAtEntry);

    expect("}");
    if (!isBody) leaveBlock();
    leaveScope();
    Block *b = new Block(std::move(body));
    b->setScope(scope);

    b->setPos(pos);
    return StmtPtr(b);
}

StmtPtr Parser::statement() {
    std::size_t pos = peek().pos;
    StmtPtr s = statementBody();
    if (s) s->setPos(pos);
    return s;
}

StmtPtr Parser::statementBody() {
    if (consume("return")) {
        std::size_t pos = peek().pos;
        if (consume(";")) {
            if (!returnType_->isVoid())
                src_.fail(pos, "this function returns '" + returnType_->describe() +
                               "', so 'return' needs a value - a bare 'return' is "
                               "only for a function returning 'void'");
            if (!alive_.empty()) {
                std::vector<StmtPtr> unwind;
                emitDestructors(unwind, 0, pos);
                unwind.push_back(StmtPtr(new Return(nullptr)));
                return StmtPtr(new Block(std::move(unwind)));
            }
            return StmtPtr(new Return(nullptr));
        }
        ExprPtr value = returnType_->isReference() ? expr() : decay(expr());
        if (returnType_->isReference()) {
            value = bindReference(returnType_, std::move(value), pos,
                                  "this function's return type");
            if (dynamic_cast<const Comma *>(value.get()) != nullptr)
                src_.fail(pos, "this returns a reference to a temporary of "
                               "this function, which is gone by the time the "
                               "caller could read it");
        } else {
            checkAssignable(*value, returnType_, pos, "this function's return type");
            value = convert(std::move(value), returnType_);
        }
        expect(";");

        // **A return runs every destructor the function still owes, and the
        // value is computed first.** The order is not a detail: the expression
        // may read an object that is about to be destroyed, so it goes into a
        // slot of its own, then the destructors run, then the slot is
        // returned. Without the temporary this would return a value read out
        // of an object after its destructor had been told it was finished.
        if (!alive_.empty()) {
            std::vector<StmtPtr> unwind;
            int slot = allocateFrameSlot(returnType_);
            std::string temp = ".ret" + std::to_string(refTemps_++);

            ExprPtr keep(Var::local(temp, slot));
            keep->setType(returnType_);
            ExprPtr save(new Assign(std::move(keep), std::move(value)));
            save->setType(returnType_);
            unwind.push_back(StmtPtr(new ExprStmt(std::move(save))));

            emitDestructors(unwind, 0, pos);

            ExprPtr give(Var::local(temp, slot));
            give->setType(returnType_);
            unwind.push_back(StmtPtr(new Return(std::move(give))));
            return StmtPtr(new Block(std::move(unwind)));
        }
        return StmtPtr(new Return(std::move(value)));
    }
    if (consume("if")) {
        expect("(");
        ExprPtr cond = decay(expr());
        expect(")");
        StmtPtr thenArm = statement();
        StmtPtr elseArm;
        if (consume("else")) elseArm = statement();
        return StmtPtr(new If(std::move(cond), std::move(thenArm), std::move(elseArm)));
    }
    if (consume("while")) {
        expect("(");
        ExprPtr cond = decay(expr());
        expect(")");
        loopDepth_++;
        StmtPtr body = statement();
        loopDepth_--;
        return StmtPtr(new While(std::move(cond), std::move(body)));
    }

    if (peek().is("for")) return forStatement();

    if (consume("do")) {
        loopDepth_++;
        StmtPtr body = statement();
        loopDepth_--;
        expect("while");
        expect("(");
        ExprPtr cond = decay(expr());
        expect(")");
        expect(";");
        return StmtPtr(new DoWhile(std::move(body), std::move(cond)));
    }

    if (peek().is("switch")) return switchStatement();
    if (peek().is("case") || peek().is("default")) return caseLabel();

    if (consume("goto")) {
        std::size_t pos = peek().pos;
        std::string name = expectIdent("a label to jump to");
        expect(";");
        gotos_.push_back(LabelDef{ name, pos });
        return StmtPtr(new Goto(std::move(name)));
    }

    if (peek().kind == TokenKind::Ident && peekAt(1).is(":")) return gotoLabel();

    if (peek().is("break") || peek().is("continue") || peek().is("goto")) {
        // A jump can leave a scope without falling off its end, and this
        // compiler runs destructors at the end. Rather than skip them
        // silently - which loses a release, a close, a free - the jump is
        // refused while anything is alive. Conservative: it refuses some
        // programs whose jump would not have crossed the object at all. The
        // precise rule needs each jump to know which scopes it leaves, and
        // that is a change to how jumps are built rather than an addition.
        if (!alive_.empty())
            src_.fail(peek().pos, "'" + peek().text + "' would leave a scope "
                                  "holding '" + alive_.back().name + "', whose "
                                  "destructor runs at the end of that scope - "
                                  "jumping over a destructor is not supported "
                                  "yet");
    }

    if (consume("break")) {
        if (loopDepth_ == 0 && switchDepth_ == 0)
            src_.fail(peek().pos, "'break' is not inside a loop or a switch");
        expect(";");
        return StmtPtr(new Break());
    }

    if (consume("continue")) {
        if (loopDepth_ == 0)
            src_.fail(peek().pos, "'continue' is not inside a loop");
        expect(";");
        return StmtPtr(new Continue());
    }
    if (peek().is("{")) return block();
    if (consume(";")) return StmtPtr(new Block({}));

    ExprPtr e = expr();
    expect(";");
    return StmtPtr(new ExprStmt(std::move(e)));
}

// extern "C" - [dcl.link]. Two forms: one declaration, or a brace-enclosed
// list of them. The list is not a scope: what it holds is declared where the
// specification is, and only the linkage of the names changes.
bool Parser::linkageSpecification() {
    if (!peek().is("extern") || peekAt(1).kind != TokenKind::Str) return false;
    at_++;
    std::size_t pos = peek().pos;
    std::string language = peek().text;
    at_++;

    if (language != "C" && language != "C++")
        src_.fail(pos, "'" + language + "' is not a linkage this compiler "
                       "knows - the standard fixes only \"C\" and \"C++\", "
                       "and every other spelling is the implementation's own");

    bool c = language == "C";
    if (c) cLinkage_++;

    if (consume("{")) {
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this 'extern \"" + language + "\"' block is "
                               "never closed");
            topLevel(*current_);
        }
        at_++;
    } else {
        topLevel(*current_);
    }

    if (c) cLinkage_--;
    return true;
}

void Parser::topLevel(Program &program) {
    if (linkageSpecification()) return;

    StorageClass sc;
    Qualifiers quals;
    std::size_t scPos = peek().pos;
    const Type *base = specifiers(&sc, &quals);

    if (peek().is(";")) { at_++; return; }

    if (sc == StorageRegister)
        src_.fail(scPos, "'register' is a storage class for a local or a "
                         "parameter, and this is file scope");
    if (sc == StorageAuto)
        src_.fail(scPos, "'auto' is a storage class for a local, and this is "
                         "file scope - every object here has static duration");

    if (sc == StorageTypedef) {
        do {
            Declared td = declarator(base);
            typedefFunctionSuffix(td);
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same
            // type, which is what makes the C idiom "typedef struct S S;"
            // legal now that the tag already names the type by itself. Only a
            // redeclaration to a *different* type is an error.
            if (const Type *had = findTypedef(td.name))
                if (had != td.type)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      had->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            typedefIndex_[td.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ td.name, td.type });
        } while (consume(","));
        expect(";");
        return;
    }

    locals_.clear();
    fnVars_.clear();
    scopeStarts_.clear();
    blocks_.clear();
    blockStack_.clear();
    blocks_.push_back(0);
    blockStack_.push_back(0);
    enterScope();
    frameSize_ = 0;
    Declared d = declarator(base);

    if (d.type->isFunction() && d.paramsAt == 0 && !peek().is("(")) {
        std::vector<const Type *> ps(d.type->params());
        declareFunction(d.name, d.type->returns(), ps,
                        d.type->isVariadicFn(), false, d.pos,
                        sc == StorageStatic);
        if (peek().is("{"))
            src_.fail(d.pos, "'" + d.name + "' cannot be *defined* through a "
                             "typedef - the body has no names for the "
                             "parameters; write the parameter list out");
        expect(";");
        return;
    }

    if (!peek().is("(") && d.paramsAt == 0) {
        for (;;) {
            if (d.type->isVoid()) src_.fail(d.pos, "'" + d.name + "' cannot have type void");
            // A reference at file scope has to be bound before main runs,
            // which is a whole mechanism - the same one static objects with
            // constructors will need - and it is not here yet.
            if (d.type->isReference())
                src_.fail(d.pos, "'" + d.name + "' is a reference at file "
                                 "scope, and binding one before main is not "
                                 "supported yet - make it a local or a "
                                 "pointer");

            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            if (consume("=")) {
                Init in = parseInitialiser();
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                flattenInit(d.type, in, 0, pieces);
                hasInit = true;
            } else if (d.type->isArray() && d.type->length() < 0 &&
                       sc != StorageExtern) {
                src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                 "to take one from");
            }

            if (GlobalSym *prev = findGlobalToUpdate(d.name)) {
                const Type *both = composite(prev->type, d.type);
                if (both == nullptr)
                    src_.fail(d.pos, "'" + d.name + "' was already declared as '" +
                                     prev->type->describe() + "', not '" +
                                     d.type->describe() + "'");

                prev->type = both;
                d.type = both;
                if (hasInit && prev->hasInit)
                    src_.fail(d.pos, "'" + d.name + "' is given an initialiser twice");
                if (hasInit) prev->hasInit = true;

                if (sc != StorageExtern) {
                    if (!prev->emitted) {
                        prev->emitted = true;
                        program.globals.push_back(Global{ d.name, prev->symbol,
                                                          d.type, pieces, hasInit,
                                                          sc == StorageStatic,
                                                          prev->isConst });
                    } else {
                        for (Global &g : program.globals)
                            if (g.name == d.name) {
                                g.type = both;
                                if (hasInit) { g.init = pieces; g.hasInit = true; }
                                break;
                            }
                    }
                }
                if (!consume(",")) break;
                d = declarator(base);
                continue;
            }

            globalIndex_[d.name] = globals_.size();
            bool objectIsConst = d.type->isConst();
            // A const object at namespace scope has internal linkage of its
            // own - [basic.link]/3 - which is why a header may define one and
            // C, where it would be external, may not. Nothing outside can
            // name it, so it keeps the name it was written with.
            bool internal = sc == StorageStatic ||
                            (objectIsConst && sc != StorageExtern);
            std::string symbol = dataSymbol(d.name, d.type, internal, d.pos);
            globals_.push_back(GlobalSym{ d.name, symbol, d.type, objectIsConst,
                                          sc != StorageExtern, hasInit });
            if (sc != StorageExtern)
                program.globals.push_back(Global{ d.name, symbol, d.type,
                                                  std::move(pieces), hasInit,
                                                  internal, objectIsConst });
            if (!consume(",")) break;
            d = declarator(base);
        }
        expect(";");
        return;
    }

    std::size_t resumeAt = 0;
    if (d.paramsAt != 0) {
        resumeAt = at_;
        at_ = d.paramsAt;
    }

    expect("(");
    std::vector<const Type *> params;
    std::vector<Param> paramSlots;
    bool variadic = false;
    std::size_t unnamedParam = 0;
    bool sawUnnamed = false;

    // **`this` is parameter zero, and it is declared before any written one so
    // that it takes the first slot.** That is the whole of how a member
    // function differs from a free one at the machine: an extra leading
    // pointer, which every backend already knows how to pass. It is not in
    // `params`, because `params` is the declared signature - what overload
    // resolution ranks and what the mangler spells - and `this` is in neither.
    const Type *memberOf = nullptr;
    if (!d.qualifier.empty()) {
        memberOf = findTypedef(d.qualifier);
        if (memberOf == nullptr || !memberOf->isStructOrUnion())
            src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
        currentClass_ = memberOf;
    }

    if (!consume(")")) {
        if (peek().is("void") && peekAt(1).is(")")) {
            at_ += 2;
        } else {
            for (;;) {
                if (consume("...")) { variadic = true; expect(")"); break; }
                std::size_t pscPos = peek().pos;
                StorageClass psc;
                Qualifiers pquals;
                const Type *pt = specifiers(&psc, &pquals);
                if (psc != StorageNone && psc != StorageRegister)
                    src_.fail(pscPos, "'register' is the only storage class a "
                                      "parameter may have");
                Declared pd = declarator(pt, true);
                if (pd.type->isArray())
                    pd.type = types_.pointerTo(pd.type->pointee());
                int off;
                if (pd.name.empty()) {
                    if (pd.type->isVoid())
                        src_.fail(pd.pos, "'void' is only a parameter list on its own");
                    unnamedParam = pd.pos;
                    sawUnnamed = true;
                    off = 0;
                } else {
                    inParams_ = true;
                    off = declare(pd.name, pd.type, pd.pos);
                    inParams_ = false;
                    locals_.back().isConst = pd.type->isConst();
                    locals_.back().isRegister = (psc == StorageRegister);
                }
                params.push_back(types_.withoutConst(pd.type));
                paramSlots.push_back(Param{ pd.type->isReference()
                                            ? types_.pointerTo(pd.type->referent())
                                            : pd.type, off });
                if (consume(")")) break;
                expect(",");
            }
        }
    }
    if (resumeAt != 0) at_ = resumeAt;

    if (peek().is("(") || peek().is("[")) {
        bool fn = peek().is("(");
        src_.fail(peek().pos,
                  std::string("a function cannot return ") +
                  (fn ? "a function" : "an array") +
                  " - it may return a pointer to one, written '" +
                  (fn ? "int (*f(void))(void)" : "int (*f(void))[3]") + "'");
    }

    // A member function's constness is written after the parameter list, and
    // it is part of which member this is - Point::get() const and
    // Point::get() are two functions.
    bool constThis = false;
    if (memberOf != nullptr && consume("const")) constThis = true;

    if (consume(";")) {
        if (memberOf != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is declared "
                             "inside the class - this says it again outside, "
                             "which declares nothing new");
        declareFunction(d.name, d.type, params, variadic, false, d.pos,
                        sc == StorageStatic);
        return;
    }
    if (sawUnnamed)
        src_.fail(unnamedParam, "a parameter of a definition needs a name - "
                                "a prototype may leave it out, a body cannot");

    const Signature *member = nullptr;
    if (memberOf != nullptr) {
        std::string key = d.qualifier + "::" + d.name;   // "Point::~Point" too
        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            for (std::size_t k = 0; k < set->size() && member == nullptr; k++) {
                const Signature &f = functions_[(*set)[k]];
                if (f.params.size() != params.size() ||
                    f.constThis != constThis) continue;
                bool same = true;
                for (std::size_t i = 0; i < params.size(); i++)
                    if (f.params[i] != params[i]) { same = false; break; }
                if (same) member = &f;
            }
        }
        if (member == nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "' declares no member '" +
                             d.name + "' with these parameters");
        if (member->returns != d.type)
            src_.fail(d.pos, "'" + key + "' was declared to return '" +
                             member->returns->describe() + "' and this says '" +
                             d.type->describe() + "'");
        if (member->defined)
            src_.fail(d.pos, "'" + key + "' is defined twice");
        functions_[(*overloadsOf(key))[0]].pos = member->pos;   // keep the table stable
        const_cast<Signature *>(member)->defined = true;

        // `this` takes the first slot, and its type carries the constness the
        // member was declared with - so a const member function cannot write
        // through it, by the ordinary rule that a const object's members are
        // const.
        const Type *pointee = constThis ? types_.withConst(memberOf) : memberOf;
        const Type *thisType = types_.pointerTo(pointee);
        inParams_ = true;
        thisOffset_ = declare("this", thisType, d.pos);
        inParams_ = false;
        paramSlots.insert(paramSlots.begin(), Param{ thisType, thisOffset_ });
    } else {
        declareFunction(d.name, d.type, params, variadic, true, d.pos,
                        sc == StorageStatic);
    }
    returnType_ = d.type;
    functionName_ = d.name;
    staticSymbols_.clear();

    int sretSlot = 0;
    if (d.type->isStructOrUnion() && returnsIndirectly(d.type)) {
        frameSize_ += 8;
        frameSize_ = alignTo(frameSize_, 8);
        sretSlot = frameSize_;
    }

    int regSaveSlot = 0;
    if (variadic) {
        frameSize_ = alignTo(frameSize_, 16);
        frameSize_ += 176;
        regSaveSlot = frameSize_;
    }
    variadicBody_ = variadic;

    atFunctionBody_ = true;
    StmtPtr body = block();
    resolveGotos();
    variadicBody_ = false;

    // **A polymorphic object's vptr is set by its constructor**, before the
    // body and after the base's constructor - which is what makes the object
    // this class's during its own body even though the base already set the
    // pointer to its own table.
    //
    // What is stored is the table's address plus the header: Itanium writes
    // offset-to-top and typeinfo first, so the vptr points at table + 16
    // (measured from clang's own `addq $16`), and Microsoft has no header so
    // the address is the table's own.
    if (memberOf != nullptr && d.name == d.qualifier && memberOf->polymorphic()) {
        const bool ms = target_.microsoftNames();
        const std::string table = ms ? "??_7" + d.qualifier + "@@6B@"
                                     : "_ZTV" + std::to_string(d.qualifier.size()) +
                                       d.qualifier;
        const Type *entry = types_.pointerTo(types_.get(Kind::Void));
        const Type *entries = types_.pointerTo(entry);

        // **The table's ADDRESS, not its contents.** A global Var is an
        // lvalue and reading one loads from it - which stored the table's
        // first word in the vptr and crashed on the first call. Giving it the
        // array type and decaying it is what yields the address, the same road
        // any array name takes.
        std::size_t entryCount = vtables_[d.qualifier].size() + (ms ? 0 : 2);
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

        ExprPtr self(Var::local("this", thisOffset_));
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
                secondaryVptr_.find(d.qualifier + "::" + bs[bi].type->tag());
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

            ExprPtr self2(Var::local("this", thisOffset_));
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

        withVptr.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withVptr)));
    }

    // **A constructor runs the base's first and a destructor runs it last**,
    // which is the order the standard fixes and the order clang emits: the
    // base subobject has to exist before the derived body can touch it, and it
    // has to outlive the derived body for the same reason.
    //
    // The base's C2 and D2 are what is called - the base-object forms - and
    // this is what those two names have been emitted for since constructors
    // landed. On Windows there is one name for each and it is called directly.
    for (std::size_t bn = 0;
         memberOf != nullptr && bn < memberOf->bases().size() &&
         (d.name == d.qualifier || d.name == "~" + d.qualifier); bn++) {
        const bool building = d.name == d.qualifier;
        // Bases are built in the order they were written and destroyed in the
        // reverse - measured: A up, B up, C up, then C down, B down, A down.
        //
        // **Both walk the list backwards**, because a constructor's call is
        // prepended to the body and a destructor's is appended. Prepending A
        // last is what leaves it first; appending A last is what leaves it
        // last. Walking forwards for the constructor put B before A.
        const std::size_t which = memberOf->bases().size() - 1 - bn;
        const Type *base = memberOf->bases()[which].type;
        const int baseAt = memberOf->bases()[which].offset;
        const std::string key = building ? constructorKey(base->tag())
                                         : destructorKey(base->tag());

        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            const Signature *chosen = nullptr;
            for (std::size_t k = 0; k < set->size(); k++)
                if (functions_[(*set)[k]].params.empty()) chosen = &functions_[(*set)[k]];
            if (chosen == nullptr)
                src_.fail(d.pos, "'" + base->tag() + "' has no constructor "
                                 "taking nothing, and naming one for a base "
                                 "needs an initialiser list - not supported yet");

            std::string symbol = chosen->symbol;
            if (!target_.microsoftNames()) {
                std::string sub;
                if (building) {
                    const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                             std::vector<const Type *>(),
                                                             false);
                    std::string why;
                    itaniumConstructorName(base->tag(), fnType, false, &sub, &why);
                } else {
                    itaniumDestructorName(base->tag(), false, &sub);
                }
                symbol = sub;
            }

            const Type *basePtr = types_.pointerTo(base);
            ExprPtr me(Var::local("this", thisOffset_));
            if (baseAt == 0) {
                me->setType(basePtr);      // the first base is the object
            } else {
                me->setType(types_.pointerTo(memberOf));
                me = convert(std::move(me), basePtr);
            }
            std::vector<ExprPtr> args;
            args.push_back(std::move(me));
            std::vector<const Type *> params;
            params.push_back(basePtr);
            ExprPtr call = completeCall(base->tag(), symbol, nullptr,
                                        types_.get(Kind::Void), params, false,
                                        d.pos, std::move(args));

            std::vector<StmtPtr> wrapped;
            if (building) {
                wrapped.push_back(StmtPtr(new ExprStmt(std::move(call))));
                wrapped.push_back(std::move(body));
            } else {
                wrapped.push_back(std::move(body));
                wrapped.push_back(StmtPtr(new ExprStmt(std::move(call))));
            }
            body = StmtPtr(new Block(std::move(wrapped)));
        }
    }

    int frame = alignTo(frameSize_, 16);
    const Type *emittedReturn = d.type->isReference()
                              ? types_.pointerTo(d.type->referent()) : d.type;
    const Signature &defined = memberOf != nullptr
                             ? *member
                             : lookupSignature(d.name, params, variadic, d.pos);
    currentClass_ = nullptr;
    program.functions.push_back(Function(d.name, emittedReturn, std::move(paramSlots),
                                         std::move(body), frame,
                                         sc == StorageStatic, sretSlot,
                                         variadic, regSaveSlot, d.pos,
                                         std::move(fnVars_)));
    program.functions.back().setSymbol(defined.symbol);
    // A constructor is emitted under both of Itanium's names: C1 for a
    // complete object, C2 for a base subobject, the second as a label in front
    // of the first. The Microsoft ABI has one name and wants no alias.
    if (memberOf != nullptr && d.name == d.qualifier && !target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void), params, false);
        std::string c2, why;
        if (itaniumConstructorName(d.qualifier, fnType, false, &c2, &why))
            program.functions.back().setAlias(c2);
    }
    if (memberOf != nullptr && d.name == "~" + d.qualifier && !target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(d.qualifier, false, &d2);
        program.functions.back().setAlias(d2);
    }
    // The deleting form is emitted beside the destructor that was just
    // defined, because that is where its body comes from.
    if (memberOf != nullptr && d.name == "~" + d.qualifier && member != nullptr &&
        member->isVirtual)
        synthesizeDeleting(d.qualifier, memberOf, member->access, d.pos);
    program.functions.back().setBlocks(std::move(blocks_));
}

Program Parser::parse() {
    Program program;
    current_ = &program;
    while (peek().kind != TokenKind::End)
        topLevel(program);
    if (program.functions.empty())
        src_.fail(0, "the file defines no functions");
    return program;
}
