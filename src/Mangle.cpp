#include "Mangle.h"

#include "Operator.h"

// "N::M::f" -> {"N", "M", "f"}. A namespace scope is spelled by both ABIs
// exactly as a class scope is - measured, `N::f` is `_ZN1N1fEi` and
// `?f@N@@YAHH@Z` - so the manglers need the components and nothing else.
static std::vector<std::string> scopeComponents(const std::string &name) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t sep = name.find("::", at);
        if (sep == std::string::npos) { out.push_back(name.substr(at)); break; }
        out.push_back(name.substr(at, sep - at));
        at = sep + 2;
    }
    return out;
}

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
    // Measured: `void f(decltype(nullptr))` is `_Z1fDn`.
    case Kind::NullPtr:    return "Dn";
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
    // Measured with clang: `void f(decltype(nullptr))` is `?f@@YAX$$T@Z`.
    case Kind::NullPtr:    return "$$T";
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
    // **The namespaces in a qualified tag, each a substitution candidate of
    // its own.** A namespace is not a Type here, so it cannot go in the table
    // by pointer the way a class does - it goes in by the name it is reached
    // under, which is the cumulative "N::M" and not just "M", so that two
    // namespaces of the same leaf name in different parents stay apart.
    // Measured: `g(N::M::T, N::S)` inside N::M is `_ZN1N1M1gENS0_1TENS_1SE`,
    // where S_ is N and S0_ is N::M - both candidates, and neither the class.
    // The class's own name is *not* written here; the caller writes it, since
    // what it is called differs between a plain class and a specialization.
    void namespacesOf(const std::string &qualified) {
        const std::vector<std::string> parts = scopeComponents(qualified);
        std::vector<std::string> reach;               // N, then N::M, ...
        std::string sofar;
        for (std::size_t i = 0; i + 1 < parts.size(); i++) {
            sofar += sofar.empty() ? parts[i] : "::" + parts[i];
            reach.push_back(sofar);
        }
        // **The longest prefix already in the table stands for the whole of
        // it.** Writing `S_` for N and then `S0_` for N::M spells the same
        // scope twice; N::M alone is what clang writes.
        std::size_t start = 0;
        for (std::size_t i = reach.size(); i-- > 0; ) {
            if (!substitutedName(reach[i])) continue;
            start = i + 1;
            break;
        }
        for (std::size_t i = start; i < reach.size(); i++) {
            out += std::to_string(parts[i].size());
            out += parts[i];
            Sub s;
            s.name = reach[i];
            subs_.push_back(s);
        }
    }

    void prefix(const Type *cls, const std::string &fallback) {
        if (cls == nullptr) {
            // A namespace-qualified tag with no Type to walk: split it, the
            // same way a free function's name is split.
            namespacesOf(fallback);
            const std::string leaf = scopeComponents(fallback).back();
            out += std::to_string(leaf.size());
            out += leaf;
            return;
        }
        if (substituted(cls)) return;
        if (cls->enclosing() != nullptr) prefix(cls->enclosing(), std::string());
        // A class in a namespace has no enclosing Type; its namespaces are in
        // its tag, and they are written - and made candidates - here. A local
        // class's tag has a "::" in it too and is *not* this: `f::L` is one
        // name, not a scope, which is why the flag is asked rather than the
        // spelling.
        else if (cls->inNamespace()) namespacesOf(cls->tag());
        if (cls->isSpecialization()) {
            templateId(cls);
        } else {
            const std::string &one = cls->localName();
            out += std::to_string(one.size());
            out += one;
        }
        subs_.push_back(Sub{ cls, std::string() });
    }

    // An operator's code stands exactly where an ordinary name writes its
    // length and its letters - `_ZNK1VplERKS_` against `_ZNK1V3addERKS_` -
    // and everything on either side of it is unchanged. `unary` picks between
    // the two codes Itanium gives a token that has both forms.
    void writtenName(const std::string &name, bool unary) {
        const std::string spelling = operatorSpelling(name);
        if (const OperatorCode *op = findOperator(spelling)) {
            out += itaniumOperatorCode(*op, unary);
            return;
        }
        out += std::to_string(name.size());
        out += name;
    }

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, bool constThis) {
        out = "_ZN";
        if (constThis) out += "K";
        prefix(clsType, cls);
        // A member operator's operand count is one more than its parameter
        // count, `this` being the first, so a member with no parameter is the
        // unary form.
        writtenName(name, fn->params().empty());
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

    // D1 complete, D2 base-subobject, D0 the deleting form that lives in a
    // vtable. All three are the same nested-name with two characters after it.
    void destructor(const std::string &cls, const Type *clsType, char form) {
        out = "_ZN";
        prefix(clsType, cls);
        out += 'D';
        out += form;
        out += "Ev";
    }

    void staticMember(const std::string &cls, const Type *clsType,
                      const std::string &name) {
        out = "_ZN";
        prefix(clsType, cls);
        out += std::to_string(name.size());
        out += name;
        out += "E";
    }

    // The type as a signature spells it, with nothing around it - which is
    // what follows `_ZTI`.
    void typeInfoFor(const Type *t) { type(t); }

    void function(const std::string &name, const Type *fn, bool internal) {
        out = internal ? "_ZL" : "_Z";
        // **A name with a scope in it is a nested-name**, `_ZN1N1fEi`, and a
        // namespace component is written exactly as a class one is.
        const std::vector<std::string> parts = scopeComponents(name);
        if (parts.size() > 1) {
            out += "N";
            // Every component but the last is a prefix, and a prefix is a
            // substitution candidate - which is what makes a parameter of type
            // N::S inside N::f read `NS_1SE` and not `N1N1SE`.
            namespacesOf(name);
            writtenName(parts.back(), fn->params().size() == 1);
            out += "E";
        } else {
            // A non-member operator carries every operand in its parameter
            // list, so one parameter is the unary form where a member's zero
            // is.
            writtenName(name, fn->params().size() == 1);
        }
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // A function template specialization, mangled from the template's own
    // signature and the arguments it was given - never from the substituted
    // one, which cannot say where a type came from.
    //
    // **A specialization encodes its return type and an ordinary function
    // does not**, which is the rule most likely to be guessed wrong: without
    // it two specializations differing only in return type would share a
    // symbol, and there is nothing else in the name to tell them apart.
    void templateFunction(const std::string &name, const Type *pattern,
                          const std::vector<TemplateArg> &args, bool internal) {
        out = internal ? "_ZL" : "_Z";
        out += std::to_string(name.size());
        out += name;

        // **The template name is substitution candidate zero.** Measured:
        // `void f4(T, T)` with T=int is _Z2f4IiEvT_S0_, and the second T_ is
        // S0_ - index one. Something occupies index zero before the arguments
        // are written, and the only thing written by then is the name. The
        // entry is the name itself, which is what the ABI makes a candidate.
        Sub self;
        self.name = name;
        subs_.push_back(self);

        out += 'I';
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += 'E';

        type(pattern->returns());
        const std::vector<const Type *> &params = pattern->params();
        if (params.empty() && !pattern->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (pattern->isVariadicFn()) out += "z";
    }

    // `Li3E` - the value, with its own type in front and `n` for a negative
    // one. Measured: num<-11>() is _Z3numILin11EEiv.
    void templateArgument(const TemplateArg &a) {
        // `J i c E` - a pack argument is its members between J and E, and an
        // empty one is `JE`. Measured: _Z7nothingIJicEEiv, _Z5totalIiJEEiT_DpT0_.
        if (a.isPack) {
            out += 'J';
            for (std::size_t i = 0; i < a.pack.size(); i++) type(a.pack[i]);
            out += 'E';
            return;
        }
        if (a.isType) { type(a.type); return; }
        out += 'L';
        type(a.type);
        if (a.value < 0) { out += 'n'; out += std::to_string(-a.value); }
        else             { out += std::to_string(a.value); }
        out += 'E';
    }

private:
    // **A candidate is a type or a template's name.** The name is a candidate
    // of its own - measured: `void two(Holder<int>, Holder<double>)` is
    // _Z3two6HolderIiES_IdE, where the S_ is the word "Holder" and not any
    // type. So the table holds one or the other and never both.
    struct Sub {
        const Type *type = nullptr;
        std::string name;
    };
    std::vector<Sub> subs_;

    // The seq-id: the first repeat is S_, then S0_, S1_ ... S9_, SA_ and on
    // in base 36. Anything past the first is one less than its index, which
    // is the part that is easy to get wrong.
    void seqId(std::size_t i) {
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
    }

    bool substituted(const Type *t) {
        for (std::size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].type == t && subs_[i].name.empty()) { seqId(i); return true; }
        return false;
    }

    bool substitutedName(const std::string &n) {
        for (std::size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].name == n) { seqId(i); return true; }
        return false;
    }

    // `3BoxIiLi3EE` - the template's name, then the arguments between I and
    // E. Two candidates come out of it and in this order: the name, then the
    // whole thing. Measured on _Z6nested6HolderIS_IiEE, where the inner
    // Holder is the S_ that the outer one's name left behind.
    void templateId(const Type *t) {
        if (!substitutedName(t->templateName())) {
            out += std::to_string(t->templateName().size());
            out += t->templateName();
            Sub s;
            s.name = t->templateName();
            subs_.push_back(s);
        }
        out += 'I';
        const std::vector<TemplateArg> &args = t->templateArgs();
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += 'E';
    }

    void type(const Type *t) {
        if (!ok) return;
        if (substituted(t)) return;

        // **The qualifier comes first, and asking about it first is what
        // makes `const T &` come out RKT_ rather than RT_.** A qualified copy
        // of a template parameter still answers TemplateParam for its kind,
        // so a branch on the kind placed above this one silently drops the K.
        if (qualifiedItself(t)) {
            out += 'K';
            type(t->unqualified());
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // `Dp` and then what is being expanded, which is a parameter and so
        // says the same thing however many members the pack has. Measured:
        // `void f(Ts...)` is `DpT_` at every size.
        if (t->kind() == Kind::PackExpansion) {
            out += "Dp";
            type(t->pointee());
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // `N <owner> <len><name> E` - measured: `Value<T>::type` in a return
        // type is `N5ValueIT_E4typeE` and `T::type` is `NT_4typeE`. It is a
        // nested-name like any other, and the only new thing is what stands
        // in the prefix.
        if (t->kind() == Kind::DependentMember) {
            out += 'N';
            type(t->pointee());
            out += std::to_string(t->tag().size());
            out += t->tag();
            out += 'E';
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // **`T_` is the first parameter and `T0_` the second** - the same
        // seq-id shape the substitution table uses, and a candidate in that
        // table itself, which is what makes the second mention of T an S.
        if (t->kind() == Kind::TemplateParam) {
            out += 'T';
            if (t->length() > 0) out += std::to_string(t->length() - 1);
            out += '_';
            subs_.push_back(Sub{ t, std::string() });
            return;
        }
        if (const char *b = itaniumBuiltin(t->kind())) { out += b; return; }

        if (t->isPointer())        { out += 'P'; type(t->pointee()); }
        // **`M` and then the class and the member's type** - measured,
        // `int S::*` is `M1Si` and `double S::*` is `M1Sd`. The class goes in
        // as a type and not as a nested-name, so it takes part in the
        // substitution table like any other.
        else if (t->isMemberPointer() || t->isMemberFunctionPointer()) {
            // The same `M` for both: measured, `int S::*` is `M1Si` and
            // `int (S::*)()` is `M1SFivE`. A member function pointer wears the
            // shape of a struct so the backends can copy it, so it is asked
            // about separately - the kind says Struct and only the flag knows.
            out += 'M';
            type(t->unqualified()->enclosing());
            type(t->unqualified()->pointee());
        }
        // `R` for an lvalue reference and `O` for an rvalue one - measured:
        // `f(int &&)` is _Z1fOi where `g(const int &)` is _Z1gRKi.
        else if (t->isReference()) {
            out += t->isRValueReference() ? 'O' : 'R';
            type(t->referent());
        }
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
            if (t->enclosing() != nullptr || t->inNamespace()) {
                if (tagOf(t) == nullptr) return;
                out += 'N';
                prefix(t, std::string());
                out += 'E';
                return;                       // prefix() pushed it already
            }
            if (t->isSpecialization()) {
                templateId(t);
                subs_.push_back(Sub{ t, std::string() });
                return;                       // pushed here, not below
            }
            const std::string *tag = tagOf(t);
            if (tag == nullptr) return;
            out += std::to_string(tag->size());
            out += *tag;
        } else {
            refuse("this type has no Itanium linkage name yet");
            return;
        }
        subs_.push_back(Sub{ t, std::string() });
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
    // `localOwner` is the enclosing function's linkage name when the class is
    // defined inside one, and empty otherwise. It goes in as one more scope
    // component - the innermost-first list is class, then function - written
    // `?1?` and then that whole name. Measured: ?get@L@?1??f@@YAHXZ@QEAAHXZ.
    //
    // **It is not pushed as a back-reference**, and neither is anything
    // inside it: the embedded name carries its own table.
    void scopeOf(const Type *cls, const std::string &fallback,
                 const std::string &localOwner = std::string()) {
        // Innermost first. A namespace is not a Type, so once the chain of
        // enclosing classes runs out the outermost one's tag is split for
        // whatever namespaces it carries. Measured: `?g@M@N@@YAH...`.
        if (cls == nullptr) {
            const std::vector<std::string> parts = scopeComponents(fallback);
            for (std::size_t i = parts.size(); i-- > 0; ) pushName(parts[i]);
        } else {
            for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
                pushName(componentOf(c));
                if (c->enclosing() == nullptr && c->inNamespace()) {
                    const std::vector<std::string> parts = scopeComponents(c->tag());
                    for (std::size_t i = parts.size() - 1; i-- > 0; )
                        pushName(parts[i]);
                }
            }
        }
        if (!localOwner.empty()) {
            out += "?1?";
            // A function with no decorated name - main, or extern "C" - is
            // written ?name@@9, the 9 saying the name carries no type.
            if (!localOwner.empty() && localOwner[0] == '?') out += localOwner;
            else { out += '?'; out += localOwner; out += "@@9"; }
        }
        out += '@';               // closes the scope list
    }

    // What one scope component is *called*. For an ordinary class that is its
    // own name; for a specialization it is the whole template-id, built here
    // and then pushed as one name - measured, `?copyFrom@?$Holder@H@@QEAAX...`
    // where the parameter's back-reference 1 stands for `?$Holder@H`.
    //
    // **Built with the tables put aside**, the same rule a function
    // template's id follows: in `?withClass@@YAXU?$Holder@US@@@@US@@@Z` the S
    // written inside the argument list is invisible outside it, so the second
    // parameter spells S again rather than referring back.
    std::string componentOf(const Type *c) {
        if (!c->isSpecialization()) return c->localName();

        std::vector<std::string> outerNames;
        std::vector<const Type *> outerArgs;
        std::string outerOut;
        outerNames.swap(names_);
        outerArgs.swap(args_);
        outerOut.swap(out);

        out = "?$";
        pushName(c->templateName());
        const std::vector<TemplateArg> &args = c->templateArgs();
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        std::string component;
        component.swap(out);

        names_.swap(outerNames);
        args_.swap(outerArgs);
        out.swap(outerOut);
        return component;
    }

    // **An operator replaces the whole `?name@` and is not pushed as a
    // back-reference**, which is the one thing here that cannot be guessed
    // and was measured: in `??HV@@QEBA?AU0@D@Z` the class is back-reference
    // *0*, where a named member function would have left it 1. The same rule
    // the ??4 of operator= already followed, now written once.
    //
    // Arity plays no part: `??D` is multiplication and dereference both, and
    // the parameter list is what tells them apart. Only Itanium needs to know.
    bool operatorPrefix(const std::string &name) {
        const OperatorCode *op = findOperator(operatorSpelling(name));
        if (op == nullptr) return false;
        out = "??";
        out += op->microsoft;
        return true;
    }

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, char access, bool constThis,
                        const std::string &localOwner = std::string()) {
        if (!operatorPrefix(name)) {
            out = "?";
            pushName(name);
        }
        scopeOf(clsType, cls, localOwner);
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

    // ??_GVB@@UEAAPEAXI@Z - the deleting destructor, which takes a flag beside
    // `this` and answers `this`.
    void deletingDestructor(const std::string &cls, const Type *clsType) {
        out = "??_G";
        scopeOf(clsType, cls);
        out += "UEAAPEAXI@Z";
    }

    void function(const std::string &name, const Type *fn) {
        const std::vector<std::string> parts = scopeComponents(name);
        if (parts.size() > 1) {
            // `?g@M@N@@YAHXZ` - the name, then the scopes innermost first,
            // then the '@' that closes the list. The same shape a member
            // function has, a namespace being a scope like any other.
            // An operator written in a namespace keeps its code: measured,
            // `N::operator+` is `??HN@@YA...` and not `?operator+@N@@YA...`.
            if (!operatorPrefix(parts.back())) {
                out = "?";
                pushName(parts.back());
            }
            for (std::size_t i = parts.size() - 1; i-- > 0; ) pushName(parts[i]);
            out += '@';
        } else if (operatorPrefix(name)) {
            out += '@';           // the empty list of scopes
        } else {
            out = "?";
            nameComponent(name);  // the name, and the empty list of scopes
        }
        out += "YA";              // a free function, __cdecl
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // ??$twice@H@@YAHH@Z - `??$` where an ordinary function has `?`, then the
    // template-id as one scope component, then the empty enclosing scope
    // list, and from there an ordinary free function. The signature written
    // is the *substituted* one: H for the return type where Itanium writes
    // T_. Measured with cl.
    void templateFunction(const std::string &name, const Type *fn,
                          const std::vector<TemplateArg> &args) {
        out = "??$";
        templateId(name, args);
        out += '@';               // the empty enclosing scope list
        out += "YA";
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // **A template-id carries back-reference tables of its own.** Measured
    // with cl: `T same(T)` at T=S is ??$same@US@@@@YA?AUS@@U0@, and the
    // parameter's name back-reference 0 is the S the *return type* pushed -
    // the S inside the argument list is invisible to the signature. So both
    // tables are put aside, used fresh, and put back.
    void templateId(const std::string &name,
                    const std::vector<TemplateArg> &args) {
        std::vector<std::string> outerNames;
        std::vector<const Type *> outerArgs;
        outerNames.swap(names_);
        outerArgs.swap(args_);
        pushName(name);
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += '@';               // closes the template-id
        names_.swap(outerNames);
        args_.swap(outerArgs);
    }

    // `$02` for 3, and `$0?4` for -5: `$0`, then a '?' if it is negative,
    // then the magnitude through number(). Measured with cl.
    void templateArgument(const TemplateArg &a) {
        // A pack's members are written one after another with nothing around
        // them, and an empty pack is `$$V`. Measured with cl:
        // ??$total@HH@@YAHHH@Z for a pack of one, ??$total@H$$V@@YAHH@Z for none.
        if (a.isPack) {
            if (a.pack.empty()) { out += "$$V"; return; }
            for (std::size_t i = 0; i < a.pack.size(); i++) {
                TemplateArg one;
                one.type = a.pack[i];
                templateArgument(one);
            }
            return;
        }
        if (a.isType) {
            // **A top-level const on a type argument is spelled `$$CB`**,
            // and only where the thing under it is not a pointer: `const int`
            // is `$$CBH`, while `const int *` is `PEBH` and `int *const` is
            // `QEAH` - the P becoming a Q, which type() already writes.
            // Without any of this the const was simply dropped, and
            // `W<const int>` shared one symbol with `W<int>`: two different
            // classes, one name, and the wrong body called.
            if (a.type->unqualified() != a.type && !a.type->isPointer()) {
                out += "$$CB";
                type(a.type->unqualified());
                return;
            }
            type(a.type);
            return;
        }
        out += "$0";
        if (a.value < 0) { out += '?'; number(-a.value); }
        else             { number(a.value); }
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
        // `?v@N@@3HA` - the name, then the namespaces innermost first, then
        // the '@' that closes the list, exactly as a function's scopes go.
        const std::vector<std::string> parts = scopeComponents(name);
        if (parts.size() > 1) {
            pushName(parts.back());
            for (std::size_t i = parts.size() - 1; i-- > 0; ) pushName(parts[i]);
            out += '@';
        } else {
            nameComponent(name);
        }
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

    // A name whose scopes are written into it - `N::S`, a class in a
    // namespace, which has no `enclosing()` to walk because a namespace is not
    // a Type. Innermost first, the same order scopeOf() walks in, and each
    // component is a back-reference candidate of its own.
    // Measured: `use(N::S)` is `?use@@YAHUS@N@@@Z`.
    void qualifiedName(const std::string &n) {
        const std::vector<std::string> parts = scopeComponents(n);
        for (std::size_t i = parts.size(); i-- > 0; ) pushName(parts[i]);
        out += '@';               // closes the scope list
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
        // **`PEQ` and then the class's scope and the member's type** -
        // measured, `int S::*` is `PEQS@@H` where an ordinary `int *` is
        // `PEAH`. The Q sits where a pointer's A does and the class comes
        // between it and the type.
        if (t->isMemberPointer()) {
            const Type *plain = t->unqualified();
            out += "PEQ";
            scopeOf(plain->enclosing(), plain->enclosing()->tag());
            type(plain->pointee());
            return;
        }
        // **`P8` and then the class, where a data member pointer writes
        // `PEQ`** - measured, `int (S::*)()` is `P8S@@EAAHXZ`. The `EAA` is
        // the same near/__cdecl/non-const `this` a member function's own name
        // carries, and the signature follows exactly as one.
        if (t->isMemberFunctionPointer()) {
            const Type *plain = t->unqualified();
            const Type *fn = plain->pointee();
            out += "P8";
            scopeOf(plain->enclosing(), plain->enclosing()->tag());
            out += "EAA";
            returnType(fn->returns());
            const std::vector<const Type *> &ps = fn->params();
            if (ps.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
            for (const Type *one : ps) argument(one);
            out += fn->isVariadicFn() ? "ZZ" : "@Z";
            return;
        }
        // `$$Q` where an lvalue reference writes `A`, and the qualifier that
        // follows is the same one either way - measured with clang for the
        // Microsoft target: `?f@@YAH$$QEAH@Z` beside `?g@@YAHAEBH@Z`.
        if (t->isReference()) {
            if (t->isRValueReference()) out += "$$Q";
            else                        out += 'A';
            pointee(t->referent());
            return;
        }
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
            if (t->enclosing() != nullptr || t->isSpecialization() ||
                t->inNamespace()) {
                scopeOf(t, std::string());
                return;
            }
            qualifiedName(*tag);
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

std::string vtableSymbol(const std::string &tag, bool microsoft) {
    const std::vector<std::string> parts = scopeComponents(tag);
    if (microsoft) {
        std::string out = "??_7";
        for (std::size_t i = parts.size(); i-- > 0; ) { out += parts[i]; out += '@'; }
        return out + "@6B@";
    }
    std::string out = "_ZTV";
    if (parts.size() > 1) out += 'N';
    for (const std::string &part : parts) {
        out += std::to_string(part.size());
        out += part;
    }
    if (parts.size() > 1) out += 'E';
    return out;
}

bool itaniumTypeInfoName(const Type *t, std::string *out, std::string *problem) {
    if (itaniumBuiltin(t->kind()) == nullptr) {
        *problem = "only a fundamental type has a type_info object the "
                   "standard library already carries; one for '" +
                   t->describe() + "' would have to be emitted here, and that "
                   "is its own step";
        return false;
    }
    Itanium m;
    m.out = "_ZTI";
    m.typeInfoFor(t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftThrowNames(const Type *t, int size, MicrosoftThrow *out,
                         std::string *problem) {
    const char *letter = microsoftBuiltin(t->kind());
    if (letter == nullptr) {
        *problem = "only a fundamental type has a type descriptor this "
                   "compiler can name; one for '" + t->describe() + "' would "
                   "have to be emitted here, and that is its own step";
        return false;
    }
    // The type's own letter runs through all four names, which is what makes
    // them agree without anything having to be passed between them. Measured
    // with cl: int is ??_R0H@8, _CT??_R0H@84, _CTA1H, _TI1H, and double is
    // the same with N and a size of 8.
    const std::string code = letter;
    out->size = size;
    out->decorated = "." + code;
    out->descriptor = "??_R0" + code + "@8";
    out->catchable = "_CT" + out->descriptor + std::to_string(size);
    out->array = "_CTA1" + code;
    out->info = "_TI1" + code;
    return true;
}

bool itaniumTemplateFunctionName(const std::string &name, const Type *pattern,
                                 const std::vector<TemplateArg> &args,
                                 bool internal,
                                 std::string *out, std::string *problem) {
    Itanium m;
    m.templateFunction(name, pattern, args, internal);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftTemplateFunctionName(const std::string &name, const Type *fn,
                                   const std::vector<TemplateArg> &args,
                                   std::string *out, std::string *problem) {
    Microsoft m;
    m.templateFunction(name, fn, args);
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

// Itanium wraps the ordinary name rather than building a different one:
// <local-name> ::= Z <function encoding> E <entity>. So the member is spelled
// exactly as it would be outside a function, and both it and the enclosing
// function's name give up their _Z to sit inside the wrapper.
bool itaniumLocalMemberName(const std::string &owner, const std::string &cls,
                            const Type *clsType, const std::string &name,
                            const Type *fn, bool constThis,
                            std::string *out, std::string *problem) {
    std::string inner;
    if (!itaniumMemberName(cls, clsType, name, fn, constThis, &inner, problem))
        return false;

    std::string function;
    if (inner.compare(0, 2, "_Z") != 0) { *problem = "an unmangled member"; return false; }
    if (owner.compare(0, 2, "_Z") == 0) {
        function = owner.substr(2);
    } else {
        // No mangled name to take apart - `main`, or `extern "C"`. Itanium
        // writes the plain source name as a length-and-letters component,
        // measured: _ZZ4mainEN1B3getEv.
        function = std::to_string(owner.size()) + owner;
    }
    *out = "_ZZ" + function + "E" + inner.substr(2);
    return true;
}

bool microsoftLocalMemberName(const std::string &owner, const std::string &cls,
                              const Type *clsType, const std::string &name,
                              const Type *fn, char access, bool constThis,
                              std::string *out, std::string *problem) {
    Microsoft m;
    m.memberFunction(cls, clsType, name, fn, access, constThis, owner);
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
    m.destructor(cls, clsType, complete ? '1' : '2');
    *out = m.out;
    return true;
}

std::string itaniumDeletingDestructorName(const std::string &cls,
                                          const Type *clsType) {
    Itanium m;
    m.destructor(cls, clsType, '0');
    return m.out;
}

std::string microsoftDeletingDestructorName(const std::string &cls,
                                            const Type *clsType) {
    Microsoft m;
    m.deletingDestructor(cls, clsType);
    return m.out;
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
    // **A variable in a namespace is a nested-name**, `_ZN1N1vE` - measured.
    // One at file scope keeps the name it was written with, which is what lets
    // C name it, and that is the case this used to be the whole of.
    const std::vector<std::string> parts = scopeComponents(name);
    if (parts.size() > 1) {
        std::string out = "_ZN";
        for (std::size_t i = 0; i < parts.size(); i++) {
            out += std::to_string(parts[i].size());
            out += parts[i];
        }
        return out + "E";
    }
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
