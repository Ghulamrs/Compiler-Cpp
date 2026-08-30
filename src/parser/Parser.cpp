// The parser: the pieces the rest of it is built on.
//
// Reading tokens, looking a name up as a type or an enumerator, deciding from
// the tokens ahead what kind of thing is being declared, and replaying the
// member bodies a class held back. Below those, scopes and the local and
// global symbol tables every other unit declares into.
//
// The parser is one class over nine files - Parser.cpp and the eight
// ParserXxx.cpp beside it. It was one file of nine and a half thousand lines
// until then, which built as a single translation unit and so had to be read
// and rebuilt as one too. The split is by subject and nothing moved between
// subjects; what little has to be seen across the seams is in ParserInternal.h,
// and the three definitions it declares are here.

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

int alignTo(int n, int a) { return (n + a - 1) / a * a; }

// Recognised by the lexer, with no rule in this parser yet. Naming the
// keyword is the whole point: without this the word reaches expression
// parsing as an unknown identifier and the error lands on whatever follows
// it, which is never where the reader is looking.
const char *notYetSupported(const std::string &word) {
    static const char *const pending[] = {
        "alignas", "alignof", "and", "and_eq", "asm",
        "bitand", "bitor", "catch", "char16_t", "char32_t", "compl",
        "const_cast", "dynamic_cast",
        "export", "friend", "inline", "mutable", "namespace",
        "noexcept", "not", "not_eq", "operator", "or",
        "or_eq", "reinterpret_cast",
        "template", "thread_local",
        "typeid", "using", "virtual",
        "xor", "xor_eq"
    };
    for (const char *k : pending)
        if (word == k) return k;
    return nullptr;
}

// **A null pointer constant**, which C++11 spells two ways: the integer
// literal 0, and `nullptr`. [conv.ptr]/1 - and everything that asks this
// question wants both, which is why `nullptr` needed no new call site here.
bool isNullConstant(const Expr &e) {
    if (e.type() != nullptr && e.type()->isNullPtr()) return true;
    const Num *n = dynamic_cast<const Num *>(&e);
    return n != nullptr && n->type()->isInteger() && n->value() == 0;
}

// Does this expression name an object at all - what [basic.lval] calls a
// glvalue? Both an lvalue and an xvalue do, and the difference between them
// is not asked here: reference binding wants an address and either will give
// one, so this is the question it asks.
bool isGlvalue(const Expr &e) {
    if (dynamic_cast<const Var *>(&e)) return true;
    if (dynamic_cast<const MemberAccess *>(&e)) return true;
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) return u->op() == '*';
    // **A comma has the value category of its right operand** - [expr.comma],
    // and it is C++'s rule rather than C's, where the result is always a
    // value. It matters here because `b.count` for a static member is built
    // as one: the object is evaluated and thrown away and what the expression
    // names is the shared object, which can be assigned to and have its
    // address taken like any other.
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return isGlvalue(c->right());
    return false;
}

// An object that something else may still be looking at. The same question as
// above minus the objects handed over by `static_cast<T &&>`, and that
// subtraction is the whole of what an rvalue reference is for: `T &&` refuses
// an lvalue and takes an xvalue, so that a move can empty out what it is
// given without anyone noticing.
bool isLvalue(const Expr &e) {
    return !e.isXvalue() && isGlvalue(e);
}

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
    if (peek().kind != TokenKind::Ident) {
        // A keyword standing where a name was wanted is a feature this parser
        // has not grown rather than a slip of the finger, and "expected a
        // name" points at the word without saying what is wrong with it.
        // `int operator+(int);` is the case this was written for.
        if (const char *pending = notYetSupported(peek().text))
            src_.fail(peek().pos, std::string("'") + pending +
                                  "' is not supported yet");
        src_.fail(peek().pos, std::string("expected ") + what);
    }
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

// A class's own nested types, and its bases'. The one table this parser has
// for type names is flat and keyed by the *qualified* name, so the scope is
// walked here rather than kept as a stack of tables.
const Type *Parser::lookupInClass(const Type *cls, const std::string &name) const {
    for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
        auto it = typedefIndex_.find(c->tag() + "::" + name);
        if (it != typedefIndex_.end()) return typedefs_[it->second].type;
        for (const Type::BaseSpec &b : c->bases())
            if (const Type *t = lookupInClass(b.type, name)) return t;
    }
    return nullptr;
}

bool Parser::insideClass(const Type *cls) const {
    for (std::size_t i = 0; i < classStack_.size(); i++)
        for (const Type *c = classStack_[i]; c != nullptr; c = c->enclosing())
            if (c == cls) return true;
    for (const Type *c = currentClass_; c != nullptr; c = c->enclosing())
        if (c == cls) return true;
    return false;
}

const std::string *Parser::localOwnerOf(const std::string &tag) const {
    auto it = localClassOwner_.find(tag);
    return it == localClassOwner_.end() ? nullptr : &it->second;
}

bool Parser::hasFunctionNamed(const std::string &key) const {
    return functionIndex_.find(key) != functionIndex_.end();
}

bool Parser::hasGlobalNamed(const std::string &key) const {
    return globalIndex_.find(key) != globalIndex_.end();
}

bool Parser::hasTypeNamed(const std::string &key) const {
    return typedefIndex_.find(key) != typedefIndex_.end();
}

// **Unqualified lookup inside a namespace, and it is a search and not a rule.**
// The enclosing namespaces are tried from the innermost outwards, then the ones
// a `using namespace` has opened, then the name as written - which is
// [basic.lookup.unqual] closely enough for a compiler with no argument-
// dependent lookup. The first that names anything wins, and the answer is a key
// every table here is already able to hold.
std::string Parser::qualifyForLookup(const std::string &name,
                                     bool (Parser::*exists)(const std::string &) const) const {
    if (name.find("::") != std::string::npos) return name;
    for (std::size_t i = namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += namespaceStack_[k] + "::";
        if ((this->*exists)(prefix + name)) return prefix + name;
    }
    for (std::size_t i = usingNamespaces_.size(); i-- > 0; ) {
        const std::string key = usingNamespaces_[i] + "::" + name;
        if ((this->*exists)(key)) return key;
    }
    return name;
}

const Type *Parser::findTypedef(const std::string &name) const {
    // **A class local to this function is found first, and that is what makes
    // it shadow a global of the same name** - [basic.scope.local], and the
    // reason the local scope is a table of its own rather than an entry in
    // the one every other type name lives in.
    auto local = localTypes_.find(name);
    if (local != localTypes_.end()) return local->second;

    auto it = typedefIndex_.find(name);
    if (it != typedefIndex_.end()) return typedefs_[it->second].type;

    // A class named without its namespace, from inside that namespace or from
    // one a `using namespace` has opened.
    {
        const std::string key = qualifyForLookup(name, &Parser::hasTypeNamed);
        if (key != name) {
            auto q = typedefIndex_.find(key);
            if (q != typedefIndex_.end()) return typedefs_[q->second].type;
        }
    }

    // **Not found by its own name, so look in the classes this is inside.**
    // Innermost first: a class body being parsed, then the class whose member
    // function's body this is. A nested class is only visible from inside
    // without its qualification, which is what these two answer.
    for (std::size_t i = classStack_.size(); i-- > 0; )
        if (const Type *t = lookupInClass(classStack_[i], name)) return t;
    if (currentClass_ != nullptr)
        if (const Type *t = lookupInClass(currentClass_, name)) return t;

    // **A held body is replayed at file scope, and its return type is read
    // before anything says which class it belongs to.** `Holder *self()`
    // inside `Holder` is the case: currentClass_ is set from the declarator's
    // qualifier, which has not been read yet. inlineOwner_ is the one thing
    // that knows, so it is asked here - and this is why the injected class
    // name works in a member's return type as well as in its parameters.
    if (!inlineOwner_.empty()) {
        auto it = typedefIndex_.find(inlineOwner_);
        if (it != typedefIndex_.end())
            if (const Type *t = lookupInClass(typedefs_[it->second].type, name))
                return t;
    }
    return nullptr;
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

// `Point::Point(`, `Outer::Inner::Inner(` and `Outer::Inner::~Inner(` - a
// member definition whose name is its class's own, and so has no type in
// front of it. Every level is asked, because the class may itself be nested.
bool Parser::atUntypedMemberDefinition() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return false;
    std::string q = peek().text;
    for (std::size_t k = 1; peekAt(k).is("::"); ) {
        std::size_t n = k + 1;
        const bool destructor = peekAt(n).is("~");
        if (destructor) n++;
        if (peekAt(n).kind != TokenKind::Ident) return false;
        const std::string component = peekAt(n).text;
        const Type *cls = findTypedef(q);
        if (cls != nullptr && cls->isStructOrUnion() &&
            cls->localName() == component && peekAt(n + 1).is("("))
            return true;
        if (destructor) return false;
        q += "::" + component;
        k = n + 1;
    }
    return false;
}

bool Parser::atTypeName() const {
    static const char *const t[] = { "void", "bool", "char", "short", "int",
                                     "long", "signed", "unsigned", "wchar_t",
                                     "float", "double",
                                     "struct", "union", "enum",
                                     "const", "volatile",
                                     // It says the next thing is a type, and
                                     // saying so is the whole of what it does.
                                     "typename",
                                     // And this one *is* a type, written as a
                                     // question about an expression.
                                     "decltype" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    if (peek().kind != TokenKind::Ident) return false;
    // `Box<int> b;` declares a variable, so the name has to answer yes here -
    // but only with a `<` after it, since the bare name is not a type.
    auto tmpl = templates_.find(peek().text);
    if (tmpl != templates_.end() && tmpl->second.isClass && peekAt(1).is("<"))
        return true;
    if (findTypedef(peek().text) != nullptr) return true;
    // `N::S` names a type as much as `Outer::Inner` does, and neither answers
    // to the leading name on its own. One of the two leading names is a
    // namespace and the other a class, and past that first token the walk is
    // the same one.
    return qualifiedTypeEnd() != 0;
}

// How many tokens of an `A::B::C` chain starting here reach a type, or 0 if
// none does. The longest prefix that names one wins, so `Outer::Inner::shared`
// stops at Inner and `N::M::S` runs to the end.
std::size_t Parser::qualifiedTypeEnd() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return 0;
    std::string q = peek().text;
    std::size_t typeEnd = 0;
    for (std::size_t k = 1; peekAt(k).is("::") &&
                            peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        q += "::" + peekAt(k + 1).text;
        if (findTypedef(q) != nullptr) typeEnd = k + 2;
    }
    return typeEnd;
}

bool Parser::atDeclarationStart() const {
    // **`Counter::total = 1;` is a statement, not a declaration**, even though
    // it starts with a name that names a type. A declaration whose type is
    // written `C::something` needs a nested class, which is refused by name,
    // so an identifier naming a class and followed by '::' is always the
    // start of an expression here - a static member being read or written.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        const Type *named = findTypedef(peek().text);
        // A namespace comes here for the same reason a class does: `N::v = 1;`
        // is a statement and `N::S s;` is a declaration, and only the walk
        // below tells them apart.
        if ((named != nullptr && named->isStructOrUnion()) ||
            namespaces_.find(peek().text) != namespaces_.end()) {
            // ...**unless the qualified name is itself a type**, which makes
            // it a declaration after all: `Outer::Inner x;` declares an x
            // where `Counter::total = 1;` assigns to a static member. Whether
            // the whole name reaches a type is the difference.
            // The name is a declaration only where it *stops* at a type.
            // `Outer::Inner x;` declares an x; `Outer::Inner::shared = 1;`
            // goes on past the type to a member, and is a statement.
            const std::size_t typeEnd = qualifiedTypeEnd();
            return typeEnd != 0 && !peekAt(typeEnd).is("::");
        }
    }

    // `constexpr` beside the storage classes, and it has to be here rather
    // than in atTypeName: it is a decl-specifier that names no type, so a
    // statement starting with it is a declaration and nothing else. Left out,
    // `constexpr int n = 4;` inside a function goes to expression parsing and
    // the reader is told an expression was expected, pointing at a keyword
    // that begins a perfectly good declaration.
    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef")
        || peek().is("constexpr");
}

// From the '{' to the '}' that closes it, counting depth. The body is not
// looked at here at all - only found and stepped over.
void Parser::skipBracedBlock() {
    // A constructor's body may be preceded by its mem-initializer list, so
    // what is skipped starts at the ':' and the '{' is found from there.
    while (!peek().is("{")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function has no body after all");
        at_++;
    }
    int depth = 0;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function's body is never closed");
        if (peek().is("{")) depth++;
        else if (peek().is("}")) { depth--; at_++; if (depth == 0) return; continue; }
        at_++;
    }
}

// Each held body re-read as though it had been written outside the class. The
// tokens are the ones already there - return type, name, parameters, body - so
// the whole ordinary definition path runs over them, and `inlineOwner_` is
// what supplies the `Class::` the source does not have.
void Parser::replayInlineBodies(std::vector<PendingBody> mine) {
    if (mine.empty()) return;
    const std::size_t resume = at_;
    // **A replay is a nested parse of a different function**, and this is in
    // the middle of whatever asked for the class - which may be a declaration
    // inside a function body. `topLevel` sets up the function it is reading
    // and clears what it finds, so anything belonging to the *enclosing*
    // function has to be put back afterwards. That is how a local class with
    // a member function works at all: the member's body is replayed here,
    // between the class being closed and the next statement, and without this
    // the class's own name would be gone by the time `L l;` is read.
    // Everything `topLevel` sets up per function and clears on the way in.
    // A local class's member body is replayed *between* the class being
    // closed and the next statement of the function that wrote it, so
    // without this the enclosing function loses its parameters and its
    // locals: `h(int k) { struct M { ... }; M m; m.z = k; }` was told `k` was
    // not declared, because reading M::get had emptied the table k lived in.
    // The frame size goes back too, or the enclosing function's later locals
    // are laid out on top of each other.
    const std::string outerFunction = currentFunction_;
    const std::string outerFunctionName = currentFunctionName_;
    const std::map<std::string, const Type *> outerLocalTypes = localTypes_;
    const std::vector<Local> outerLocals = locals_;
    const std::vector<::Local> outerFnVars = fnVars_;
    const std::vector<std::size_t> outerScopeStarts = scopeStarts_;
    const std::vector<int> outerBlocks = blocks_;
    const std::vector<int> outerBlockStack = blockStack_;
    const std::vector<LabelDef> outerLabels = labels_;
    const std::vector<LabelDef> outerGotos = gotos_;
    const int outerFrameSize = frameSize_;
    const int outerThisOffset = thisOffset_;
    const Type *outerClass = currentClass_;
    // **The return type belongs to the function being read, and the replay
    // reads a different one.** Local classes hid this: a member's body set it
    // to whatever that member returned, and the enclosing function's next
    // `return` happened to want the same type. A lambda made it visible -
    // `voidly` returns void, and the function that wrote it was then told its
    // own `return` was wrong.
    const Type *outerReturn = returnType_;
    const int outerLambdaCount = lambdaCount_;   // lambdaRetSeq_ never resets
    const std::string outerName = functionName_;
    const bool outerAtBody = atFunctionBody_;
    for (std::size_t i = 0; i < mine.size(); i++) {
        at_ = mine[i].start;
        inlineOwner_ = mine[i].tag;
        inlineOwnerName_ = mine[i].local.empty() ? mine[i].tag : mine[i].local;
        topLevel(*current_);
        inlineOwner_.clear();
        inlineOwnerName_.clear();
    }
    currentFunction_ = outerFunction;
    currentFunctionName_ = outerFunctionName;
    localTypes_ = outerLocalTypes;
    locals_ = outerLocals;
    fnVars_ = outerFnVars;
    scopeStarts_ = outerScopeStarts;
    blocks_ = outerBlocks;
    blockStack_ = outerBlockStack;
    labels_ = outerLabels;
    gotos_ = outerGotos;
    frameSize_ = outerFrameSize;
    thisOffset_ = outerThisOffset;
    currentClass_ = outerClass;
    returnType_ = outerReturn;
    lambdaCount_ = outerLambdaCount;
    functionName_ = outerName;
    atFunctionBody_ = outerAtBody;
    at_ = resume;
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
    if (it != globalIndex_.end()) return &globals_[it->second];
    // Not under the name as written: an unqualified one inside a namespace
    // names that namespace's variable if it has one.
    const std::string key = qualifyForLookup(name, &Parser::hasGlobalNamed);
    if (key == name) return nullptr;
    it = globalIndex_.find(key);
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
