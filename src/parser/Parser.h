#pragma once

#include "../Ast.h"
#include "../Lexer.h"
#include "../Mangle.h"
#include "../Type.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Source;

class Parser {
public:
    Parser(const Source &src, std::vector<Token> tokens,
           TypeTable &types, const Target &target, int structReturnLimit,
           bool aggregatesByReference = false,
           bool homogeneousFloatAggregates = false)
        : src_(src), tokens_(std::move(tokens)), types_(types), target_(target),
          structReturnLimit_(structReturnLimit),
          aggregatesByReference_(aggregatesByReference),
          homogeneousFloatAggregates_(homogeneousFloatAggregates) {}

    Program parse();

private:
    struct Local {
        std::string name;
        int offset;
        const Type *type;
        bool isConst = false;
        std::string staticName;
        bool isRegister = false;
        // [expr.const]: a const object of integral type initialised with a
        // constant expression *is* one, so its value has to be kept where
        // fold() can find it later. The object still exists and still has an
        // address; this is what it is worth when read.
        bool isConstantValue = false;
        long long constantValue = 0;
        // **[class.copy]/31 lets a `return` elide the copy of an automatic
        // object, and excludes a parameter by name** - the caller destroys
        // the argument, so eliding there hands the caller two objects over
        // one set of bytes and it destroys both. Nothing else needed to know
        // a parameter from a local until then.
        bool isParameter = false;
    };

    struct GlobalSym {
        std::string name;
        std::string symbol;
        const Type *type;
        bool isConst = false;
        bool emitted = false;
        bool hasInit = false;
        bool isConstantValue = false;   // as for Local, above
        long long constantValue = 0;
    };

    struct Signature {
        std::string name;
        std::string symbol;
        const Type *returns;
        std::vector<const Type *> params;
        bool variadic;
        bool defined;
        std::size_t pos;
        // A name with C linkage, and `main`, carry one symbol and so can hold
        // one function. Recorded rather than re-derived, because the second
        // declaration is refused where it stands and the message wants to say
        // which rule stopped it.
        bool cLinkage;
        // The class this belongs to, empty for a free function. A member is
        // keyed in the table as "Point::get", so overload resolution works on
        // members with no second implementation of it.
        std::string owner;
        bool constThis = false;
        Access access = Access::Public;
        bool isVirtual = false;
        // **`explicit` on a constructor**, which changes nothing about the
        // function and only about who may pick it: copy-initialization may
        // not. Kept on the signature rather than on the class, because it is
        // one constructor of a set that is explicit and not the class.
        // Written after `access` on purpose: the members up to there are
        // filled positionally by every `Signature{...}` in the parser, so a
        // new one in the middle silently shifts them.
        bool isExplicit = false;
        // **`noexcept` on this function**, which in C++11 is not part of its
        // type - so it changes no name and no overload set, and is recorded
        // only so that the `noexcept(e)` operator can answer.
        bool isNoexcept = false;
        // **Nobody wrote this one.** An implicitly declared special member is
        // put in the table so overload resolution finds it, and given a body
        // only if something calls it. That second half is what keeps the
        // symbol list level with the oracles: cl and clang both emit an
        // implicit special member on use and not on declaration - measured.
        bool implicit = false;
        bool used = false;
        // **A specialization is a candidate like any other, and loses a tie.**
        // [over.match.best]: where a template specialization and an ordinary
        // function are equally good, the ordinary one wins. It is also never
        // a redeclaration of one, which is the other thing this flag is asked.
        bool fromTemplate = false;
    };

    // One vtable slot: the function it currently points at, and enough of the
    // declaration to tell an override from an unrelated function of the same
    // name. Slots keep the order the base first declared them in - that is
    // what lets a Base * and a Derived * agree on where to look.
    struct VSlot {
        std::string name;
        std::string symbol;
        std::vector<const Type *> params;
        bool constThis;
    };
    std::map<std::string, std::vector<VSlot> > vtables_;
    // Where a class's secondary vptr for a given base points into its table,
    // keyed "Derived::Base". Filled while the table is laid out, read by the
    // constructor that has to store it.
    std::map<std::string, int> secondaryVptr_;
    // A member function body written inside the class, held until the class
    // closes. `start` is the token the whole declaration begins at, so the
    // replay re-reads the return type and parameters too rather than trying to
    // reconstruct them.
    struct PendingBody {
        std::string tag;
        std::size_t start;
        // The class's name **as the source wrote it**, which is not the tag
        // for a specialization: the body of `Holder<int>` still says
        // `Holder(` for its constructor. The tag qualifies; this recognises.
        std::string local;
        // The function table's key for the member this body belongs to. Only
        // a specialization's bodies are gated on it: clang instantiates a
        // member function of a class template only where something calls one,
        // and emitting the rest would put symbols in the object that no
        // oracle has.
        std::string key;
    };
    std::vector<PendingBody> pendingBodies_;
    void replayInlineBodies(std::vector<PendingBody> mine);
    void skipBracedBlock();

    // ---- Rung 5.1: the template table, and nothing instantiated ----
    //
    // One template parameter as written: `class T` / `typename T`, or a
    // non-type one such as `int N`. Nothing is bound to either yet.
    struct TemplateParam {
        std::string name;
        const Type *type = nullptr;   // null for a type parameter
        std::size_t pos = 0;
        // `class... Ts` - it stands for a list, and only the last parameter
        // may be one, since everything after it could never be deduced.
        bool isPack = false;
    };
    // What is known about a name that names a template. `start` is the
    // `template` keyword: instantiation here will be a replay of these
    // tokens, so where they begin is the thing worth keeping.
    struct TemplateDecl {
        std::string name;
        std::vector<TemplateParam> params;
        bool isClass = false;
        bool defined = false;
        std::size_t start = 0;
        // The token after the `>` that closed the parameter list, which is
        // where the declaration proper begins. Instantiating is re-reading
        // from here with the arguments bound.
        std::size_t afterParams = 0;
        std::size_t pos = 0;
        // **A member of this class template defined outside it** -
        // `template <class T> T Box<T>::get(int)`. Kept on the class rather
        // than as a template of its own, because that is what it is: the
        // member was declared in the body and this is where its definition
        // happens to be written. Replayed when the class is instantiated,
        // and only for a member something calls.
        struct OutOfLine {
            std::size_t start = 0;
            std::string member;    // "get", or the class's name for a ctor
            bool destructor = false;
        };
        std::vector<OutOfLine> outOfLine;

        // **A partial specialization: a second body for the argument lists
        // that match a pattern.** `template <class T> struct Box<T *>` has
        // parameters of its own, and its arguments are patterns rather than
        // types - which is why they are kept as written and matched at every
        // use rather than resolved once.
        struct Partial {
            std::vector<TemplateParam> params;
            // One written argument. A type argument is a pattern; a non-type
            // one is either a value or one of this specialization's own
            // parameters, which is the only shape of it that deduces.
            struct Arg {
                bool isType = true;
                const Type *type = nullptr;
                bool isParam = false;
                std::size_t param = 0;
                long long value = 0;
                // `R...` written as the last argument of the pattern, where R
                // is this specialization's own pack parameter. It takes every
                // argument the fixed ones in front of it did not, which is
                // what lets `L<T, R...>` peel one type off a list of any
                // length - the shape a recursive variadic class is made of.
                bool isPackExpansion = false;
            };
            std::vector<Arg> args;
            std::size_t bodyAt = 0;      // the '{' of its class body
            std::size_t pos = 0;
        };
        std::vector<Partial> partials;
    };
    std::map<std::string, TemplateDecl> templates_;

    // ---- Rung 5.2: function templates, explicit arguments ----
    //
    // One specialization that has been asked for. The body is not written
    // where the call is - a call is in the middle of another function - so
    // the request is recorded and the definitions are replayed afterwards, to
    // a fixed point, the way the implicit special members already are.
    struct Specialization {
        std::string key;                    // "twice<int>", and the table key
        std::string name;                   // "twice"
        std::string symbol;
        const Type *fn = nullptr;           // the substituted signature
        std::vector<const Type *> binding;  // by parameter index
        std::vector<long long> values;      // non-type arguments, same index
        std::vector<std::vector<const Type *> > packs;   // same index again
        std::size_t start = 0;              // the template's own tokens
        std::size_t pos = 0;                // where it was first asked for
        bool emitted = false;
        // A class specialization instead of a function one. Its member
        // functions are held bodies, replayed with the same fixed-point pass
        // and for the same reason: instantiating a class happens in the
        // middle of whatever asked for it, and replaying a body there would
        // walk over the enclosing function's own state.
        bool isClass = false;
        // Written out rather than made, so there are no parameters to bind
        // and no primary template to replay.
        bool explicitly = false;
        // The parameters `binding` and `values` are for. Usually the
        // template's own; for a partial specialization they are its.
        std::vector<TemplateParam> params;
        std::vector<PendingBody> bodies;
        // Out-of-line definitions already turned into keys for this tag. They
        // are replayed through topLevel and not through replayInlineBodies:
        // the tokens carry `Box<T>::get`, so the qualifier is written and the
        // ordinary member-definition path reads it.
        struct Outside {
            std::size_t start = 0;
            std::string key;
        };
        std::vector<Outside> outside;
        // Which of the class template's out-of-line definitions have been
        // replayed for this specialization. Not a count: the list can grow
        // after the class is made, since a definition may be written further
        // down the file than the first use.
        std::vector<bool> outsideDone;
    };
    std::vector<Specialization> specializations_;
    // Set while a specialization's definition is being replayed: the name the
    // function must be given, and the symbol it must carry. One-shot, taken
    // by the declarator that names the function.
    std::string instantiationKey_;
    std::string instantiationOf_;
    // A `>` inside a template argument list closes it and is not an operator
    // - [temp.names], and the reason a comparison there must be parenthesised.
    // Cleared inside parentheses, where a `>` is an operator again.
    bool inTemplateArgs_ = false;
    const std::string &instantiationName(const std::string &name) const {
        return (!instantiationKey_.empty() && name == instantiationOf_)
                   ? instantiationKey_ : name;
    }
    ExprPtr templateCall(Program *program);
    // `Box<int, 3>` where a type was expected. Answers the class, made if the
    // arguments have not been seen before.
    const Type *instantiateClass(const TemplateDecl &decl, std::size_t pos);
    // Set while a class template is being instantiated: the tag the class
    // must take, and the sign to hold its member bodies rather than replay
    // them. One-shot, taken by structOrUnionSpecifier.
    std::string classInstantiationTag_;
    // The template's own name, for the explicit-specialization path, where
    // `struct Box<int>` has already been read by the time the class body is
    // parsed and there is no identifier left for structOrUnionSpecifier to
    // take it from.
    std::string classInstantiationOf_;
    // **Held bodies are deferred only for an implicit instantiation.** That
    // happens in the middle of whatever asked for the class; an explicit
    // specialization is a definition at file scope like any other, so its
    // bodies are replayed where they are written.
    bool deferSpecializationBodies_ = false;
    // `template <> struct Box<int> { ... };` - a class written out for one
    // argument list instead of made from the template.
    bool explicitSpecialization();
    // The arguments that tag was built from, and the bodies the class came
    // back with. Both are handed between instantiateClass and the one call to
    // structOrUnionSpecifier it makes, which is why they are fields rather
    // than parameters: everything in between is the ordinary class path.
    std::vector<TemplateArg> instantiatingArgs_;
    std::vector<PendingBody> heldForSpecialization_;
    // Set while a template's declaration is being read as a *pattern* - with
    // Kind::TemplateParam bound in place of the arguments, for the Itanium
    // mangler and for deduction. A class template met there must not be
    // instantiated: `Holder<T>` has a member of type T, which has no size.
    // It answers a shallow type instead, carrying the name and the arguments
    // and nothing else, which is all either caller reads.
    bool patternOnly_ = false;

    // **A trial, and everything it has to put back.** Forming a candidate's
    // signature reads tokens, pushes classes and binds parameters; if it
    // fails half way through, none of that may be left behind for the next
    // candidate to trip over.
    struct Trial {
        Parser *p;
        std::size_t at;
        std::size_t classes;
        bool pattern;
        explicit Trial(Parser *parser);
        ~Trial();
    };
    const Signature &instantiate(const TemplateDecl &decl,
                                 const std::vector<const Type *> &binding,
                                 const std::vector<long long> &values,
                                 const std::vector<TemplateArg> &args,
                                 std::size_t pos,
                                 const std::vector<std::vector<const Type *> > &packs =
                                     std::vector<std::vector<const Type *> >());
    void instantiatePending();
    bool memberIsUsed(const std::string &key) const;
    // "twice<int>" - the table key and what the diagnostics call it.
    std::string specializationKey(const std::string &name,
                                  const std::vector<TemplateArg> &args) const;
    // The template's declaration re-read with `binding` in force. Answers its
    // function type and, through `name`, the name it declares.
    const Type *readTemplateDeclaration(const TemplateDecl &decl,
                                        const std::vector<const Type *> &binding,
                                        const std::vector<long long> &values,
                                        std::string *name,
                                        std::string *qualifier = nullptr,
                                        const std::vector<std::vector<const Type *> > *packs = nullptr);
    // What a binding does to the two name tables, and how to put them back.
    struct Shadow {
        std::string name;
        bool isType = true;
        bool isPack = false;
        bool had = false;
        std::size_t was = 0;
        std::vector<const Type *> hadPack;
        std::vector<std::string> hadNames;
    };
    // **A pack while a specialization is being read**: the types it was given
    // and, once its function parameters have been made, the names they were
    // given. `rest` becomes `rest$0`, `rest$1`, and `rest...` in a call is
    // those two names - which is the whole of how an expansion works here.
    struct PackBinding {
        std::vector<const Type *> types;
        std::vector<std::string> names;
    };
    std::map<std::string, PackBinding> packs_;
    // `Ts... rest` in a parameter list. Answers false where the tokens are
    // not that; otherwise reads it and adds what it expands to.
    bool packParameter(std::vector<const Type *> *types,
                       std::vector<std::string> *names);
    // `packs` is indexed like the others and read only for a parameter that
    // is one; everything else ignores it.
    void bindTemplateParameters(const std::vector<TemplateParam> &params,
                                const std::vector<const Type *> &binding,
                                const std::vector<long long> &values,
                                const std::vector<std::vector<const Type *> > &packs,
                                std::vector<Shadow> *undo);
    // [temp.deduct.type] rather than [temp.deduct.call]: this matches a
    // template *argument* against a pattern, where nothing decays and nothing
    // is allowed to differ - a pattern that is a pointer matches a pointer
    // and nothing else. Deduction from a call is deliberately looser, because
    // a conversion may still get the argument to the parameter; here there is
    // no conversion to be had.
    bool matchPattern(const Type *pattern, const Type *arg,
                      std::vector<const Type *> *binding,
                      std::string *why) const;
    // The partial specialization these arguments ask for, or npos for none.
    // Refuses by name where two match and neither is more specialized.
    std::size_t choosePartial(const TemplateDecl &decl,
                              const std::vector<TemplateArg> &args,
                              std::vector<const Type *> *binding,
                              std::vector<long long> *values,
                              std::vector<std::vector<const Type *> > *packs,
                              std::size_t pos);
    // [temp.class.order]: whether one pattern is at least as specialized as
    // another, which is asked by matching each against the other.
    bool atLeastAsSpecialized(const TemplateDecl::Partial &a,
                              const TemplateDecl::Partial &b) const;
    bool moreSpecialized(const TemplateDecl::Partial &a,
                         const TemplateDecl::Partial &b) const;
    // `Box<T *>` after `template <class T> struct` - the argument list of a
    // partial specialization, read as patterns.
    void partialArguments(TemplateDecl::Partial *ps,
                          const std::vector<TemplateParam> &primary);
    void unbindTemplateParameters(const std::vector<Shadow> &undo);
    // Deduction: the arguments worked out from the call rather than written.
    // Answers false and fills `why` when some parameter cannot be deduced.
    bool deduceTemplateArguments(const TemplateDecl &decl,
                                 const std::vector<ExprPtr> &args,
                                 std::vector<const Type *> *binding,
                                 std::vector<std::vector<const Type *> > *packs,
                                 std::string *why);
    bool deduceOne(const Type *pattern, const Type *arg,
                   std::vector<const Type *> *binding, std::string *why) const;
    // An argument's type as a parameter sees it: an array or function becomes
    // a pointer and the top-level qualifier goes, which is [temp.deduct.call]
    // and also just what passing something does.
    const Type *decayedType(const Type *a) const;
    // ---- Rung 7.1: `auto` ----
    //
    // [dcl.spec.auto] deduces a variable's `auto` **as if by template
    // argument deduction from a function call**, which is not an analogy to
    // borrow but the rule itself - so deduceOne does the work, decay and all,
    // and Kind::Deduced stands where Kind::TemplateParam would.
    static bool mentionsDeduced(const Type *t);
    const Type *substituteDeduced(const Type *t, const Type *with);
    // Reads the initialiser to learn its type, puts the tokens back, and
    // answers the declared type with `auto` replaced.
    const Type *deduceAuto(const Type *declared, const std::string &name,
                           std::size_t pos);
    // The same deduction against a type already in hand, for a `for` whose
    // initialiser is built rather than written.
    const Type *deduceAutoFrom(const Type *declared, const Type *from,
                               const std::string &name, std::size_t pos);
    // ---- Rung 7.3: the range-based `for` ----
    //
    // A declaration followed by `:` rather than `;`, which takes a scan to
    // tell from `for (int x = a ? b : c; ...)` - the `?` has to be counted.
    // ---- Rung 7.2: `decltype` ----
    //
    // [dcl.type.simple]: an unparenthesised name answers what that entity was
    // *declared* as; anything else answers the expression's type, with a `&`
    // added when it is an lvalue. That is the whole of the difference between
    // `decltype(x)` and `decltype((x))`, and it is why the shape of the
    // tokens has to be looked at and not only the expression they build.
    const Type *decltypeSpecifier();
    bool atNamePath() const;
    bool atRangeFor() const;
    StmtPtr rangeForStatement(int scope);
    // The argument list at a use: `<int, 3>` read into types and values.
    void templateArguments(const TemplateDecl &decl,
                           std::vector<const Type *> *binding,
                           std::vector<long long> *values,
                           std::vector<TemplateArg> *args,
                           std::vector<std::vector<const Type *> > *packs = nullptr);
    bool isTemplateName(const std::string &name) const {
        return templates_.find(name) != templates_.end();
    }
    bool templateDeclaration();
    void templateParameters(std::vector<TemplateParam> &params);
    // What the declaration after `template <...>` actually declares: a class
    // template, a function template, or a member of a class template defined
    // outside it. `qualifier` is the class for that last one and empty
    // otherwise.
    std::string templatedName(const std::vector<TemplateParam> &params,
                              bool *isClass, std::string *qualifier);
    bool skipTemplatedDefinition();
    void skipTemplateArguments();
    // `Box<T>::Box(` or `Box<T>::~Box(` - a constructor or destructor written
    // outside its class template.
    bool atOutOfLineSpecial(std::string *what);
    // The whole of what a use of a template does in 5.1: step over the
    // argument list so that the reader is told about the template rather than
    // about a stray '<', and then refuse by name.
    void refuseTemplateId();

    // **A `>>` closes two argument lists, and the lexer hands it over as one
    // token.** The parser cannot split it by inserting a second one: held
    // bodies and templates record absolute token indices, and an insert would
    // move every one of them. So the first `>` is taken by leaving this index
    // behind instead of advancing, and the second by advancing past the
    // token. Tying it to the index rather than to a flag is what lets a
    // replay that reaches the same `>>` again start over.
    std::size_t angleSplit_ = static_cast<std::size_t>(-1);
    bool atClosingAngle() const;
    void takeClosingAngle();

    // Set only while an inline body is being replayed. It makes an unqualified
    // name in a declarator mean a member of that class - and it is one-shot,
    // cleared by the first declarator that uses it, so nothing inside the body
    // picks it up.
    std::string inlineOwner_;
    // The same class as the source spells it. Equal to inlineOwner_ for an
    // ordinary class and the template's own name for a specialization.
    std::string inlineOwnerName_;

    void emitVtable(const Type *cls, const std::string &tag, std::size_t pos);
    // The statements that set an object's vptrs - the primary one and any a
    // polymorphic second base needs. Every constructor emits these, written
    // or implicit, which is why they live in one place.
    std::vector<StmtPtr> storeVptrs(const std::string &cls, const Type *memberOf,
                                    int thisSlot);
    // A thunk: the entry a secondary table holds for a function this class
    // overrides. It moves `this` back to the complete object and calls the
    // real one. Named for the offset it undoes - _ZThn16_N1C1gEv.
    std::string synthesizeThunk(const std::string &cls, const Type *type,
                                const VSlot &slot, int offset, std::size_t pos);

    // How well one argument matches one parameter, in the order
    // [over.ics.scs] ranks them. The values are compared, so the order of
    // this enum is what the resolution means - do not reorder it.
    // **Identity and Qualification are both "Exact Match" in [over.ics.scs],
    // and they are still not equal.** [over.ics.rank]/3.2.1 ranks a sequence
    // above another that has it as a proper subsequence, and the identity
    // conversion is a subsequence of a qualification conversion - which is why
    // f(char *) beats f(const char *) for a `char *` argument rather than
    // tying with it. Collapsing the two makes that call ambiguous, which is
    // what happened here before they were split.
    enum class Rank { Identity, Qualification, Promotion, Conversion, Ellipsis, None };
    bool betterCandidate(const std::vector<Rank> &a, const std::vector<Rank> &b,
                         const Signature &fa, const Signature &fb) const;

    struct Declared {
        std::string name;
        const Type *type;
        std::size_t pos;

        std::size_t paramsAt = 0;
        // The class named before a `::` in a definition's declarator -
        // `int Point::get()` declares nothing called "Point::get", it defines
        // the `get` that Point already declared.
        std::string qualifier;
    };

    enum StorageClass { StorageNone, StorageStatic, StorageExtern, StorageTypedef,
                        StorageRegister, StorageAuto };

    struct Qualifiers {
        bool isConst = false;
        bool isVolatile = false;
        // `constexpr` implies const on an object, so isConst is set with it
        // and almost everything downstream needs to know nothing more. What
        // this adds is the *demand*: a constexpr object whose initialiser is
        // not a constant expression is an error, where a const one is simply
        // an ordinary variable.
        bool isConstexpr = false;
    };

    struct TypedefName {
        std::string name;
        const Type *type;
    };

    struct EnumConst {
        std::string name;
        long long value;
    };

    const Source &src_;
    std::vector<Token> tokens_;
    TypeTable &types_;
    const Target &target_;

    int structReturnLimit_;

    bool aggregatesByReference_;

    bool homogeneousFloatAggregates_;
    // **How a class goes to a function, and the two ABIs part company here.**
    // Itanium passes one by address whenever copying or destroying it is a
    // call - so a class with only a destructor goes by address too, and the
    // caller destroys the copy it made. Microsoft passes that same class by
    // the ordinary size rules, in a register if it fits, and has the *callee*
    // destroy its own parameter. Measured with cl and with clang.
    bool passedByAddress(const Type *t) const {
        if (!t->isStructOrUnion()) return false;
        if (t->nonTrivialCopy()) return true;
        return !target_.microsoftNames() && t->hasDestructor();
    }
    bool returnsIndirectly(const Type *t) const {
        int size = t->size(target_);

        // A class whose copy or destruction is a call is returned through a
        // hidden pointer whatever its size - the caller owns the storage and
        // the callee builds into it. Both ABIs agree here, measured.
        if (t->nonTrivialCopy() || t->hasDestructor()) return true;
        if (containsX87(t, target_)) return true;
        if (aggregatesByReference_)
            return !(size == 1 || size == 2 || size == 4 || size == 8);
        if (homogeneousFloatAggregates_) {
            Kind elem;
            if (homogeneousFloatCount(t, &elem) > 0) return false;
        }
        return size > structReturnLimit_;
    }
    bool variadicBody_ = false;

    std::size_t at_ = 0;

    std::vector<Local> locals_;

    std::vector<::Local> fnVars_;
    bool inParams_ = false;
    std::vector<std::size_t> scopeStarts_;
    std::vector<int> blocks_;
    std::vector<int> blockStack_;

    bool atFunctionBody_ = false;
    int frameSize_ = 0;
    const Type *returnType_ = nullptr;
    std::string functionName_;
    std::vector<std::string> staticSymbols_;

    std::vector<Signature> functions_;
    // One name, every function declared under it. C had one; C++ has a set,
    // and the set is ordered by declaration so a diagnostic can list the
    // candidates in the order the reader wrote them.
    std::vector<Signature> &functionTable() { return functions_; }
    std::unordered_map<std::string, std::vector<std::size_t> > functionIndex_;
    std::vector<GlobalSym> globals_;
    std::unordered_map<std::string, std::size_t> globalIndex_;
    std::vector<TypedefName> typedefs_;
    std::unordered_map<std::string, std::size_t> typedefIndex_;
    std::vector<EnumConst> enums_;
    std::unordered_map<std::string, std::size_t> enumIndex_;
    int strings_ = 0;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
    int caseIds_ = 0;

    struct SwitchCtx {
        std::vector<const Case *> cases;
        const Case *deflt;
        const Type *governing;
    };
    std::vector<SwitchCtx> switches_;

    struct LabelDef { std::string name; std::size_t pos; };
    std::vector<LabelDef> labels_;
    std::vector<LabelDef> gotos_;

    const Token &peek() const { return tokens_[at_]; }
    const Token &peekAt(std::size_t n) const;
    bool consume(const char *s);
    void expect(const char *s);
    std::string expectIdent(const char *what);
    long long expectNumber(const char *what);

    bool atTypeName() const;
    const Type *findTypedef(const std::string &name) const;
    void declareTypeName(const std::string &name, const Type *type);
    const EnumConst *findEnum(const std::string &name) const;
    const Type *memberTypeWalk(const Type *t);
    const Type *structOrUnionSpecifier(Kind kind, bool isClass = false);
    void checkAccessible(const Type *object, const Member &m, std::size_t pos) const;
    // A constructor is keyed in the function table under "Point::Point", so
    // resolving one is resolving an overload set like any other.
    // The last component of a qualified tag: "Outer::Inner" is "Inner", which
    // is what a constructor and a destructor are written as.
    static std::string localOf(const std::string &qualified) {
        const std::size_t at = qualified.rfind("::");
        return at == std::string::npos ? qualified : qualified.substr(at + 2);
    }
    static std::string constructorKey(const std::string &cls) {
        return cls + "::" + localOf(cls);
    }
    void declareConstructor(const std::string &cls, std::size_t pos, Access access,
                            bool isExplicit);
    void declareDestructor(const std::string &cls, std::size_t pos, Access access,
                           bool isVirtual);
    std::string deletingDestructorSymbol(const std::string &cls);
    void registerDestructor(const std::string &cls, std::size_t pos,
                            Access access, bool isVirtual, bool implicit);
    void declareImplicitDestructor(const std::string &tag, const Type *type,
                                   std::size_t pos);
    void synthesizeDestructor(std::size_t which);
    // The deleting destructor, which no program writes: it runs the
    // destructor and then frees. Itanium calls it D0 and Microsoft ??_G, and
    // it is what a `delete` through a base pointer reaches.
    void synthesizeDeleting(const std::string &cls, const Type *type,
                            Access access, std::size_t pos);
    static std::string destructorKey(const std::string &cls) {
        return cls + "::~" + localOf(cls);
    }
    // A constructor of this class taking nothing, written or implicit.
    const Signature *defaultConstructorOf(const Type *cls) const;
    // What the class did not write, the compiler declares. Called once the
    // class is complete, because whether an implicit member is trivial - and
    // so whether it is a function at all - is a question about the members.
    void declareImplicitSpecials(const std::string &tag, const Type *type,
                                 std::size_t pos);
    // Bodies for the implicit members something actually called, run at the
    // end of the file and to a fixed point: giving one class a body can be
    // what first calls another's.
    void defineImplicitFunctions();
    void synthesizeDefaultCtor(std::size_t which);
    // One body for both halves of the copy. They differ in three places -
    // which member function to call, whether the vptr is stored, and whether
    // there is a value to return - and in nothing else.
    void synthesizeCopy(std::size_t which, bool assigning);
    const Signature *copyAssignOf(const Type *cls) const;
    void declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                   std::size_t pos);
    static std::string assignmentKey(const std::string &cls) {
        return cls + "::operator=";
    }
    void declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                 std::size_t pos);
    // A constructor of this class taking one reference to it, written or
    // implicit - which is what [class.copy] calls a copy constructor.
    const Signature *copyConstructorOf(const Type *cls) const;
    const Signature *moveConstructorOf(const Type *cls) const;
    void declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                 std::size_t pos, bool userDeclared);
    // `int i = 0; while (i < count) { one; i = i + 1; }` over an array
    // member's elements. A loop rather than `count` copies of the statement,
    // because the count is a property of the type and nothing bounds it.
    StmtPtr eachElement(int indexSlot, long long count, StmtPtr one);
    // The name a base subobject's constructor is called by: Itanium's C2
    // rather than the C1 the signature carries, and on Windows the one name
    // there is.
    std::string baseConstructorSymbol(const Signature &ctor, const Type *base);
    const Signature *destructorOf(const Type *cls) const;
    ExprPtr destructorCall(ExprPtr address, const Signature &dtor, std::size_t pos);

    // Objects that have been constructed and not yet destroyed, innermost
    // last. RAII is this list read backwards at the right moments.
    struct Alive {
        std::string name;
        int offset;
        const Type *cls;
        // Set for a by-value class parameter that arrived by address: its slot
        // holds the caller's pointer, so the object's address is what the slot
        // *contains* rather than where the slot sits.
        bool byAddress = false;
    };
    std::vector<Alive> alive_;
    // `except` is the frame offset of an object not to destroy - the one
    // being returned, which the caller destroys instead.
    // A cleanup region's landing pad: destroy alive_[from..to), last first,
    // and hand the exception back to the unwinder.
    StmtPtr cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                       std::size_t pos);
    // The block's statements with each stretch that has objects alive turned
    // into a cleanup region of its own.
    std::vector<StmtPtr> wrapMsCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos);
    std::vector<StmtPtr> wrapCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos);
    // `to` bounds the top of the range, for a cleanup pad that must destroy
    // only what existed at its point in the block.
    void emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                         std::size_t pos, int except = -1,
                         std::size_t to = static_cast<std::size_t>(-1));
    StmtPtr constructLocal(const Declared &d, int offset,
                           std::vector<ExprPtr> args, bool copyInit = false);

    // A static data member: declared inside the class, defined outside it,
    // and reached by all three of `C::n`, `obj.n` and `p->n`.
    std::string staticMemberSymbol(const std::string &cls, const std::string &name,
                                   const Type *t, Access access, std::size_t pos);
    void declareStaticMember(const std::string &cls, Type *owner,
                             const Declared &d, Access access);
    void defineStaticMember(Declared &d, Program &program);
    ExprPtr staticMemberRef(const Type *owner, const Type::StaticMember &s,
                            const std::string &cls, std::size_t pos);
    void declareMember(const std::string &cls, const Declared &d, bool constThis,
                       Access access, bool inUnion, bool isVirtual);
    std::string memberSymbol(const std::string &cls, const std::string &name,
                             const Type *fn, Access access, bool constThis,
                             std::size_t pos, bool isVirtual = false);
    // `S a[4];` - the default constructor once per element. Separate from
    // constructLocal because that one builds a single object and names the
    // class through d.type, which for an array is the array.
    StmtPtr constructLocalArray(const Declared &d, int offset, int indexSlot);
    ExprPtr memberCall(ExprPtr object, const Type *cls, const std::string &name,
                       std::size_t pos);
    ExprPtr memberCallWith(ExprPtr object, const Type *cls,
                           const std::string &name, std::size_t pos,
                           std::vector<ExprPtr> args);
    // The class up the chain that declares this member function, searching
    // every base rather than only the first.
    const Type *findMemberOwner(const Type *cls, const std::string &name) const;

    // The class whose member function is being parsed, and the frame offset of
    // its hidden `this` parameter. Empty and 0 outside one, which is what
    // makes "is this inside the class" a question with an answer.
    // The classes whose bodies are being parsed, outermost first. It is what
    // makes an unqualified name inside a class find that class's own nested
    // types, and what gives a nested class its qualified tag.
    std::vector<const Type *> classStack_;
    const Type *lookupInClass(const Type *cls, const std::string &name) const;
    // Whether the class being parsed, or the one whose member function is
    // being parsed, is this class or something derived from it.
    bool insideClass(const Type *cls) const;
    // `Point::Point(` and `Outer::Inner::~Inner(` have no type before the
    // name and the name IS a type, so the specifier list has to decline them.
    bool atUntypedMemberDefinition() const;
    const Type *currentClass_ = nullptr;
    int thisOffset_ = 0;
    const Type *enumSpecifier();
    bool atDeclarationStart() const;
    const Type *specifiers(StorageClass *storage, Qualifiers *quals = nullptr);
    const Type *unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals);

    Declared declarator(const Type *base, bool nameOptional = false,
                        bool insideParens = false);
    // `operator+` where a declarator wants a name. The name a declaration
    // carries is the whole of it - "operator+", punctuation and all - so that
    // every table here keys it exactly as it keys `get`, and overloading,
    // access and mangling needed to learn nothing about operators to hold
    // one. `declaredName` is `expectIdent` with that case in front of it, and
    // stands wherever a declarator reads a name.
    // `[](int a) { return a * 2; }` - rung 7.6.
    // `P(1)` - a temporary of class type, built into a slot of this frame.
    // The gap this closes was reachable three ways: as an expression, as
    // `return P(1);`, and as `static_cast<T &&>` of a prvalue.
    ExprPtr classTemporary(const Type *cls, std::size_t pos);
    ExprPtr lambdaExpression();
    // [expr.prim.lambda]/4 with no trailing return type: a body that is one
    // `return expression;` has that expression's type, anything else is void.
    // The expression is read with the parameters in scope and then put back -
    // the same read-it-twice 7.1 does for `auto`, and for the same reason:
    // threading the type out of a body parsed once would mean threading it
    // through every path the body can take.
    const Type *deduceLambdaReturn(std::size_t paramsFrom, std::size_t paramsTo,
                                   std::size_t bodyFrom, std::size_t bodyTo,
                                   const std::vector<std::string> &capNames,
                                   const std::vector<const Type *> &capTypes);
    int lambdaCount_ = 0;
    // The hidden typedef that spells a closure's return type needs a name no
    // other lambda will take, so this one never resets where lambdaCount_ does
    // - two functions each numbering their closures from zero would otherwise
    // ask for `$lret1` twice and get "typedefed twice, and not to the same
    // type" from the second.
    int lambdaRetSeq_ = 0;
    // The class a closure captured `this` from, by closure tag. Inside the
    // call operator, `currentClass_` is the *closure* - so an unqualified name
    // that belongs to the enclosing class, and the word `this` itself, are
    // found through this and reached through the captured pointer.
    std::map<std::string, const Type *> closureOuter_;
    // `$this` - the member a `[this]` capture holds. Not a name any program
    // can write, which is the point: it must not collide with a capture.
    static const char *capturedThis() { return "$this"; }
    // Inside a closure that captured `this`, the pointer it holds - built as
    // `this->$this`, `this` being the closure. Null anywhere else.
    ExprPtr capturedThisPointer();
    // Does the code being parsed have a member's-eye view of this class?
    // Inside a lambda that is the class it was *written in*, not the closure -
    // [expr.prim.lambda]/7 gives the call operator the context's access - and
    // both access checks have to ask the same question or they disagree.
    bool insideAccessOf(const Type *cls) const;
    // A name that is a *capture of the lambda around this one*: by the time an
    // inner lambda is read, the outer one's capture is a member of the outer
    // closure and not a local at all. Answers the expression that reads it -
    // `this->name`, `this` being the outer closure - or null when the name is
    // not that. Asked where a capture is looked up and again where the closure
    // object is built, which must agree.
    ExprPtr outerCaptureAccess(const std::string &name);
    // **One closure type per lambda written, however often it is read.** 7.1
    // reads an `auto` initialiser twice - once to learn the type, once to
    // build it - so `auto f = [](int a){...};` reached here twice and made two
    // classes, and the declaration then refused its own initialiser: "'f' is
    // 'struct main::$_0' and this is 'struct main::$_1'". Keyed by the token
    // the '[' sits at, which is the same on every reading.
    struct MadeLambda {
        const Type *type;
        std::size_t end;
        // The captures have to be copied in on *every* reading, not only the
        // one that built the class - the second reading was handing back an
        // uninitialised closure and the lambda saw whatever was on the stack.
        std::vector<std::string> names;
        std::vector<const Type *> types;
        std::vector<int> offsets;
    };
    // One closure object: a frame slot, and each capture copied into it.
    ExprPtr buildClosure(const MadeLambda &made, std::size_t pos);
    std::map<std::size_t, MadeLambda> lambdaAt_;

    std::string operatorName();
    std::string declaredName(const char *what);

    // [class.friend]. **A friend is not a member**: the declaration is
    // written inside the class and the function it declares belongs to the
    // enclosing namespace, so all the class gives it is access. That is why
    // this is a table beside the class rather than anything in it.
    //
    // Keyed by the class's tag, holding the *linkage names* of the functions
    // it has befriended - the name alone would grant access to every overload
    // of it, including ones declared later that were never offered any.
    std::map<std::string, std::vector<std::string> > friends_;

    // The linkage name of the function whose body is being read, which is the
    // whole of what an access check needs to ask about friendship. Empty
    // outside a definition, and outside a *free* function's definition: a
    // member cannot be granted friendship here, because the qualified form
    // that would grant it is refused by name.
    std::string currentFunction_;
    // The same function's *source* name, which is what a local class is
    // qualified by so that a diagnostic can say `struct f::L` rather than
    // spelling a mangled symbol at the reader.
    std::string currentFunctionName_;

    bool isFriendOf(const Type *cls) const;

    // [class.local]. A class defined in a function body belongs to that
    // function: two functions may each define `struct L` and they are
    // different types, and neither name is visible outside its own function.
    // Both facts come from these two tables rather than from anything on the
    // Type - the tag is qualified for uniqueness, and the written name is
    // resolved through a scope that is emptied when the function ends.
    std::map<std::string, const Type *> localTypes_;   // written name -> type
    std::map<std::string, std::string> localClassOwner_;  // tag -> enclosing symbol
    // The enclosing function's linkage name, for a class that is local to
    // one; empty for every other class. Both ABIs spell a local class's
    // member functions by wrapping the enclosing function's whole name, so
    // this is what the mangler is missing without it.
    const std::string *localOwnerOf(const std::string &tag) const;
    // An operator this compiler can *name* but cannot yet reach from an
    // expression is refused where it is declared, not where it is written.
    // The arity is what decides it - `operator-` is two functions and only
    // one of them is missing - so this is asked once the parameters are
    // known, which is why it lives beside the declaration and not beside the
    // name.
    void checkOperatorDeclarable(const std::string &name, std::size_t params,
                                 bool member, std::size_t pos);
    const Type *arraySuffix(const Type *base, std::size_t pos);
    const Type *promote(const Type *t) const;
    const Type *usualArithmetic(const Type *a, const Type *b) const;
    ExprPtr convert(ExprPtr e, const Type *to) const;
    const Type *unsignedVersion(const Type *t) const;

    ExprPtr decay(ExprPtr e);
    void requireScalar(const Expr &e, std::size_t pos, const char *what);

    void checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                         const std::string &what) const;

    int declare(const std::string &name, const Type *type, std::size_t pos);
    int allocateFrameSlot(const Type *type);
    void declareStaticLocal(const std::string &name, const Type *type,
                            std::size_t pos, const std::string &symbol);
    void requireAssignable(const Expr &e, std::size_t pos, const char *what);
    const Local *findLocal(const std::string &name) const;
    void enterScope();
    void leaveScope();

    int enterBlock();
    void leaveBlock();
    int currentBlock() const { return blockStack_.empty() ? 0 : blockStack_.back(); }
    const GlobalSym *findGlobal(const std::string &name) const;
    GlobalSym *findGlobalToUpdate(const std::string &name);

    const Type *composite(const Type *a, const Type *b);
    void declareFunction(const std::string &name, const Type *returns,
                         const std::vector<const Type *> &params,
                         bool variadic, bool defining, std::size_t pos,
                         bool internal = false);
    ExprPtr defaultPromote(ExprPtr e);
    const Signature &lookupFunction(const std::string &name, std::size_t pos) const;
    const Signature *findFunction(const std::string &name) const;
    const std::vector<std::size_t> *overloadsOf(const std::string &name) const;
    const Signature &lookupSignature(const std::string &name,
                                     const std::vector<const Type *> &params,
                                     bool variadic, std::size_t pos) const;

    static std::string describeSignature(const Signature &f);
    const Type *decayedType(const Type *t);
    Rank rankArgument(const Expr &arg, const Type *param);
    // `object` is the type the call is made on for a member function, and
    // null for a free one. **A member function has an implicit object
    // parameter** - [over.match.funcs] - and it is ranked like any other,
    // which is the whole of what tells `get()` from `get() const`.
    const Signature &resolveOverload(const std::string &name,
                                     const std::vector<ExprPtr> &args,
                                     std::size_t pos,
                                     const Type *object = nullptr);

    ExprPtr newExpression(std::size_t pos);
    ExprPtr deleteExpression(std::size_t pos);
    // `try { ... } catch (T e) { ... }` - rung 6.3.
    StmtPtr tryStatement(std::size_t pos);
    // Set while a try's body is being read, so a try inside one is refused
    // rather than laid out as an overlapping range the table cannot hold.
    bool inTryBody_ = false;
    // Inside a Microsoft `catch` body, which is compiled as a funclet - a
    // function of its own. Leaving one is not a jump but a *return* of the
    // address to carry on at, and that address travels in the register a
    // return value would use, so a `return` inside a handler needs machinery
    // this does not have yet and is refused by name.
    bool inMsHandler_ = false;
    // Set when this function has a landing pad, so its prologue names the
    // personality routine and the table.
    bool functionHasPads_ = false;
    // **The selector is an index into the whole function's type table, not
    // into one try's handler list.** A second try in the same function
    // continues the numbering, and the parser has to know that because it is
    // what its comparisons are written against - the backend assigns the same
    // numbers in the same order, which is the order the tries were read.
    int functionTypeIndex_ = 0;
    // Set where a function writes a `try`. A cleanup region is a call site
    // too, and a call-site table holds sorted ranges that do not overlap - so
    // one inside a try, or a try inside one, is refused until the table can
    // split a range rather than nest it.
    bool functionHasTry_ = false;
    // `throw x;` - rung 6.2. Answers the statement it lowers to.
    StmtPtr throwStatement(ExprPtr value, std::size_t pos);
    StmtPtr microsoftThrow(ExprPtr value, std::size_t pos);
    // A call to something in the runtime, named by its symbol and needing no
    // declaration - the same shape callAllocator has used for operator new.
    ExprPtr runtimeCall(const char *symbol, const Type *returns,
                        std::vector<ExprPtr> args);
    ExprPtr callAllocator(const char *itanium, const char *microsoft,
                          const Type *returns, ExprPtr arg, std::size_t pos);
    int newTemps_ = 0;

    void parseArguments(std::vector<ExprPtr> &args);
    // The copy the caller makes for a by-value class argument: a temporary in
    // the caller's frame, built by the copy constructor, whose address is what
    // the callee receives.
    ExprPtr materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                            const std::string &what,
                            std::vector<std::pair<int, const Type *> > &destroy);
    // Temporaries this full expression has made for by-value class arguments.
    // **They are destroyed at the end of the full expression and not when the
    // call they were made for returns** - [class.temporary], and it is
    // visible: `printf("%d", useD(d))` destroys the copy after the printf, not
    // between the two calls. Held here rather than wrapped around the call for
    // exactly that reason.
    std::vector<std::pair<int, const Type *> > pendingTemps_;
    ExprPtr endFullExpression(ExprPtr e);
    void flushTemporaries(std::vector<StmtPtr> &into);
    ExprPtr completeCall(const std::string &name, const std::string &symbol,
                         ExprPtr callee, const Type *returns,
                         const std::vector<const Type *> &params, bool variadic,
                         std::size_t pos, std::vector<ExprPtr> args);

    void parameterTypes(std::vector<const Type *> &params, bool &variadic);

    // **A default argument is kept as a place in the token stream, not as a
    // parsed expression.** [dcl.fct.default] evaluates it afresh at every call
    // that leaves the argument out, so one tree could not be handed to two
    // call sites anyway without a general clone this parser does not have -
    // and re-reading tokens is what it already does for a member function's
    // held body. `pendingDefaults_` carries them the short distance from the
    // parameter list to whichever declare() records the function, the way the
    // class-instantiation fields carry a tag; `defaultArgs_` keys them by the
    // linkage name, so a redeclaration cannot give the same function two sets.
    std::vector<std::size_t> pendingDefaults_;
    std::map<std::string, std::vector<std::size_t> > defaultArgs_;
    // Past one default argument: to the ',' or ')' that ends it, counting
    // brackets so that a call or a subscript inside it keeps its commas.
    void skipDefaultArgument();
    // **A member's own initialiser is kept as a place in the token stream**,
    // the same way a default argument is and for the same reason: it is
    // evaluated once per construction, in a constructor that may not have been
    // read yet. Keyed "Class::member".
    // **A namespace is a scope that qualifies a name, and nothing else here.**
    // Every table in this parser is already keyed by a qualified string - a
    // nested class is "Outer::Inner" - and both ABIs spell a namespace
    // component exactly as they spell a class one. So a namespace costs a
    // prefix on the way in and a search on the way out, and no new table.
    std::vector<std::string> namespaceStack_;
    // Namespaces named by a `using namespace` still open here, innermost last.
    std::vector<std::string> usingNamespaces_;
    // Every namespace ever opened, by full path. A qualified name has to be
    // told from a class's - `N::f` and `S::f` are written the same and mean
    // different lookups - and this is what tells them apart.
    std::set<std::string> namespaces_;
    std::string namespacePrefix() const {
        std::string out;
        for (std::size_t i = 0; i < namespaceStack_.size(); i++)
            out += namespaceStack_[i] + "::";
        return out;
    }
    // The qualified name to look a written one up under: the enclosing
    // namespaces from the innermost outwards, then whatever is open by a
    // `using namespace`, then the name itself. `exists` says which table to
    // ask, because a function and a variable are kept in different ones.
    std::string qualifyForLookup(const std::string &name,
                                 bool (Parser::*exists)(const std::string &) const) const;
    bool hasFunctionNamed(const std::string &key) const;
    bool hasGlobalNamed(const std::string &key) const;
    bool hasTypeNamed(const std::string &key) const;
    std::size_t qualifiedTypeEnd() const;
    std::vector<std::string> lookupKeys(const std::string &name,
                                        const Type *left,
                                        const Type *right) const;

    std::map<std::string, std::size_t> memberInit_;
    // Whether this class wrote an initialiser on any of its members, which is
    // what [dcl.init.aggr]/1 in C++11 makes the difference between an
    // aggregate and a class that is not one.
    bool hasMemberInitialiser(const std::string &tag) const;
    // To the ',' or ';' that ends one, counting brackets.
    void skipMemberInitialiser();
    // The statements a constructor of this class needs for the members it did
    // not initialise itself - [class.base.init]/9, where an initialiser the
    // class wrote stands in for a mem-initialiser nobody wrote.
    std::vector<StmtPtr> memberInitialisers(const std::string &tag,
                                            const Type *type, int thisSlot,
                                            const std::set<std::string> &already,
                                            std::size_t pos);
    // [dcl.fct.default]/4: the defaults have to be a suffix of the parameter
    // list. Asked by both parameter-list parsers - the one for declarations
    // and the one a definition has of its own - because a definition may
    // carry the defaults where the declaration did not.
    void requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                   std::size_t pos);
    // The lowest number of arguments a call may give this function.
    std::size_t leastArguments(const Signature &f) const;
    // Fill in what the call left out, by re-reading each default expression.
    void applyDefaults(const Signature &f, std::vector<ExprPtr> &args,
                       std::size_t pos);
    void blockFunctionDeclaration(const Declared &d);

    ExprPtr finishCall(const std::string &name, const std::string &symbol,
                       ExprPtr callee, const Type *returns,
                       const std::vector<const Type *> &params, bool variadic,
                       std::size_t pos);

    // How deep inside 'extern "C"' the parser currently is. Zero means C++
    // linkage, which is what a name has unless someone says otherwise.
    int cLinkage_ = 0;
    bool linkageSpecification();
    std::string functionSymbol(const std::string &name, const Type *returns,
                               const std::vector<const Type *> &params,
                               bool variadic, bool internal, std::size_t pos);
    std::string dataSymbol(const std::string &name, const Type *type,
                           bool isStatic, std::size_t pos);

    ExprPtr objectRef(const std::string &name);
    ExprPtr useReference(ExprPtr e);
    ExprPtr bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                          const std::string &what);
    int refTemps_ = 0;

    struct Init {
        bool isList = false;
        ExprPtr value;
        std::vector<Init> items;
        std::size_t pos = 0;
    };

    struct InitStep {
        const Member *member = nullptr;
        long long index = 0;
    };

    struct InitCursor {
        std::vector<Init> *items = nullptr;
        std::size_t at = 0;
        bool done() const { return items == nullptr || at >= items->size(); }
        Init &cur() const { return (*items)[at]; }
    };

    Init parseInitialiser();
    bool constantInitialiser(const Type *t, const Init &in, long long *out) const;

    // **A `constexpr` function, kept so that fold() can run it.**
    // [dcl.constexpr] in C++11 lets the body be one return statement and
    // nothing else - no loop, no local, no second statement - which is what
    // makes evaluating a call an expression fold rather than an interpreter
    // with a program counter. C++14 relaxed that and is out of scope here, so
    // the restriction is a gift: `value` is that one expression and running
    // the function is folding it with the parameters standing for arguments.
    //
    // The function is still compiled and still callable at run time. This is
    // an *extra* way to reach it, taken only where a constant is required.
    struct ConstexprFn {
        const Expr *value = nullptr;   // owned by the Function in the Program
        std::vector<int> slots;        // parameter frame slots, in order
        std::size_t pos = 0;
    };
    std::map<std::string, ConstexprFn> constexprFns_;   // by mangled symbol

    // One frame per call being folded, holding what each parameter slot is
    // worth. Mutable because fold() is const and answering a call means
    // remembering its arguments for as long as the body is being read.
    mutable std::vector<std::vector<std::pair<int, long long> > > constexprFrames_;

    const Expr *singleReturnValue(const Stmt &body) const;
    ExprPtr targetFor(const std::string &name, const std::vector<InitStep> &path);

    void initStore(const std::string &name, std::vector<InitStep> &path,
                   ExprPtr value, std::size_t pos, std::vector<StmtPtr> &out);
    void initZero(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, std::size_t pos, std::vector<StmtPtr> &out);
    void emitString(const std::string &name, std::vector<InitStep> &path,
                    const Type *type, const StrLit *s, std::size_t pos,
                    std::vector<StmtPtr> &out);
    void emitFill(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, InitCursor &c, std::vector<StmtPtr> &out);
    void emitAggregate(const std::string &name, std::vector<InitStep> &path,
                       const Type *type, InitCursor &c, std::size_t pos,
                       std::vector<StmtPtr> &out);
    void emitInit(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, Init &in, std::vector<StmtPtr> &out);

    void flattenScalar(const Type *type, Init &in, int base,
                       std::vector<GlobalPiece> &out);
    void flattenFill(const Type *type, InitCursor &c, int base,
                     std::vector<GlobalPiece> &out);
    void flattenAggregate(const Type *type, InitCursor &c, int base,
                          std::vector<GlobalPiece> &out);
    void flattenInit(const Type *type, Init &in, int base,
                     std::vector<GlobalPiece> &out);

    void skipInit(const Type *type, InitCursor &c);
    long long inferredLength(const Init &in, const Type *element, std::size_t pos);
    static const StrLit *stringInitialiser(const Init &in, const Type *type);

    void topLevel(Program &program);
    StmtPtr block();
    StmtPtr statement();
    StmtPtr statementBody();
    StmtPtr declarationBody();
    StmtPtr forStatement();
    StmtPtr switchStatement();
    StmtPtr caseLabel();
    StmtPtr gotoLabel();
    StmtPtr declaration();
    void resolveGotos();

    bool staticAssertion();
    bool exceptionSpecification();
    // Set by exceptionSpecification() at each place a parameter list can be
    // closed, read and cleared by whichever declare* call follows. The same
    // shape `pendingDefaults_` uses, and for the same reason: the four places
    // that parse the specification are not the places that build a Signature.
    bool pendingNoexcept_ = false;
    // How many potentially-throwing things the expression being parsed has
    // reached. `noexcept(e)` reads it; nothing else does.
    int mayThrow_ = 0;
    long long constantExpression(const char *what);
    bool fold(const Expr &e, long long *out, std::size_t pos) const;

    void typedefFunctionSuffix(Declared &td);

    bool foldAddress(const Expr &e, std::string *sym, long long *off) const;
    bool addressOfObject(const Expr &e, std::string *sym, long long *off) const;
    long long narrowTo(long long v, const Type *t) const;

    ExprPtr expr();
    ExprPtr assign();
    ExprPtr conditional();
    ExprPtr bitOr();
    ExprPtr bitXor();
    ExprPtr bitAnd();
    ExprPtr compound(BinOp op, ExprPtr target, ExprPtr value, std::size_t pos);
    ExprPtr incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos);
    ExprPtr clonePure(const Expr &e);
    ExprPtr cloneLvalue(const Expr &e, std::size_t pos);
    ExprPtr shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr relational();
    ExprPtr shift();
    ExprPtr add();
    ExprPtr mul();
    ExprPtr castExpr();
    // `a .* p` and `p ->* q` - [expr.mptr.oper], which sits between a cast and
    // a multiplication. What it builds is the object's address plus the
    // offset the member pointer holds, read as the member's type.
    ExprPtr memberPointerExpr();
    ExprPtr applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos);
    // `&S::f` - the ABI's pair, built into a slot of this frame.
    ExprPtr boundMemberPointer(const Type *cls, const Signature &f,
                               std::size_t pos);
    // **`o.*p` for a member *function* pointer has to carry two things to the
    // call** - the object's address and the code pointer - and there is no
    // expression that holds a pair. So `applyMemberPointer` leaves the address
    // here and answers with the code pointer, and the `(` in `postfix` picks
    // it up as the first argument. The window is one token wide: `(o.*p)` is
    // only ever written to be called, and anything else clears it and says so.
    ExprPtr boundThis_;
    const Type *boundFn_ = nullptr;
    std::size_t boundAt_ = 0;
    ExprPtr staticCast(std::size_t pos);
    ExprPtr constCast(std::size_t pos);
    ExprPtr reinterpretCast(std::size_t pos);
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary(Program *program);

    ExprPtr arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr comparison(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    // Null unless an operand has class type, in which case the call it built.
    // Takes its operands by reference because it leaves them alone when it
    // answers null, and the built-in path below still needs them.
    ExprPtr overloadedBinary(BinOp op, ExprPtr &lhs, ExprPtr &rhs,
                             std::size_t pos);

    // Which side of [over.match.oper] won. The standard builds ONE candidate
    // set out of a class's member operators and the non-member ones and ranks
    // them together, so "is there a member" is not the question - "which
    // candidate is best, of all of them" is, and an equally good pair from the
    // two halves is an ambiguity like any other.
    enum class OperatorChoice { None, Member, NonMember };
    // `right` is null for a unary operator, where the operand is `left` and
    // a member candidate writes no parameter at all.
    OperatorChoice resolveOperator(const std::string &name, const Expr &left,
                                   const Expr *right, std::size_t pos);
    // `-v`, `!v`, `*p`, `++v`. Null when the operand is not a class or the
    // class has no such operator, and the built-in path below is then reached
    // unchanged - which is what keeps `&obj` the ordinary address-of it has
    // always been for a class that declares no `operator&`.
    ExprPtr overloadedUnary(const char *spelling, ExprPtr &operand,
                            std::size_t pos);
    ExprPtr pointerAdd(ExprPtr p, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r, std::size_t pos);

    Program *current_ = nullptr;
};
