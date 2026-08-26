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

    void data(const std::string &name, const Type *t) {
        out = "?";
        nameComponent(name);
        out += "3";               // a variable at namespace scope
        type(t->unqualified());
        out += t->isConst() ? "B" : "A";
    }

private:
    std::vector<std::string> names_;
    std::vector<const Type *> args_;

    // Names repeat by index, and the function's own name is the first one in
    // the table - so the second mention of a class is '1' where the first was
    // spelled out.
    void nameComponent(const std::string &n) {
        for (std::size_t i = 0; i < names_.size(); i++)
            if (names_[i] == n) { out += static_cast<char>('0' + i); out += '@'; return; }
        if (names_.size() < 10) names_.push_back(n);
        out += n;
        out += '@';
        out += '@';
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
            out += t->kind() == Kind::Union ? 'T' : 'U';
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

std::string itaniumDataName(const std::string &name, bool internal) {
    if (!internal) return name;
    return "_ZL" + std::to_string(name.size()) + name;
}

bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem) {
    Microsoft m;
    m.data(name, t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}
