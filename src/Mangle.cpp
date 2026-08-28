#include "Mangle.h"

#include "Type.h"

#include <vector>

namespace {

// A type is const-qualified in its own right when it is not the same object
// as its unqualified self. Type::isConst() cannot be used for this: it also
// answers true for an array of const elements, where the qualifier is on the
// element and the array itself is unqualified.
bool qualifiedItself(const Type *t) { return t->unqualified() != t; }

const char *itaniumBuiltin(Kind k) {
    switch (k) {
    case Kind::Void:       return "v";
    case Kind::Bool:       return "b";
    case Kind::Char:       return "c";
    case Kind::SChar:      return "a";
    case Kind::UChar:      return "h";
    case Kind::Short:      return "s";
    case Kind::UShort:     return "t";
    case Kind::Int:        return "i";
    case Kind::UInt:       return "j";
    case Kind::Long:       return "l";
    case Kind::ULong:      return "m";
    case Kind::LongLong:   return "x";
    case Kind::ULongLong:  return "y";
    case Kind::Float:      return "f";
    case Kind::Double:     return "d";
    case Kind::LongDouble: return "e";
    default:               return nullptr;
    }
}

const char *microsoftBuiltin(Kind k) {
    switch (k) {
    case Kind::Void:       return "X";
    case Kind::Bool:       return "_N";
    case Kind::Char:       return "D";
    case Kind::SChar:      return "C";
    case Kind::UChar:      return "E";
    case Kind::Short:      return "F";
    case Kind::UShort:     return "G";
    case Kind::Int:        return "H";
    case Kind::UInt:       return "I";
    case Kind::Long:       return "J";
    case Kind::ULong:      return "K";
    case Kind::LongLong:   return "_J";
    case Kind::ULongLong:  return "_K";
    case Kind::Float:      return "M";
    case Kind::Double:     return "N";
    case Kind::LongDouble: return "O";
    default:               return nullptr;
    }
}

class Mangler {
public:
    bool ok = true;
    std::string problem;
    std::string out;

protected:
    void refuse(const std::string &why) {
        if (ok) { ok = false; problem = why; }
    }
    const std::string *tagOf(const Type *t) {
        const std::string &tag = t->tag();
        if (tag.empty()) {
            refuse("an unnamed class has no name to give the linker - give the "
                   "struct or union a tag, or reach it through a typedef, "
                   "which names it");
            return nullptr;
        }
        return &tag;
    }
};

// ---------------------------------------------------------------- Itanium

class Itanium : public Mangler {
public:
    // <nested-name> ::= N [<CV-qualifiers>] <prefix> <unqualified-name> E
    // The K is the const on `this` and it comes before the class, not after
    // it: _ZNK5Point4cgetEv, measured.
    // The class named in the prefix is the first substitution candidate: a
    // parameter mentioning it again is S_, not the name spelled twice.
    // **The prefix of a nested-name: every enclosing class, outermost first,
    // and each one a substitution candidate of its own.** That is what makes a
    // parameter of type Outer::Inner read `NS_5InnerE` inside a member of
    // Outer - Outer is candidate zero there, so its name is not spelled twice.
    // Measured: clang writes _ZN5Outer3useENS_5InnerE.
    void prefix(const Type *cls, const std::string &fallback) {
        if (cls == nullptr) {
            out += std::to_string(fallback.size());
            out += fallback;
            return;
        }
        if (substituted(cls)) return;
        if (cls->enclosing() != nullptr) prefix(cls->enclosing(), std::string());
        const std::string &one = cls->localName();
        out += std::to_string(one.size());
        out += one;
        subs_.push_back(cls);
    }

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, bool constThis) {
        out = "_ZN";
        if (constThis) out += "K";
        prefix(clsType, cls);
        out += std::to_string(name.size());
        out += name;
        out += "E";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // _ZN4PolyaSERKS_ - the class, then `aS` where a member function writes
    // the length and letters of its name, then E and the parameters. There is
    // no return type in an Itanium function name, and the class is candidate
    // zero in the substitution table, which is what makes the parameter S_.
    void copyAssign(const std::string &cls, const Type *clsType, const Type *fn) {
        out = "_ZN";
        prefix(clsType, cls);
        out += "aSE";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty()) { out += "v"; return; }
        for (const Type *p : params) type(p);
    }

    // _ZN5PointC1Eii - the class, then C1 or C2, then E, then the parameters.
    // **A constructor has both names**: C1 builds a complete object and C2 a
    // base subobject, and clang emits the two of them. Nothing calls C2 until
    // a derived class does, but the object file is not the same object file
    // without it - so both are emitted, C2 as a label in front of C1's body.
    void constructor(const std::string &cls, const Type *clsType,
                     const Type *fn, bool complete) {
        out = "_ZN";
        prefix(clsType, cls);
        out += complete ? "C1E" : "C2E";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    void destructor(const std::string &cls, const Type *clsType, bool complete) {
        out = "_ZN";
        prefix(clsType, cls);
        out += complete ? "D1Ev" : "D2Ev";
    }

    void staticMember(const std::string &cls, const Type *clsType,
                      const std::string &name) {
        out = "_ZN";
        prefix(clsType, cls);
        out += std::to_string(name.size());
        out += name;
        out += "E";
    }

    void function(const std::string &name, const Type *fn, bool internal) {
        out = internal ? "_ZL" : "_Z";
        out += std::to_string(name.size());
        out += name;
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

private:
    std::vector<const Type *> subs_;

    // The seq-id: the first repeat is S_, then S0_, S1_ ... S9_, SA_ and on
    // in base 36. Anything past the first is one less than its index, which
    // is the part that is easy to get wrong.
    bool substituted(const Type *t) {
        for (std::size_t i = 0; i < subs_.size(); i++) {
            if (subs_[i] != t) continue;
            out += 'S';
            if (i > 0) {
                std::string digits;
                for (std::size_t v = i - 1;; v /= 36) {
                    digits.insert(digits.begin(),
                                  static_cast<char>(v % 36 < 10 ? '0' + v % 36
                                                                : 'A' + v % 36 - 10));
                    if (v < 36) break;
                }
                out += digits;
            }
            out += '_';
            return true;
        }
        return false;
    }

    void type(const Type *t) {
        if (!ok) return;
        if (substituted(t)) return;

        if (qualifiedItself(t)) {
            out += 'K';
            type(t->unqualified());
            subs_.push_back(t);
            return;
        }
        if (const char *b = itaniumBuiltin(t->kind())) { out += b; return; }

        if (t->isPointer())        { out += 'P'; type(t->pointee()); }
        else if (t->isReference()) { out += 'R'; type(t->referent()); }
        else if (t->isArray()) {
            out += 'A';
            out += std::to_string(t->length());
            out += '_';
            type(t->pointee());
        } else if (t->isFunction()) {
            out += 'F';
            type(t->returns());
            if (t->params().empty() && !t->isVariadicFn()) out += 'v';
            for (const Type *p : t->params()) type(p);
            if (t->isVariadicFn()) out += 'z';
            out += 'E';
        } else if (t->isStructOrUnion()) {
            // A class inside another is a nested-name here too, and prefix()
            // is what consults the substitution table on the way down.
            if (t->enclosing() != nullptr) {
                if (tagOf(t) == nullptr) return;
                out += 'N';
                prefix(t, std::string());
                out += 'E';
                return;                       // prefix() pushed it already
            }
            const std::string *tag = tagOf(t);
            if (tag == nullptr) return;
            out += std::to_string(tag->size());
            out += *tag;
        } else {
            refuse("this type has no Itanium linkage name yet");
            return;
        }
        subs_.push_back(t);
    }
};

// -------------------------------------------------------------- Microsoft

class Microsoft : public Mangler {
public:
    // ?name@Class@@ then four letters: the access, __ptr64, the constness of
    // this, and the calling convention. ?get@Point@@QEAAHXZ is public and
    // non-const; ?cget@Point@@QEBAHXZ is public and const; ?priv@C@@AEAAHXZ
    // is private. All measured.
    // **Every enclosing class, innermost first, and then the '@' that closes
    // the list.** `?get@Inner@Outer@@QEAAHXZ` - measured with cl. Each
    // component goes through pushName, so a scope mentioned again later is a
    // back-reference digit: `?use@Outer@@QEAAHUInner@1@@Z` for a parameter of
    // type Outer::Inner, where the 1 is Outer.
    void scopeOf(const Type *cls, const std::string &fallback) {
        if (cls == nullptr) pushName(fallback);
        else for (const Type *c = cls; c != nullptr; c = c->enclosing())
            pushName(c->localName());
        out += '@';               // closes the scope list
    }

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, char access, bool constThis) {
        out = "?";
        pushName(name);
        scopeOf(clsType, cls);
        out += access;            // Q public, I protected, A private
        out += 'E';               // this is __ptr64
        out += constThis ? 'B' : 'A';
        out += 'A';               // __cdecl
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // ??4X@@QEAAAEAU0@AEBU0@@Z - ??4 says operator=, and unlike ??0 it does
    // write the return type. Only the class is pushed as a name, so it is the
    // back-reference 0 rather than the 1 a named member function leaves it.
    void copyAssign(const std::string &cls, const Type *clsType, const Type *fn,
                    char access) {
        out = "??4";
        scopeOf(clsType, cls);
        out += access;
        out += "EAA";
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += "@Z";
    }

    // ??0Point@@QEAA@HH@Z - ??0 says constructor, and the '@' after QEAA sits
    // where a member function writes its return type.
    void constructor(const std::string &cls, const Type *clsType, const Type *fn,
                     char access) {
        out = "??0";
        scopeOf(clsType, cls);
        out += access;
        out += "EAA";
        out += '@';               // a constructor returns nothing to say
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    void destructor(const std::string &cls, const Type *clsType, char access) {
        out = "??1";
        scopeOf(clsType, cls);
        out += access;
        out += "EAA";
        out += "@XZ";
    }

    void function(const std::string &name, const Type *fn) {
        out = "?";
        nameComponent(name);      // the name, and the empty list of scopes
        out += "YA";              // a free function, __cdecl
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // ?pub@C@@2HA - the name, the class, then the access as a digit and the
    // type spelled the way a data symbol spells it. Measured with cl.
    void staticMember(const std::string &cls, const Type *clsType,
                      const std::string &name, const Type *t, char access) {
        out = "?";
        pushName(name);
        scopeOf(clsType, cls);
        out += access;            // '2' public, '1' protected, '0' private
        dataType(t);
    }

    void data(const std::string &name, const Type *t) {
        out = "?";
        nameComponent(name);
        out += "3";               // a variable at namespace scope
        dataType(t);
    }

    // **A data symbol's type is not spelled the way a parameter's is**, and
    // the three differences were all measured with cl:
    //
    //   int arr[3]        ?arr@@3PAHA        not Y02H - an array decays here
    //   int m2[2][3]      ?m2@@3PAY02HA      to a pointer to its element
    //   S *self           ?self@@3PEAUS@@EA  the qualifier carries E as well
    //   const char *ccp   ?ccp@@3PEBDEB      and repeats the POINTEE's const
    //
    // The last is the surprising one: `ccp` is a mutable pointer to const
    // char, and the qualifier after the type says B all the same. It is the
    // pointee being const that is written there, not the variable - a const
    // variable at namespace scope has internal linkage and no symbol to
    // disagree about.
    void dataType(const Type *t) {
        const Type *u = t->unqualified();
        if (u->isArray()) {
            out += "PA";
            type(u->pointee());
            out += t->isConst() ? "B" : "A";
            return;
        }
        type(u);
        if (u->isPointer()) {
            out += 'E';
            out += u->pointee()->isConst() ? 'B' : 'A';
            return;
        }
        out += t->isConst() ? "B" : "A";
    }

private:
    std::vector<std::string> names_;
    std::vector<const Type *> args_;

    // Names repeat by index, and the function's own name is the first one in
    // the table - so the second mention of a class is '1' where the first was
    // spelled out.
    // One component and the '@' that ends it. A free function has exactly one
    // and then an empty scope list; a member has two, its own and its class.
    // **A backreference digit replaces the whole component, its '@'
    // included.** clang writes AEBV1@@Z for a parameter repeating the class -
    // V, the digit, then only the scope-list terminator - where digit-plus-'@'
    // would give V1@@. Measured on the first case whose names actually repeat;
    // every earlier one mentioned each name once, which is why the extra '@'
    // never showed.
    void pushName(const std::string &n) {
        for (std::size_t i = 0; i < names_.size(); i++)
            if (names_[i] == n) { out += static_cast<char>('0' + i); return; }
        if (names_.size() < 10) names_.push_back(n);
        out += n;
        out += '@';
    }

    void nameComponent(const std::string &n) {
        pushName(n);
        out += '@';               // and the empty scope list
    }

    // Numbers: one less than the value as a digit up to ten, and the value
    // itself in hexadecimal above that, with A standing for 0 and a '@' to
    // close it. Both halves of that were measured against clang.
    void number(long long v) {
        if (v == 0)               { out += "A@"; return; }
        if (v >= 1 && v <= 10)    { out += static_cast<char>('0' + v - 1); return; }
        std::string digits;
        for (long long x = v; x != 0; x /= 16)
            digits.insert(digits.begin(), static_cast<char>('A' + x % 16));
        out += digits;
        out += '@';
    }

    void returnType(const Type *t) {
        // A class returned by value carries its cv-qualification in front of
        // it; everything else is written as it stands.
        if (t->isStructOrUnion()) out += "?A";
        type(t);
    }

    // An argument that is not a plain builtin can be named again by index,
    // and only arguments go in that table - a return type of the same type is
    // spelled out in full.
    void argument(const Type *t) {
        if (microsoftBuiltin(t->kind()) == nullptr) {
            for (std::size_t i = 0; i < args_.size(); i++)
                if (args_[i] == t) { out += static_cast<char>('0' + i); return; }
            if (args_.size() < 10) args_.push_back(t);
        }
        type(t);
    }

    void pointee(const Type *p) {
        // A function has no storage class after the pointer that reaches it,
        // and an array carries its dimensions there instead.
        if (p->isFunction()) {
            out += "6A";
            type(p->returns());
            if (p->params().empty() && !p->isVariadicFn()) { out += "XZ"; return; }
            for (const Type *a : p->params()) type(a);
            out += p->isVariadicFn() ? "ZZ" : "@Z";
            return;
        }
        // The qualifier asked about here is the pointee's own. An array of
        // const elements is not itself const - its elements carry that, and
        // they are marked where the elements are written - so isConst(),
        // which answers for the elements too, is the wrong question.
        out += qualifiedItself(p) ? "EB" : "EA";
        type(p);
    }

    void type(const Type *t) {
        if (!ok) return;

        if (t->isPointer())        { out += qualifiedItself(t) ? 'Q' : 'P';
                                     pointee(t->pointee()); return; }
        if (t->isReference())      { out += 'A'; pointee(t->referent()); return; }
        if (t->isArray()) {
            out += 'Y';
            std::vector<long long> dims;
            const Type *elem = t;
            while (elem->isArray()) { dims.push_back(elem->length()); elem = elem->pointee(); }
            number(static_cast<long long>(dims.size()));
            for (long long d : dims) number(d);
            // A const element is written with the qualifier marker rather
            // than by qualifying the array, which has no qualifier of its own.
            if (elem->isConst()) out += "$$CB";
            type(elem->unqualified());
            return;
        }
        if (const char *b = microsoftBuiltin(t->kind())) { out += b; return; }
        if (t->isStructOrUnion()) {
            const std::string *tag = tagOf(t);
            if (tag == nullptr) return;
            // **T union, U struct, V class** - the Microsoft ABI spells the
            // three differently, and until vtables there was nothing declared
            // with `class` whose type reached a name, so U covered both.
            // clang writes ?f@@YAHPEAVShape@@@Z where cxx1 wrote ...PEAUShape...
            // Itanium spells all three by tag and does not care.
            out += t->kind() == Kind::Union ? 'T'
                 : t->declaredClass()       ? 'V'
                                            : 'U';
            if (t->enclosing() != nullptr) { scopeOf(t, std::string()); return; }
            nameComponent(*tag);
            return;
        }
        refuse("this type has no Microsoft linkage name yet");
    }
};

}  // namespace

bool itaniumFunctionName(const std::string &name, const Type *fn, bool internal,
                         std::string *out, std::string *problem) {
    Itanium m;
    m.function(name, fn, internal);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftFunctionName(const std::string &name, const Type *fn, bool internal,
                           std::string *out, std::string *problem) {
    (void)internal;   // the Microsoft ABI spells an internal function the same
    Microsoft m;
    m.function(name, fn);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumMemberName(const std::string &cls, const Type *clsType,
                       const std::string &name,
                       const Type *fn, bool constThis,
                       std::string *out, std::string *problem) {
    Itanium m;
    m.memberFunction(cls, clsType, name, fn, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftMemberName(const std::string &cls, const Type *clsType,
                         const std::string &name,
                         const Type *fn, char access, bool constThis,
                         std::string *out, std::string *problem) {
    Microsoft m;
    m.memberFunction(cls, clsType, name, fn, access, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumCopyAssignName(const std::string &cls, const Type *clsType,
                           const Type *fn, std::string *out, std::string *problem) {
    Itanium m;
    m.copyAssign(cls, clsType, fn);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftCopyAssignName(const std::string &cls, const Type *clsType,
                             const Type *fn, char access,
                             std::string *out, std::string *problem) {
    Microsoft m;
    m.copyAssign(cls, clsType, fn, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumDestructorName(const std::string &cls, const Type *clsType,
                           bool complete, std::string *out) {
    // A destructor takes nothing and returns nothing, so there is nothing to
    // spell but the class - which is a whole nested-name once a class can be
    // written inside another.
    Itanium m;
    m.destructor(cls, clsType, complete);
    *out = m.out;
    return true;
}

std::string microsoftDestructorName(const std::string &cls, const Type *clsType,
                                    char access) {
    Microsoft m;
    m.destructor(cls, clsType, access);
    return m.out;
}

bool itaniumConstructorName(const std::string &cls, const Type *clsType,
                            const Type *fn,
                            bool complete, std::string *out, std::string *problem) {
    Itanium m;
    m.constructor(cls, clsType, fn, complete);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftConstructorName(const std::string &cls, const Type *clsType,
                              const Type *fn, char access,
                              std::string *out, std::string *problem) {
    Microsoft m;
    m.constructor(cls, clsType, fn, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

std::string itaniumDataName(const std::string &name, bool internal) {
    if (!internal) return name;
    return "_ZL" + std::to_string(name.size()) + name;
}

std::string itaniumStaticMemberName(const std::string &cls, const Type *clsType,
                                    const std::string &name) {
    Itanium m;
    m.staticMember(cls, clsType, name);
    return m.out;
}

bool microsoftStaticMemberName(const std::string &cls, const Type *clsType,
                               const std::string &name,
                               const Type *t, char access,
                               std::string *out, std::string *problem) {
    Microsoft m;
    m.staticMember(cls, clsType, name, t, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem) {
    Microsoft m;
    m.data(name, t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}
