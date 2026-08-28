#pragma once

#include "Ast.h"
#include "Lexer.h"
#include "Type.h"

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
    };

    struct GlobalSym {
        std::string name;
        std::string symbol;
        const Type *type;
        bool isConst = false;
        bool emitted = false;
        bool hasInit = false;
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
    };

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
    bool returnsIndirectly(const Type *t) const {
        int size = t->size(target_);

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
    const Type *structOrUnionSpecifier(Kind kind, bool isClass = false);
    void checkAccessible(const Type *object, const Member &m, std::size_t pos) const;
    void declareMember(const std::string &cls, const Declared &d, bool constThis,
                       Access access, bool inUnion);
    std::string memberSymbol(const std::string &cls, const std::string &name,
                             const Type *fn, Access access, bool constThis,
                             std::size_t pos);
    ExprPtr memberCall(ExprPtr object, const Type *cls, const std::string &name,
                       std::size_t pos);

    // The class whose member function is being parsed, and the frame offset of
    // its hidden `this` parameter. Empty and 0 outside one, which is what
    // makes "is this inside the class" a question with an answer.
    const Type *currentClass_ = nullptr;
    int thisOffset_ = 0;
    const Type *enumSpecifier();
    bool atDeclarationStart() const;
    const Type *specifiers(StorageClass *storage, Qualifiers *quals = nullptr);
    const Type *unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals);

    Declared declarator(const Type *base, bool nameOptional = false,
                        bool insideParens = false);
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
    const Signature &resolveOverload(const std::string &name,
                                     const std::vector<ExprPtr> &args,
                                     std::size_t pos);

    ExprPtr newExpression(std::size_t pos);
    ExprPtr deleteExpression(std::size_t pos);
    ExprPtr callAllocator(const char *itanium, const char *microsoft,
                          const Type *returns, ExprPtr arg, std::size_t pos);
    int newTemps_ = 0;

    void parseArguments(std::vector<ExprPtr> &args);
    ExprPtr completeCall(const std::string &name, const std::string &symbol,
                         ExprPtr callee, const Type *returns,
                         const std::vector<const Type *> &params, bool variadic,
                         std::size_t pos, std::vector<ExprPtr> args);

    void parameterTypes(std::vector<const Type *> &params, bool &variadic);
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
    ExprPtr shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs);
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr relational();
    ExprPtr shift();
    ExprPtr add();
    ExprPtr mul();
    ExprPtr castExpr();
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary(Program *program);

    ExprPtr arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr comparison(BinOp op, ExprPtr lhs, ExprPtr rhs);
    ExprPtr pointerAdd(ExprPtr p, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r, std::size_t pos);

    Program *current_ = nullptr;
};
