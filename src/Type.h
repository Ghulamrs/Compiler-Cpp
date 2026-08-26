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
    LValueRef
};

class Target;

class Type;

int homogeneousFloatCount(const Type *t, Kind *elem);

bool containsX87(const Type *t, const Target &target);

void x87Parts(long double v, unsigned long long *significand, unsigned int *signExp);

int objectAlign(const Type *t, const Target &target);

struct Member {
    std::string name;
    const Type *type;
    int offset;
    int width = 0;
    int bitOffset = 0;

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
    const std::vector<Member> &members() const {
        return unqual_ ? unqual_->members() : members_;
    }
    const Member *findMember(const std::string &name) const;

    void complete(std::vector<Member> members, int size, int align);

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
    std::vector<Member> members_;
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

    virtual const char *name() const = 0;
};
