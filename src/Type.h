#pragma once

#include <string>
#include <vector>

enum class Kind {
    Void,
    // bool sits inside the integer range and at the bottom of it, which is
    // what lets isInteger() stay a range check. Its position in this enum is
    // load-bearing: TypeTable builds one Type per value from Void to Function.
    Bool,
    Char, SChar, UChar,
    Short, UShort,
    Int, UInt,
    Long, ULong,
    LongLong, ULongLong,
    Float, Double, LongDouble,
    Struct, Union,
    Pointer, Array, Function,
    // After Function, and it matters: TypeTable builds one Type per value
    // from Void to Function, and a reference is never one of those - it is
    // always a reference *to* something and so always derived.
    LValueRef,
    // **The expansion of a parameter pack**, `Ts...`, which appears in a
    // pattern where the pack's own members appear in the substituted
    // signature. Itanium writes `Dp` and then the parameter - `DpT0_` - and
    // says the same thing however many members the pack has, which is what
    // makes one pattern serve every specialization.
    PackExpansion,
    // **A template parameter, and it is not a type anything is made of.** It
    // exists so that a template's signature can be written down as the
    // *pattern* it was declared as - `T twice(T)` rather than
    // `int twice(int)` - which is what the Itanium mangler has to read: a
    // specialization's name spells `T_` where the substituted signature has
    // lost all record of where the int came from. Nothing but the manglers
    // ever sees one; a parameter has no size, no alignment and no value.
    TemplateParam,
    // **A member type of something that is not known yet** - `T::type`, or
    // `Value<T>::type`. Like TemplateParam it exists only in a pattern, and
    // for the same reason: Itanium spells a function template's return type
    // from the pattern, and `_Z4takeIiEN5ValueIT_E4typeES1_` says
    // `Value<T>::type` where the substituted signature says int and has
    // forgotten where it came from.
    DependentMember
};

class Target;

class Type;

// One template argument: a type, or a value of a type. `type` is the argument
// itself for a type argument and the *parameter's* type for a non-type one,
// because that is what both ABIs write beside the value - Itanium `Li3E`,
// Microsoft `$02`.
//
// It lives here rather than beside the manglers because a class that is a
// specialization carries its arguments: the tag is "Box<int,3>", which is
// what every table in the parser is keyed by, and neither ABI spells a name
// that way.
struct TemplateArg {
    const Type *type = nullptr;
    bool isType = true;
    long long value = 0;
    // **A pack argument is a list, not a type.** Itanium spells one `J...E`
    // and Microsoft lists its members inline with `$$V` for an empty one, so
    // both need the members rather than anything standing for them.
    bool isPack = false;
    std::vector<const Type *> pack;
};

int homogeneousFloatCount(const Type *t, Kind *elem);

bool containsX87(const Type *t, const Target &target);

void x87Parts(long double v, unsigned long long *significand, unsigned int *signExp);

int objectAlign(const Type *t, const Target &target);

// Who may name a member. `protected` is not `private` even with no derived
// class to tell them apart yet - recording which one was written is what lets
// inheritance mean something later without revisiting every declaration.
enum class Access { Public, Protected, Private };

struct Member {
    std::string name;
    const Type *type;
    int offset;
    int width = 0;
    int bitOffset = 0;
    Access access = Access::Public;

    bool isBitField() const { return width != 0; }
};

class Type {
public:
    explicit Type(Kind k) : kind_(k) {}
    Type(Kind k, const Type *pointee, long long length)
        : kind_(k), pointee_(pointee), length_(length) {}

    Kind kind() const { return kind_; }

    // Constness belongs to the type, not to the object that has it: a
    // 'const char *' and a 'char *' must be two different types, because
    // overload resolution ranks between them and the mangler spells them
    // differently. An array is const when its elements are - there is no
    // separate qualifier to hang on the array itself.
    bool isConst() const {
        return const_ || (kind_ == Kind::Array && pointee_->isConst());
    }
    const Type *unqualified() const { return unqual_ != nullptr ? unqual_ : this; }

    const Type *pointee() const { return pointee_; }
    long long length() const { return length_; }

    bool isPointer() const { return kind_ == Kind::Pointer; }

    // A reference is a pointer that has lost the right to be written or read
    // as one: it holds an address and occupies a pointer's worth of storage,
    // and every use of it goes through that address without saying so. The
    // parser lowers it away, so no backend ever sees this kind.
    bool isReference() const { return kind_ == Kind::LValueRef; }
    const Type *referent() const { return pointee_; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isScalar() const { return isArithmetic() || isPointer(); }

    bool isInteger() const {
        return kind_ >= Kind::Bool && kind_ <= Kind::ULongLong;
    }
    bool isFloating() const {
        return kind_ >= Kind::Float && kind_ <= Kind::LongDouble;
    }

    bool isX87(const Target &t) const;
    bool isArithmetic() const { return isInteger() || isFloating(); }
    bool isVoid() const { return kind_ == Kind::Void; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isStructOrUnion() const { return kind_ == Kind::Struct || kind_ == Kind::Union; }
    bool isComplete() const {
        if (unqual_ != nullptr) return unqual_->isComplete();
        if (isVoid()) return false;
        if (isArray() && length_ < 0) return false;
        if (isStructOrUnion()) return complete_;
        return true;
    }

    int size(const Target &t) const;
    int align(const Target &t) const;
    bool isSigned(const Target &t) const;

    int rank() const;

    const char *name() const;

    std::string describe() const;

    const std::string &tag() const { return unqual_ ? unqual_->tag() : tag_; }

    // `class X` and `struct X` build the same kind of type and differ only in
    // the default access - and in what a diagnostic should call it. A message
    // that says "struct Account" about something the program wrote as a class
    // sends the reader looking for a declaration that is not there.
    bool declaredClass() const { return unqual_ ? unqual_->declaredClass() : isClass_; }
    void setDeclaredClass(bool c) { isClass_ = c; }

    // **A class written inside another one.** `tag()` is the qualified name -
    // "Outer::Inner" - because every table in the parser is keyed by it and a
    // nested class must not collide with a global of the same name.
    // `localName()` is the single component, which is what both ABIs spell,
    // and `enclosing()` is the class it was written in, which is what the
    // Itanium substitution table has to be able to recognise: a parameter of
    // type Outer::Inner inside a member of Outer is NS_5InnerE, and the S_ is
    // Outer being found in that table rather than spelled again.
    const std::string &localName() const {
        if (unqual_) return unqual_->localName();
        return local_.empty() ? tag_ : local_;
    }
    void setLocalName(std::string n) { local_ = std::move(n); }

    // A class made by instantiating a class template. The name and arguments
    // are kept because the tag - "Box<int,3>" - is the parser's key and not
    // anything a linker has ever seen.
    bool isSpecialization() const { return !templateName_.empty(); }
    const std::string &templateName() const { return templateName_; }
    const std::vector<TemplateArg> &templateArgs() const { return templateArgs_; }
    void setSpecialization(std::string name, std::vector<TemplateArg> args) {
        templateName_ = std::move(name);
        templateArgs_ = std::move(args);
    }
    const Type *enclosing() const {
        return unqual_ ? unqual_->enclosing() : enclosing_;
    }
    void setEnclosing(const Type *e) { enclosing_ = e; }
    // Who may name this class, when it is written inside another one. A
    // nested class is a member like any other and `private:` reaches it.
    Access nestedAccess() const {
        return unqual_ ? unqual_->nestedAccess() : nestedAccess_;
    }
    void setNestedAccess(Access a) { nestedAccess_ = a; }

    // The one base class, or null. A base subobject sits at offset 0 - measured
    // - so a derived object's address IS its base's address and no adjustment
    // is needed anywhere. Multiple inheritance is what ends that, and it is a
    // Whether this class has any virtual function, its bases' included. It
    // decides the layout - a polymorphic object carries a vptr at offset 0 and
    // its members start after it - so it has to be answerable before the
    // members are placed.
    bool polymorphic() const { return unqual_ ? unqual_->polymorphic() : polymorphic_; }
    void setPolymorphic(bool p) { polymorphic_ = p; }

    // **Whether copying this class is a function call rather than a move of
    // bytes**, which both platform ABIs make a question about how it is
    // *passed*: a class with a non-trivial copy constructor goes by address,
    // whatever its size, where a trivially copyable one of the same size goes
    // in a register. Measured with cl and with clang for both Itanium
    // targets. The backends need to agree with the parser about this, which
    // is why it lives on the type rather than in the function table where
    // copy constructors are actually kept.
    bool nonTrivialCopy() const {
        return unqual_ ? unqual_->nonTrivialCopy() : nonTrivialCopy_;
    }
    void setNonTrivialCopy(bool n) { nonTrivialCopy_ = n; }

    // **Whether this class has a destructor to run.** It decides how the class
    // is *passed*, and the two ABIs disagree about how: Itanium passes such a
    // class by address whatever its size and has the caller destroy the copy,
    // where Microsoft passes it by the ordinary size rules and has the callee
    // destroy its own. Both return it through a hidden pointer. All measured.
    bool hasDestructor() const {
        return unqual_ ? unqual_->hasDestructor() : hasDestructor_;
    }
    void setHasDestructor(bool h) { hasDestructor_ = h; }

    // **Every base, with the offset it sits at.** The first is at 0 and any
    // second is not - measured: `class C : public A, public B` puts A at 0 and
    // B at 4 - which is the whole difference multiple inheritance makes. A
    // pointer to the second base is the object's address plus that offset, and
    // so is the `this` its member functions expect.
    struct BaseSpec {
        const Type *type;
        int offset;
        Access access;
    };
    const std::vector<BaseSpec> &bases() const {
        return unqual_ ? unqual_->bases() : bases_;
    }
    void addBase(const Type *b, int offset, Access how) {
        bases_.push_back(BaseSpec{ b, offset, how });
    }

    // The first base - the only one for most classes, and the only one that
    // needs no adjustment. Kept because most callers ask exactly that.
    const Type *base() const {
        const std::vector<BaseSpec> &b = bases();
        return b.empty() ? nullptr : b[0].type;
    }
    Access baseAccess() const {
        const std::vector<BaseSpec> &b = bases();
        return b.empty() ? Access::Public : b[0].access;
    }
    const std::vector<Member> &members() const {
        return unqual_ ? unqual_->members() : members_;
    }
    const Member *findMember(const std::string &name) const;

    // **A static data member is one object shared by the class, not one per
    // object.** It has no offset and takes no room, so it is kept apart from
    // the members rather than among them - neither the size computation nor
    // anything walking members() has to learn to skip it.
    struct StaticMember {
        std::string name;
        const Type *type;
        Access access;
        std::string symbol;
        // A `static const` of integer type whose initialiser is written
        // inside the class is a constant and needs no definition at all - cl
        // emits no symbol for one and folds the value in. That value is here.
        bool folded = false;
        long long value = 0;
    };
    const std::vector<StaticMember> &staticMembers() const {
        return unqual_ ? unqual_->staticMembers() : statics_;
    }
    void addStaticMember(StaticMember s) { statics_.push_back(std::move(s)); }
    // Searched up through the bases, the way a member function is: a static
    // member lives under a name, and a derived class names its base's.
    const StaticMember *findStaticMember(const std::string &name) const;

    void complete(std::vector<Member> members, int size, int align);

    // **Where this class's data actually ends, before the padding.** A base
    // subobject occupies this rather than sizeof, so a derived class may put
    // its first member in the base's tail padding - which is what the Itanium
    // ABI says and what clang does: a base of {vptr, int} ends at 12 and pads
    // to 16, and a derived int lands at 12, making the whole thing 16 rather
    // than 24. Equal to size() for anything with no tail padding, which is why
    // this stayed invisible until a class had some.
    int dataSize() const { return unqual_ ? unqual_->dataSize() : dataSize_; }
    void setDataSize(int d) { dataSize_ = d; }

    const Type *returns() const { return pointee_; }
    const std::vector<const Type *> &params() const { return params_; }
    bool isVariadicFn() const { return variadic_; }
    bool isFunction() const { return kind_ == Kind::Function; }
    bool isFunctionPointer() const {
        return kind_ == Kind::Pointer && pointee_ != nullptr && pointee_->isFunction();
    }
    std::string parameterList() const;

private:
    friend class TypeTable;
    Kind kind_;

    // Set only on a qualified type, and it points at the unqualified one it
    // was made from. Everything that depends on state a struct gains later -
    // its members, its size - is asked of that one rather than of this copy,
    // which was taken before the struct was complete.
    const Type *unqual_ = nullptr;
    bool const_ = false;

    const Type *pointee_ = nullptr;
    long long length_ = -1;

    std::vector<const Type *> params_;
    bool variadic_ = false;

    std::string tag_;
    std::string local_;
    // Set on a class that is a template specialization. `local_` is
    // "Box<int,3>" there, which no ABI writes: Itanium wants `3BoxIiLi3EE`
    // and Microsoft `?$Box@H$02@`, both built from these two.
    std::string templateName_;
    std::vector<TemplateArg> templateArgs_;
    const Type *enclosing_ = nullptr;
    Access nestedAccess_ = Access::Public;
    bool isClass_ = false;
    int dataSize_ = 0;
    bool polymorphic_ = false;
    bool nonTrivialCopy_ = false;
    bool hasDestructor_ = false;
    std::vector<BaseSpec> bases_;
    std::vector<Member> members_;
    std::vector<StaticMember> statics_;
    int size_ = 0;
    int align_ = 1;
    bool complete_ = false;
};

class TypeTable {
public:
    TypeTable();
    const Type *get(Kind k) const;

    const Type *withConst(const Type *t);
    const Type *withoutConst(const Type *t);
    const Type *pointerTo(const Type *t);
    const Type *referenceTo(const Type *t);
    const Type *arrayOf(const Type *t, long long length);
    const Type *functionType(const Type *returns,
                             std::vector<const Type *> params, bool variadic);
    // The Nth parameter of the template being mangled. Interned like any
    // other derived type, so two mentions of T in one signature are one
    // pointer and the substitution table sees them as the repeat they are.
    const Type *templateParam(int index);
    // `owner::name`, where owner is a pattern. Interned on the pair.
    const Type *dependentMember(const Type *owner, const std::string &name);
    const Type *packExpansion(const Type *of);

    Type *structType(Kind kind, const std::string &tag);
    Type *anonymousStruct(Kind kind);

    const Type *voidType() const   { return get(Kind::Void); }
    const Type *intType() const    { return get(Kind::Int); }
    const Type *doubleType() const { return get(Kind::Double); }
    const Type *charType() const   { return get(Kind::Char); }

private:
    std::vector<Type> types_;
    std::vector<Type *> derived_;
};

class Target {
public:
    virtual ~Target() = default;

    virtual int sizeOf(Kind) const = 0;
    virtual int alignOf(Kind) const = 0;
    virtual bool plainCharIsSigned() const = 0;

    virtual Kind sizeType() const = 0;

    virtual Kind wcharType() const = 0;

    // Which ABI spells a C++ name, which is a property of the platform in
    // the same way that the width of a long is. Itanium everywhere but
    // Windows.
    virtual bool microsoftNames() const = 0;

    virtual const char *name() const = 0;
};
