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

const Type *Parser::findTypedef(const std::string &name) const {
    auto it = typedefIndex_.find(name);
    if (it != typedefIndex_.end()) return typedefs_[it->second].type;

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
                                     "typename" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    if (peek().kind != TokenKind::Ident) return false;
    // `Box<int> b;` declares a variable, so the name has to answer yes here -
    // but only with a `<` after it, since the bare name is not a type.
    auto tmpl = templates_.find(peek().text);
    if (tmpl != templates_.end() && tmpl->second.isClass && peekAt(1).is("<"))
        return true;
    return findTypedef(peek().text) != nullptr;
}

bool Parser::atDeclarationStart() const {
    // **`Counter::total = 1;` is a statement, not a declaration**, even though
    // it starts with a name that names a type. A declaration whose type is
    // written `C::something` needs a nested class, which is refused by name,
    // so an identifier naming a class and followed by '::' is always the
    // start of an expression here - a static member being read or written.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        const Type *named = findTypedef(peek().text);
        if (named != nullptr && named->isStructOrUnion()) {
            // ...**unless the qualified name is itself a type**, which makes
            // it a declaration after all: `Outer::Inner x;` declares an x
            // where `Counter::total = 1;` assigns to a static member. Whether
            // the whole name reaches a type is the difference.
            // The name is a declaration only where it *stops* at a type.
            // `Outer::Inner x;` declares an x; `Outer::Inner::shared = 1;`
            // goes on past the type to a member, and is a statement.
            std::string q = peek().text;
            std::size_t typeEnd = 0;
            for (std::size_t k = 1; peekAt(k).is("::") &&
                                    peekAt(k + 1).kind == TokenKind::Ident;
                 k += 2) {
                q += "::" + peekAt(k + 1).text;
                if (findTypedef(q) != nullptr) typeEnd = k + 2;
            }
            return typeEnd != 0 && !peekAt(typeEnd).is("::");
        }
    }

    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef");
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
    for (std::size_t i = 0; i < mine.size(); i++) {
        at_ = mine[i].start;
        inlineOwner_ = mine[i].tag;
        inlineOwnerName_ = mine[i].local.empty() ? mine[i].tag : mine[i].local;
        topLevel(*current_);
        inlineOwner_.clear();
        inlineOwnerName_.clear();
    }
    at_ = resume;
}

// ------------------------------------------------------------------ templates
//
// **Rung 5.1: the table exists and nothing is instantiated.** That is the
// whole of it, and it is first because everything else stands on it:
// `f<int>(x)` and `a<b>(c)` are the same tokens, and the only thing that tells
// them apart is whether `f` names a template. So the name has to be in a table
// before any `<` is read, and a `<` opens an argument list *only* for a name
// that is in it - never on shape alone, which is the one mistake here that
// silently mis-parses code that used to work.

// A `>` may be the front half of a `>>`. See the note on angleSplit_.
bool Parser::atClosingAngle() const {
    return peek().is(">") || peek().is(">>");
}

void Parser::takeClosingAngle() {
    if (consume(">")) return;
    if (!peek().is(">>"))
        src_.fail(peek().pos, "expected '>' to close this template argument list");
    if (angleSplit_ == at_) {
        angleSplit_ = static_cast<std::size_t>(-1);
        at_++;
        return;
    }
    angleSplit_ = at_;
}

// `template < class T, int N >`. Everything C++11 puts here that this rung
// does not implement is refused by name rather than misread.
void Parser::templateParameters(std::vector<TemplateParam> &params) {
    expect("<");
    if (atClosingAngle())
        src_.fail(peek().pos, "'template <>' is an explicit specialization, "
                              "and that is not supported yet");
    for (;;) {
        const std::size_t pos = peek().pos;
        if (peek().is("template"))
            src_.fail(pos, "a template template parameter is not supported yet");

        TemplateParam p;
        p.pos = pos;
        if (consume("class") || consume("typename")) {
            if (consume("...")) p.isPack = true;
            // C++ lets a parameter go unnamed. Nothing here can refer to
            // one, so it is a not-yet rather than a rule.
            if (peek().kind != TokenKind::Ident)
                src_.fail(peek().pos, "an unnamed template parameter is not "
                                      "supported yet");
            p.name = peek().text;
            at_++;
        } else {
            // A non-type parameter is written exactly like a function's, so
            // it is read exactly like one.
            for (std::size_t k = 0; peekAt(k).kind != TokenKind::End; k++) {
                if (peekAt(k).is(",") || peekAt(k).is(">") || peekAt(k).is(">>"))
                    break;
                if (peekAt(k).is("...")) {
                    src_.fail(peekAt(k).pos, "a non-type parameter pack is not "
                                             "supported yet - a pack of types "
                                             "is");
                }
            }
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            if (sc != StorageNone)
                src_.fail(pos, "a template parameter has no storage class");
            Declared d = declarator(base);
            if (d.name.empty())
                src_.fail(pos, "an unnamed template parameter is not "
                               "supported yet");
            p.name = d.name;
            p.type = d.type;
        }
        if (peek().is("="))
            src_.fail(peek().pos, "a default template argument is not "
                                  "supported yet");
        for (std::size_t i = 0; i < params.size(); i++)
            if (params[i].name == p.name)
                src_.fail(p.pos, "'" + p.name + "' is declared twice in this "
                                 "template parameter list");
        // **Only the last parameter may be a pack.** A pack takes every
        // argument that is left, so anything written after one could never be
        // given a value.
        if (!params.empty() && params.back().isPack)
            src_.fail(p.pos, "'" + params.back().name + "' is a parameter pack "
                             "and takes every argument that is left, so '" +
                             p.name + "' after it could never be given one");
        params.push_back(p);
        if (consume(",")) continue;
        break;
    }
    takeClosingAngle();
}

// The name the template is being given, and nothing else about it.
//
// A class template's name is the identifier the `struct` or `class` keyword
// introduces, and it is read straight off: parsing the body instead would
// register a class that has no business existing until an argument list is
// given for it.
//
// **A function template's name sits behind a return type and a declarator
// that mention the parameters**, so `T` has to denote *something* before
// `T twice(T x)` can be read at all. It denotes `int` here - a stand-in that
// cannot escape, because the type this builds is thrown away and the body is
// skipped unparsed. 5.2 replaces it with the argument the template is given.
std::string Parser::templatedName(const std::vector<TemplateParam> &params,
                                  bool *isClass, std::string *qualifier) {
    qualifier->clear();
    if (peek().is("struct") || peek().is("class") || peek().is("union")) {
        if (peekAt(1).kind != TokenKind::Ident)
            src_.fail(peek().pos, "this class template has no name");
        *isClass = true;
        return peekAt(1).text;
    }
    *isClass = false;

    // **Read as a pattern, with the parameters standing for themselves.** The
    // stand-in used to be `int`, which was enough to find a name - but it
    // would instantiate `Box<int>` here, at a declaration that may be the
    // out-of-line definition of a member and asks for no class at all.
    TemplateDecl scratch;
    scratch.params = params;
    scratch.afterParams = at_;
    std::vector<const Type *> binding(params.size());
    std::vector<long long> values(params.size(), 1);
    for (std::size_t i = 0; i < params.size(); i++)
        binding[i] = params[i].type == nullptr
                         ? types_.templateParam(static_cast<int>(i))
                         : params[i].type;
    std::string name;
    readTemplateDeclaration(scratch, binding, values, &name, qualifier);
    if (name.empty())
        src_.fail(peek().pos, "this function template has no name");
    return name;
}

// **The parameters are bound to the argument list, and the tables are put
// back exactly as they were.** A type parameter becomes a type name and a
// non-type one an enumerator, which is what makes `T x` and `int a[N]` read
// with no second lookup path: the two things a template parameter can be are
// the two things this parser already knows how to look up.
void Parser::bindTemplateParameters(const std::vector<TemplateParam> &params,
                                    const std::vector<const Type *> &binding,
                                    const std::vector<long long> &values,
                                    const std::vector<std::vector<const Type *> > &packs,
                                    std::vector<Shadow> *undo) {
    for (std::size_t i = 0; i < params.size(); i++) {
        const TemplateParam &p = params[i];
        Shadow s;
        s.name = p.name;
        s.isType = p.type == nullptr;
        // **A pack is not a type name.** Nothing may write `Ts` on its own;
        // what reads it is `Ts...` and `sizeof...(Ts)`, and both want the
        // list rather than a type standing for it.
        if (p.isPack) {
            s.isPack = true;
            auto had = packs_.find(p.name);
            if (had != packs_.end()) {
                s.had = true;
                s.hadPack = had->second.types;
                s.hadNames = had->second.names;
            }
            PackBinding pb;
            if (i < binding.size() && binding[i] != nullptr &&
                binding[i]->kind() == Kind::TemplateParam)
                pb.types.push_back(binding[i]);       // reading a pattern
            else if (i < packs.size())
                pb.types = packs[i];
            packs_[p.name] = pb;
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = typedefIndex_.find(p.name);
            if (it != typedefIndex_.end()) { s.had = true; s.was = it->second; }
            typedefIndex_[p.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ p.name, binding[i] });
        } else {
            auto it = enumIndex_.find(p.name);
            if (it != enumIndex_.end()) { s.had = true; s.was = it->second; }
            enumIndex_[p.name] = enums_.size();
            enums_.push_back(EnumConst{ p.name, values[i] });
        }
        undo->push_back(s);
    }
}

void Parser::unbindTemplateParameters(const std::vector<Shadow> &undo) {
    for (std::size_t k = undo.size(); k-- > 0; ) {
        const Shadow &s = undo[k];
        if (s.isPack) {
            if (s.had) {
                packs_[s.name].types = s.hadPack;
                packs_[s.name].names = s.hadNames;
            } else {
                packs_.erase(s.name);
            }
            continue;
        }
        if (s.isType) {
            if (s.had) typedefIndex_[s.name] = s.was;
            else       typedefIndex_.erase(s.name);
        } else {
            if (s.had) enumIndex_[s.name] = s.was;
            else       enumIndex_.erase(s.name);
        }
    }
}

// The declaration read again with the arguments in force. Nothing is
// registered: this answers what the signature *is*, and the caller decides
// what to do with it.
const Type *Parser::readTemplateDeclaration(const TemplateDecl &decl,
                                            const std::vector<const Type *> &binding,
                                            const std::vector<long long> &values,
                                            std::string *name,
                                            std::string *qualifier,
                                            const std::vector<std::vector<const Type *> > *packs) {
    const std::size_t resume = at_;
    // **Put back even if this throws.** Forming a signature is what a trial
    // runs, and a failed one must leave the parameter names unbound for the
    // next candidate. The guard is the only thing standing between a
    // substitution failure and a table that still says T means int.
    struct Unbind {
        Parser *p;
        std::vector<Shadow> undo;
        ~Unbind() { p->unbindTemplateParameters(undo); }
    } guard{ this, std::vector<Shadow>() };
    bindTemplateParameters(decl.params, binding, values,
                           packs != nullptr
                               ? *packs
                               : std::vector<std::vector<const Type *> >(),
                           &guard.undo);
    const bool wasPattern = patternOnly_;
    for (std::size_t i = 0; i < binding.size(); i++)
        if (binding[i] != nullptr && binding[i]->kind() == Kind::TemplateParam)
            patternOnly_ = true;

    at_ = decl.afterParams;
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);

    // **The declarator records where the parameter list is and does not read
    // it**, which is how a definition gets to read the parameters once, with
    // their names. Here there is no definition to read them for, so they are
    // read for their types the way a prototype's are.
    //
    // The declarator's type is the *return* type at this point - the same
    // shape topLevel reads, where the function type is built once the
    // parameters have been.
    if (d.paramsAt != 0 || peek().is("(")) {
        if (d.paramsAt != 0) at_ = d.paramsAt;
        std::vector<const Type *> params;
        bool variadic = false;
        parameterTypes(params, variadic);
        d.type = types_.functionType(d.type, std::move(params), variadic);
    } else if (qualifier == nullptr) {
        src_.fail(d.pos, "'" + d.name + "' is a template and not a function, "
                         "and only function templates are supported yet");
    }

    patternOnly_ = wasPattern;
    at_ = resume;
    *name = d.name;
    if (qualifier != nullptr) *qualifier = d.qualifier;
    return d.type;
}

// From here to the `;` that ends the declaration, or to the `}` that closes
// the body. Nothing inside is looked at - that is what "no instantiation"
// means. Answers whether a body was there.
bool Parser::skipTemplatedDefinition() {
    bool body = false;
    int depth = 0;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template's definition is never closed");
        if (peek().is("{")) { depth++; body = true; at_++; continue; }
        if (peek().is("}")) {
            at_++;
            if (--depth == 0) { consume(";"); return body; }
            continue;
        }
        if (peek().is(";") && depth == 0) { at_++; return body; }
        at_++;
    }
}

// Defined below, beside the pattern matching it belongs with.
static bool mentionsParam(const Type *t, std::size_t i);

// `template <class T> ...` at file scope. Answers false where the token is
// something else, so topLevel can ask without committing.
bool Parser::templateDeclaration() {
    if (!peek().is("template")) return false;

    TemplateDecl decl;
    decl.start = at_;
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "explicit instantiation is not supported yet");
    if (peekAt(1).is(">")) return explicitSpecialization();
    templateParameters(decl.params);

    decl.afterParams = at_;
    decl.pos = peek().pos;

    // Its own step, and refused by name until then: the declarator reads a
    // class *name* before the `::`, and reading a template-id there is what
    // an out-of-line constructor needs. A member function is different - it
    // has a return type, so the qualifier is read by the declarator's
    // ordinary qualified path.
    std::string special;
    if (atOutOfLineSpecial(&special))
        src_.fail(decl.pos, "a " + special + " of a class template written "
                            "outside the class is not supported yet - write "
                            "it inside the class");

    // **`template <class T> struct Box<T *>` - a partial specialization.** It
    // is told from the primary by the `<` after the name: a class template
    // being *declared* has nothing there, and one already declared is being
    // specialized rather than redeclared.
    if ((peek().is("struct") || peek().is("class") || peek().is("union")) &&
        peekAt(1).kind == TokenKind::Ident && peekAt(2).is("<")) {
        auto primary = templates_.find(peekAt(1).text);
        if (primary == templates_.end() || !primary->second.isClass)
            src_.fail(peekAt(1).pos, "'" + peekAt(1).text + "' is not a class "
                                     "template, so there is nothing here to "
                                     "specialize");
        TemplateDecl::Partial ps;
        ps.params = decl.params;
        ps.pos = decl.pos;
        at_ += 2;
        partialArguments(&ps, primary->second.params.size());
        if (!peek().is("{"))
            src_.fail(peek().pos, "a partial specialization is a definition, "
                                  "and this one has no body");
        ps.bodyAt = at_;
        for (std::size_t i = 0; i < ps.params.size(); i++) {
            bool mentioned = false;
            for (std::size_t k = 0; k < ps.args.size(); k++)
                if (!ps.args[k].isType) {
                    if (ps.args[k].isParam && ps.args[k].param == i) mentioned = true;
                } else if (mentionsParam(ps.args[k].type, i)) {
                    mentioned = true;
                }
            if (!mentioned)
                src_.fail(ps.params[i].pos, "'" + ps.params[i].name + "' is "
                          "never used in this specialization's arguments, so "
                          "nothing could ever work it out");
        }
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        primary->second.partials.push_back(ps);
        return true;
    }

    std::string qualifier;
    decl.name = templatedName(decl.params, &decl.isClass, &qualifier);
    at_ = decl.afterParams;
    const bool defined = skipTemplatedDefinition();

    // **A member of a class template defined outside it belongs to the
    // class**, not to a template of its own. The declarator already reads a
    // qualified name for a nested class; all that is new is that the
    // qualifier is a template-id, and the class it names is the pattern.
    if (!qualifier.empty()) {
        const Type *of = findTypedef(qualifier);
        if (of == nullptr || !of->isSpecialization())
            src_.fail(decl.pos, "'" + qualifier + "' is not a class template, "
                                "so this defines a member of nothing");
        auto owner = templates_.find(of->templateName());
        if (owner == templates_.end())
            src_.fail(decl.pos, "'" + of->templateName() + "' is not a class "
                                "template");
        // The template's own name, not the qualifier: that is the pattern's
        // internal tag and holds a `$` no reader ever wrote.
        if (!defined)
            src_.fail(decl.pos, "'" + of->templateName() + "::" + decl.name +
                                "' is declared here and not defined - a member "
                                "is declared inside its class");
        TemplateDecl::OutOfLine ool;
        ool.start = decl.afterParams;
        ool.member = decl.name;
        ool.destructor = !decl.name.empty() && decl.name[0] == '~';
        owner->second.outOfLine.push_back(ool);
        return true;
    }

    decl.defined = defined;
    // A template may be declared and then defined. The definition is the one
    // worth keeping, since instantiating is replaying its tokens - but any
    // out-of-line members gathered against the declaration come with it.
    auto it = templates_.find(decl.name);
    if (it == templates_.end()) {
        templates_[decl.name] = decl;
    } else if (decl.defined && !it->second.defined) {
        decl.outOfLine = it->second.outOfLine;
        it->second = decl;
    } else if (decl.defined) {
        // **One template per name, and the second is refused rather than
        // dropped.** This table holds one entry per name, so a second
        // definition used to replace nothing and simply disappear - a
        // silently missing overload. Overloading function templates is its
        // own step; until then the reader is told where it stopped.
        src_.fail(decl.pos, "'" + decl.name + "' is already a template, and "
                            "two templates of one name are not supported yet");
    }
    return true;
}

// `template <> struct Box<int> { ... };` - rung 5.6.
//
// A class written out for one argument list instead of made from the
// template. Nothing about the class path changes: the tag is `Box<int>` here
// exactly as it would be if the template had produced it, so every use finds
// this one through the same lookup, the manglers spell it the same way, and
// a member is keyed the same. What differs is only where the body came from.
//
// **The argument list is read against the primary's parameters**, which is
// what decides whether an argument is a type or a value - the same rule as
// every other use, and the reason the primary has to be declared first.
bool Parser::explicitSpecialization() {
    const std::size_t pos = peek().pos;
    expect("<");
    takeClosingAngle();

    if (!peek().is("struct") && !peek().is("class") && !peek().is("union"))
        src_.fail(peek().pos, "an explicit specialization of a function "
                              "template is not supported yet - this one is not "
                              "a class");
    const Kind kind = peek().is("union") ? Kind::Union : Kind::Struct;
    const bool isClass = peek().is("class");
    at_++;

    if (peek().kind != TokenKind::Ident)
        src_.fail(peek().pos, "this specialization names no class");
    const std::string name = peek().text;
    auto primary = templates_.find(name);
    if (primary == templates_.end() || !primary->second.isClass)
        src_.fail(peek().pos, "'" + name + "' is not a class template, so "
                              "there is nothing here to specialize");
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "'" + name + "' is a class template and a "
                              "specialization of it needs its arguments");

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    templateArguments(primary->second, &binding, &values, &args);

    const std::string tag = specializationKey(name, args);
    // **Too late is an error, not a redefinition.** [temp.expl.spec]: a
    // specialization has to be declared before the first use that would
    // instantiate the template, and if one already did then two different
    // classes have been given one name.
    if (findTypedef(tag) != nullptr)
        src_.fail(pos, "'" + tag + "' has already been used further up, so "
                       "specializing it here is too late - the specialization "
                       "goes before the first use");
    if (!peek().is("{"))
        src_.fail(peek().pos, "an explicit specialization is a definition, and "
                              "this one has no body");

    classInstantiationTag_ = tag;
    classInstantiationOf_ = name;
    instantiatingArgs_ = args;
    const Type *made = structOrUnionSpecifier(kind, isClass);
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingArgs_.clear();

    declareTypeName(tag, made);
    expect(";");
    return true;
}

// The tokens read without being consumed. skipTemplateArguments is what walks
// the argument list, so the `>>` split has to be put back too.
bool Parser::atOutOfLineSpecial(std::string *what) {
    if (peek().kind != TokenKind::Ident) return false;
    auto t = templates_.find(peek().text);
    if (t == templates_.end() || !t->second.isClass || !peekAt(1).is("<"))
        return false;

    const std::string name = peek().text;
    const std::size_t resume = at_;
    const std::size_t wasSplit = angleSplit_;
    at_++;
    skipTemplateArguments();

    bool yes = false;
    if (peek().is("::")) {
        std::size_t n = 1;
        if (peekAt(n).is("~")) { n++; *what = "destructor"; }
        else                   { *what = "constructor"; }
        if (peekAt(n).kind == TokenKind::Ident && peekAt(n).text == name &&
            peekAt(n + 1).is("(")) yes = true;
    }
    at_ = resume;
    angleSplit_ = wasSplit;
    return yes;
}

// The argument list, read only far enough to step over it - and stepping over
// it is what proves the `>>` split, since `Box<Box<int>>` cannot be got past
// any other way. A nested list is recognised by its name being a template,
// the same rule that opened this one.
void Parser::skipTemplateArguments() {
    expect("<");
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template argument list is never closed");
        if (atClosingAngle()) { takeClosingAngle(); return; }
        if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
            peekAt(1).is("<")) {
            at_++;
            skipTemplateArguments();
            continue;
        }
        // A parenthesised argument may hold a `>` that closes nothing.
        if (peek().is("(")) {
            int depth = 0;
            do {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this template argument list is "
                                          "never closed");
                if (peek().is("(")) depth++;
                else if (peek().is(")")) depth--;
                at_++;
            } while (depth > 0);
            continue;
        }
        at_++;
    }
}

// `<int, 3>` at a use, read against the parameter list it is for. A type
// parameter takes a type-id and a non-type one a constant expression, so
// which is which is decided by the template and never by the shape of what is
// written - the same rule that decided the `<` itself.
void Parser::templateArguments(const TemplateDecl &decl,
                               std::vector<const Type *> *binding,
                               std::vector<long long> *values,
                               std::vector<TemplateArg> *args,
                               std::vector<std::vector<const Type *> > *packs) {
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    inTemplateArgs_ = true;
    if (packs != nullptr) packs->assign(decl.params.size(),
                                        std::vector<const Type *>());
    for (std::size_t i = 0; i < decl.params.size(); i++) {
        if (i > 0 && !consume(","))
            src_.fail(peek().pos, "'" + decl.name + "' takes " +
                                  std::to_string(decl.params.size()) +
                                  " template arguments and this gives " +
                                  std::to_string(i));
        const TemplateParam &p = decl.params[i];
        TemplateArg a;
        // **A pack takes everything that is left**, including nothing. It is
        // the last parameter by construction, so there is no ambiguity about
        // where it stops: the closing angle stops it.
        if (p.isPack) {
            a.isPack = true;
            a.isType = true;
            while (!atClosingAngle()) {
                if (!a.pack.empty()) expect(",");
                if (peek().kind == TokenKind::Ident && peekAt(1).is("...") &&
                    packs_.find(peek().text) != packs_.end())
                    src_.fail(peek().pos, "expanding a pack into another "
                                          "template's argument list is not "
                                          "supported yet");
                StorageClass sc;
                Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                if (!d.name.empty())
                    src_.fail(d.pos, "a template argument is a type here, and "
                                     "this names something");
                a.pack.push_back(d.type);
            }
            binding->push_back(nullptr);
            values->push_back(0);
            if (packs != nullptr) (*packs)[i] = a.pack;
            args->push_back(a);
            break;
        }
        if (p.type == nullptr) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            if (!d.name.empty())
                src_.fail(d.pos, "a template argument is a type here, and this "
                                 "names something");
            binding->push_back(d.type);
            values->push_back(0);
            a.isType = true;
            a.type = d.type;
        } else {
            if (!p.type->isInteger())
                src_.fail(p.pos, "a non-type template parameter of type '" +
                                 p.type->describe() + "' is not supported yet - "
                                 "it must be an integer type");
            const long long v = constantExpression("a template argument");
            binding->push_back(p.type);
            values->push_back(v);
            a.isType = false;
            a.type = p.type;
            a.value = v;
        }
        args->push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "'" + decl.name + "' takes " +
                              std::to_string(decl.params.size()) +
                              " template arguments and this gives more");
    takeClosingAngle();
    inTemplateArgs_ = wasInArgs;
}

std::string Parser::specializationKey(const std::string &name,
                                      const std::vector<TemplateArg> &args) const {
    std::string key = name + "<";
    for (std::size_t i = 0; i < args.size(); i++) {
        if (i > 0) key += ",";
        if (args[i].isPack) {
            key += "{";
            for (std::size_t k = 0; k < args[i].pack.size(); k++) {
                if (k > 0) key += ",";
                key += args[i].pack[k]->describe();
            }
            key += "}";
        }
        else if (!args[i].isType) key += std::to_string(args[i].value);
        // A pattern's argument is a template parameter, and describe() would
        // put a space in a tag every table is keyed by.
        else if (args[i].type->kind() == Kind::TemplateParam)
            key += "$T" + std::to_string(args[i].type->length());
        else key += args[i].type->describe();
    }
    return key + ">";
}

// The specialization these arguments ask for, made if it is new.
//
// **The two ABIs are handed two different things and that is not cosmetic.**
// Itanium is given the template's *pattern* - the signature with
// Kind::TemplateParam still in it - because its name spells `T_` where a type
// came from a parameter, and the substituted signature cannot say that.
// Microsoft is given the substituted signature, which is what it writes. So
// the declaration is read twice, once each way.
const Parser::Signature &
Parser::instantiate(const TemplateDecl &decl,
                    const std::vector<const Type *> &binding,
                    const std::vector<long long> &values,
                    const std::vector<TemplateArg> &args, std::size_t pos,
                    const std::vector<std::vector<const Type *> > &packs) {
    const std::string key = specializationKey(decl.name, args);

    if (const std::vector<std::size_t> *had = overloadsOf(key))
        return functions_[(*had)[0]];

    if (!decl.defined)
        src_.fail(pos, "'" + decl.name + "' is declared but never defined, so "
                       "there is nothing to instantiate");

    std::string name;
    const Type *fn = readTemplateDeclaration(decl, binding, values, &name,
                                             nullptr, &packs);

    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = decl.params[i].type == nullptr
                         ? types_.templateParam(static_cast<int>(i))
                         : binding[i];
    std::string patternName;
    const Type *patternFn =
        readTemplateDeclaration(decl, pattern, values, &patternName);

    std::string symbol, why;
    const bool ok = target_.microsoftNames()
        ? microsoftTemplateFunctionName(decl.name, fn, args, &symbol, &why)
        : itaniumTemplateFunctionName(decl.name, patternFn, args, false,
                                      &symbol, &why);
    if (!ok)
        src_.fail(pos, "'" + key + "' cannot be given a name the linker can "
                       "hold: " + why);

    Specialization sp;
    sp.key = key;
    sp.name = decl.name;
    sp.params = decl.params;
    sp.packs = packs;
    sp.symbol = symbol;
    sp.fn = fn;
    sp.binding = binding;
    sp.values = values;
    sp.start = decl.afterParams;
    sp.pos = pos;
    specializations_.push_back(sp);

    // **Under two keys, on purpose.** "twice<int>" is what the replayed
    // definition declares and what a repeat of the same arguments finds;
    // "twice" is what overload resolution has to see, because a
    // specialization competes with the ordinary functions of that name and
    // [over.match.best] only gets to break the tie if both are candidates.
    const std::size_t at = functions_.size();
    functionIndex_[key].push_back(at);
    functionIndex_[decl.name].push_back(at);
    functions_.push_back(Signature{ key, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    functions_.back().fromTemplate = true;
    return functions_.back();
}

// **A body cannot be written where the call is**, because the call is in the
// middle of another function. So every specialization is recorded and the
// definitions are replayed afterwards - and to a fixed point, since a body
// may ask for one of its own. The same shape the implicit special members
// already have.
// Whether anything under this key has been chosen by a call. A member
// function of a class template is instantiated only where one has been -
// clang and cl both - so this is the gate on every body a specialization
// holds, inside the class or outside it.
bool Parser::memberIsUsed(const std::string &key) const {
    const std::vector<std::size_t> *set = overloadsOf(key);
    for (std::size_t k = 0; set != nullptr && k < set->size(); k++)
        if (functions_[(*set)[k]].used) return true;
    return false;
}

void Parser::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (specializations_[i].emitted) continue;
            // **Made where it was asked for, defined only where it was
            // chosen.** Deduction has to instantiate a candidate before it
            // can rank one, and an ordinary function may then win the tie -
            // in which case clang emits no specialization and neither does
            // this. The same rule the implicit special members follow.
            // **A member function of a class template is instantiated only
            // where something calls it.** clang and cl both do that, so
            // emitting the rest would put symbols in the object that neither
            // oracle has. Taken a body at a time, and a body that is skipped
            // this time round may be wanted after another one is replayed -
            // which is what the outer loop is for.
            std::vector<PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (specializations_[i].isClass) {
                std::vector<PendingBody> later;
                for (std::size_t b = 0; b < specializations_[i].bodies.size(); b++) {
                    const PendingBody &body = specializations_[i].bodies[b];
                    (memberIsUsed(body.key) ? now : later).push_back(body);
                }
                specializations_[i].bodies = later;

                // **Looked up fresh, because the list can still be growing.**
                // An out-of-line definition may be written further down the
                // file than the use that asked for the class, so what the
                // template has now is not what it had then.
                const TemplateDecl &d = templates_[specializations_[i].name];
                std::vector<bool> &done = specializations_[i].outsideDone;
                done.resize(d.outOfLine.size(), false);
                for (std::size_t k = 0; k < d.outOfLine.size(); k++) {
                    if (done[k]) continue;
                    if (!memberIsUsed(specializations_[i].key + "::" +
                                      d.outOfLine[k].member)) continue;
                    done[k] = true;
                    outsideNow.push_back(k);
                }
                if (now.empty() && outsideNow.empty()) continue;
            } else {
                const std::vector<std::size_t> *had =
                    overloadsOf(specializations_[i].key);
                if (had == nullptr || !functions_[(*had)[0]].used) continue;
                specializations_[i].emitted = true;
            }
            again = true;

            // Copied, not held by reference: replaying may append to the
            // vector and move it.
            const Specialization sp = specializations_[i];
            TemplateDecl decl = templates_[sp.name];

            std::vector<Shadow> undo;
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = instantiationKey_;
            const std::string wasOf = instantiationOf_;
            instantiationKey_ = sp.isClass ? std::string() : sp.key;
            instantiationOf_ = sp.isClass ? std::string() : sp.name;

            const std::size_t resume = at_;
            if (sp.isClass) {
                // Its member functions, written inside the class and held
                // there. inlineOwner_ supplies the "Box<int,3>::" that the
                // source does not have, the same way it does for any class.
                replayInlineBodies(now);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    at_ = templates_[sp.name].outOfLine[outsideNow[k]].start;
                    topLevel(*current_);
                }
            } else {
                at_ = sp.start;
                topLevel(*current_);
            }
            at_ = resume;

            instantiationKey_ = wasKey;
            instantiationOf_ = wasOf;
            unbindTemplateParameters(undo);
        }
    }
}

// **What a parameter sees of an argument.** [temp.deduct.call]: an array
// becomes a pointer to its first element, a function a pointer to itself, and
// the top-level qualifier goes - which is also just what passing something
// does, so this is not a rule deduction invented.
const Type *Parser::decayedType(const Type *a) const {
    if (a->isReference()) a = a->referent();
    if (a->isArray()) return types_.pointerTo(a->pointee());
    if (a->isFunction()) return types_.pointerTo(a);
    return a->unqualified();
}

// One parameter of the pattern against one argument's type.
//
// The pattern still has Kind::TemplateParam in it, so "does this position
// deduce anything" is a question about the type and not about a table: a
// parameter reached here binds, and a type that is not one has to match.
bool Parser::deduceOne(const Type *pattern, const Type *arg,
                       std::vector<const Type *> *binding,
                       std::string *why) const {
    // **A reference parameter looks *through* itself and keeps the argument's
    // qualifier; everything else decays.** `const T &` binding an `int`
    // deduces T as int, and the const on the parameter is not part of T.
    if (pattern->isReference()) {
        pattern = pattern->referent();
        if (arg->isReference()) arg = arg->referent();
        if (pattern->unqualified() != pattern) {
            pattern = pattern->unqualified();
            arg = arg->unqualified();
        }
    } else {
        arg = decayedType(arg);
        if (pattern->unqualified() != pattern) pattern = pattern->unqualified();
    }

    if (pattern->kind() == Kind::TemplateParam) {
        const std::size_t i = static_cast<std::size_t>(pattern->length());
        const Type *deduced = arg->unqualified();
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }

    // **`Holder<T>` against `Holder<int>`.** The two have to be the same
    // template before their arguments mean anything - `Holder<T>` deduces
    // nothing from a `Box<int>`.
    if (pattern->isSpecialization()) {
        if (!arg->isSpecialization() ||
            arg->templateName() != pattern->templateName() ||
            arg->templateArgs().size() != pattern->templateArgs().size()) {
            *why = "'" + arg->describe() + "' is not a '" +
                   pattern->templateName() + "'";
            return false;
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            if (!p.isType || !a.isType) continue;
            if (!deduceOne(p.type, a.type, binding, why)) return false;
        }
        return true;
    }

    if (pattern->isPointer() && arg->isPointer())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, why);
    if (pattern->isArray() && arg->isArray())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, why);

    // Nothing to deduce here. A parameter written out in full does not have to
    // match exactly - an ordinary conversion may still get the argument
    // there - so this is not where a mismatch is reported. Overload
    // resolution ranks the specialization afterwards and refuses it then.
    return true;
}

// The whole call. Answers false with a reason rather than failing, because a
// name may be both a template and an ordinary function: deduction not
// working is then not an error, it is one fewer candidate.
bool Parser::deduceTemplateArguments(const TemplateDecl &decl,
                                     const std::vector<ExprPtr> &args,
                                     std::vector<const Type *> *binding,
                                     std::vector<std::vector<const Type *> > *packs,
                                     std::string *why) {
    packs->assign(decl.params.size(), std::vector<const Type *>());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        if (decl.params[i].type != nullptr) {
            *why = "'" + decl.params[i].name + "' is a non-type parameter, "
                   "and only a type is deduced from a call - write the "
                   "arguments out";
            return false;
        }

    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    const std::vector<long long> none(decl.params.size(), 0);
    std::string ignored;
    const Type *fn = readTemplateDeclaration(decl, pattern, none, &ignored);

    // **A trailing pack takes every argument the written parameters leave.**
    // It is the last parameter by construction, so "the rest" needs no
    // searching - and it may be none, which is why this is a `<` and not a
    // `!=` on the count.
    const bool hasPack = !decl.params.empty() && decl.params.back().isPack;
    const std::size_t fixed = hasPack ? fn->params().size() - 1
                                      : fn->params().size();
    if (hasPack ? args.size() < fixed : args.size() != fixed) {
        *why = "it takes " + std::string(hasPack ? "at least " : "") +
               std::to_string(fixed) + " argument(s) and this call gives " +
               std::to_string(args.size());
        return false;
    }

    binding->assign(decl.params.size(), nullptr);
    for (std::size_t i = 0; i < fixed; i++)
        if (!deduceOne(fn->params()[i], args[i]->type(), binding, why)) {
            *why = "'" + decl.params[i < decl.params.size() ? i : 0].name +
                   "' cannot be worked out from this call: " + *why;
            return false;
        }
    if (hasPack) {
        std::vector<const Type *> members;
        for (std::size_t i = fixed; i < args.size(); i++)
            members.push_back(decayedType(args[i]->type()));
        (*packs)[decl.params.size() - 1] = members;
    }
    for (std::size_t i = 0; i < binding->size(); i++) {
        if (decl.params[i].isPack) continue;
        if ((*binding)[i] == nullptr) {
            *why = "'" + decl.params[i].name + "' appears in no parameter, so "
                   "there is nothing in the call to work it out from - write "
                   "the arguments out";
            return false;
        }
    }
    return true;
}

// Whether parameter `i` appears anywhere in a pattern. A parameter a
// specialization never mentions could not be worked out from any argument
// list, so the specialization could never be chosen - which is worth refusing
// where it is written rather than leaving as a specialization that silently
// never applies.
static bool mentionsParam(const Type *t, std::size_t i) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsParam(t->unqualified(), i);
    if (t->kind() == Kind::TemplateParam)
        return static_cast<std::size_t>(t->length()) == i;
    if (t->isPointer() || t->isArray()) return mentionsParam(t->pointee(), i);
    if (t->isReference()) return mentionsParam(t->referent(), i);
    if (t->isSpecialization()) {
        for (std::size_t k = 0; k < t->templateArgs().size(); k++)
            if (t->templateArgs()[k].isType &&
                mentionsParam(t->templateArgs()[k].type, i)) return true;
        return false;
    }
    return false;
}

Parser::Trial::Trial(Parser *parser)
    : p(parser), at(parser->at_), classes(parser->classStack_.size()),
      pattern(parser->patternOnly_) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Parser::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why) const {
    // **The qualifier is asked about before anything else, and both sides
    // must agree.** `Box<const T>` matches `Box<const int>` with T as int; it
    // does not match `Box<int>`. `Box<T>` matches both, binding T to the
    // qualified type where there is one - which is why this comes first and
    // the parameter case second.
    if (pattern->unqualified() != pattern) {
        if (arg->unqualified() == arg) {
            *why = "'" + arg->describe() + "' is not const";
            return false;
        }
        return matchPattern(pattern->unqualified(), arg->unqualified(),
                            binding, why);
    }

    if (pattern->kind() == Kind::TemplateParam) {
        const std::size_t i = static_cast<std::size_t>(pattern->length());
        if ((*binding)[i] == nullptr) { (*binding)[i] = arg; return true; }
        if ((*binding)[i] != arg) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one place and '" +
                   arg->describe() + "' in another";
            return false;
        }
        return true;
    }

    if (pattern->isPointer())
        return arg->isPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    if (pattern->isReference())
        return arg->isReference() &&
               matchPattern(pattern->referent(), arg->referent(), binding, why);
    if (pattern->isArray())
        return arg->isArray() && pattern->length() == arg->length() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);

    if (pattern->isSpecialization()) {
        if (!arg->isSpecialization() ||
            arg->templateName() != pattern->templateName() ||
            arg->templateArgs().size() != pattern->templateArgs().size())
            return false;
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            if (p.isType != a.isType) return false;
            if (!p.isType) {
                if (p.value != a.value) return false;
                continue;
            }
            if (!matchPattern(p.type, a.type, binding, why)) return false;
        }
        return true;
    }

    if (pattern != arg) {
        *why = "'" + arg->describe() + "' is not '" + pattern->describe() + "'";
        return false;
    }
    return true;
}

// `Box<T *>` - read with this specialization's own parameters bound to
// themselves, so what comes out is a pattern rather than a type.
void Parser::partialArguments(TemplateDecl::Partial *ps, std::size_t count) {
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    const bool wasPattern = patternOnly_;
    inTemplateArgs_ = true;
    patternOnly_ = true;

    std::vector<Shadow> undo;
    std::vector<const Type *> binding(ps->params.size());
    std::vector<long long> values(ps->params.size(), 1);
    for (std::size_t i = 0; i < ps->params.size(); i++)
        binding[i] = ps->params[i].type == nullptr
                         ? types_.templateParam(static_cast<int>(i))
                         : ps->params[i].type;
    bindTemplateParameters(ps->params, binding, values,
                           std::vector<std::vector<const Type *> >(), &undo);

    for (std::size_t i = 0; i < count; i++) {
        if (i > 0) expect(",");
        TemplateDecl::Partial::Arg a;
        // **A non-type argument that is one of our own parameters is the only
        // shape of one that deduces**, so it is recognised by its tokens
        // before it can be folded into the value it was bound to.
        std::size_t which = ps->params.size();
        if (peek().kind == TokenKind::Ident)
            for (std::size_t k = 0; k < ps->params.size(); k++)
                if (ps->params[k].type != nullptr &&
                    ps->params[k].name == peek().text) which = k;
        if (which < ps->params.size() &&
            (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
            a.isType = false;
            a.isParam = true;
            a.param = which;
            at_++;
        } else if (atTypeName()) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            a.isType = true;
            a.type = d.type;
        } else {
            a.isType = false;
            a.value = constantExpression("a template argument");
        }
        ps->args.push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "this specialization gives more arguments than "
                              "the template has parameters");
    takeClosingAngle();

    unbindTemplateParameters(undo);
    inTemplateArgs_ = wasInArgs;
    patternOnly_ = wasPattern;
}

// [temp.class.order], asked the standard's own way: A is at least as
// specialized as B when B's pattern matches A's. A's parameters stand as
// opaque types while that happens, which is exactly what they already are -
// Kind::TemplateParam is not a type anything can be.
bool Parser::atLeastAsSpecialized(const TemplateDecl::Partial &a,
                                  const TemplateDecl::Partial &b) const {
    std::vector<const Type *> binding(b.params.size());
    std::string why;
    for (std::size_t i = 0; i < a.args.size() && i < b.args.size(); i++) {
        if (a.args[i].isType != b.args[i].isType) return false;
        if (!a.args[i].isType) {
            if (b.args[i].isParam) continue;      // a parameter takes anything
            if (a.args[i].isParam) return false;
            if (a.args[i].value != b.args[i].value) return false;
            continue;
        }
        if (!matchPattern(b.args[i].type, a.args[i].type, &binding, &why))
            return false;
    }
    return true;
}

bool Parser::moreSpecialized(const TemplateDecl::Partial &a,
                             const TemplateDecl::Partial &b) const {
    return atLeastAsSpecialized(a, b) && !atLeastAsSpecialized(b, a);
}

// Which partial specialization these arguments ask for.
std::size_t Parser::choosePartial(const TemplateDecl &decl,
                                  const std::vector<TemplateArg> &args,
                                  std::vector<const Type *> *binding,
                                  std::vector<long long> *values,
                                  std::size_t pos) {
    std::vector<std::size_t> fits;
    std::vector<std::vector<const Type *> > bindings;
    std::vector<std::vector<long long> > valueSets;

    for (std::size_t p = 0; p < decl.partials.size(); p++) {
        const TemplateDecl::Partial &ps = decl.partials[p];
        if (ps.args.size() != args.size()) continue;
        std::vector<const Type *> b(ps.params.size());
        std::vector<long long> v(ps.params.size(), 0);
        std::string why;
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            const TemplateDecl::Partial::Arg &a = ps.args[i];
            if (a.isType != args[i].isType) { ok = false; break; }
            if (!a.isType) {
                if (a.isParam) v[a.param] = args[i].value;
                else if (a.value != args[i].value) ok = false;
                continue;
            }
            if (!matchPattern(a.type, args[i].type, &b, &why)) ok = false;
        }
        for (std::size_t i = 0; ok && i < ps.params.size(); i++)
            if (ps.params[i].type == nullptr && b[i] == nullptr) ok = false;
        if (!ok) continue;
        fits.push_back(p);
        bindings.push_back(b);
        valueSets.push_back(v);
    }

    if (fits.empty()) return static_cast<std::size_t>(-1);

    // **One has to beat every other, and "not beaten" is not the same as
    // "beats".** `P<A, int>` and `P<int, B>` given `P<int, int>` are the case:
    // neither matches the other, so neither is more specialized, and the
    // program is ambiguous. Asking only whether the winner was beaten lets
    // that through and picks whichever came first, which is the silent kind
    // of wrong this compiler refuses.
    std::size_t best = 0;
    for (std::size_t k = 1; k < fits.size(); k++)
        if (moreSpecialized(decl.partials[fits[k]], decl.partials[fits[best]]))
            best = k;
    for (std::size_t k = 0; k < fits.size(); k++)
        if (k != best &&
            !moreSpecialized(decl.partials[fits[best]], decl.partials[fits[k]]))
            src_.fail(pos, "'" + decl.name + "' has two partial "
                           "specializations that fit these arguments and "
                           "neither is more specialized than the other");

    *binding = bindings[best];
    *values = valueSets[best];
    return fits[best];
}

// `Box<int, 3>` where a type was expected - rung 5.4.
//
// The class is made by replaying `struct Box { ... };` with the arguments
// bound, exactly as a function specialization replays its definition, and the
// only thing the class path had to be told is what tag to take. Everything
// else falls out of nested classes: tag() was already an arbitrary qualified
// string, and both manglers already walked a scope.
const Type *Parser::instantiateClass(const TemplateDecl &decl, std::size_t pos) {
    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    std::vector<std::vector<const Type *> > packs;
    templateArguments(decl, &binding, &values, &args, &packs);

    const std::string tag = specializationKey(decl.name, args);

    // Reading a pattern, not building a class. `Holder<T>` cannot be laid out
    // - T has no size - and neither the mangler nor deduction wants it laid
    // out: both read only the template's name and its argument list.
    if (patternOnly_) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization())
            shallow->setSpecialization(decl.name, args);
        // Registered so that `Box<T>::get` reads: the declarator's qualified
        // path looks the class up by name, and this is the only name it has.
        // The tag holds a `$` and so cannot collide with anything written.
        declareTypeName(tag, shallow);
        return shallow;
    }

    if (const Type *had = findTypedef(tag)) return had;

    if (!decl.defined)
        src_.fail(pos, "'" + decl.name + "' is declared but never defined, so "
                       "there is nothing to instantiate");

    // **A partial specialization is chosen before anything is replayed**, and
    // what it changes is which tokens get replayed and with which parameters
    // bound. The tag does not change: `Box<int *>` is that whether the body
    // came from the template or from a pattern that matched it, which is what
    // keeps the mangling and every lookup the same.
    std::vector<const Type *> useBinding = binding;
    std::vector<long long> useValues = values;
    std::vector<TemplateParam> useParams = decl.params;
    const std::size_t which = choosePartial(decl, args, &useBinding, &useValues,
                                            pos);
    const bool partial = which != static_cast<std::size_t>(-1);
    if (partial) useParams = decl.partials[which].params;

    const std::size_t resume = at_;
    std::vector<Shadow> undo;
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? std::vector<std::vector<const Type *> >()
                                   : packs,
                           &undo);

    at_ = partial ? decl.partials[which].bodyAt : decl.afterParams;
    classInstantiationTag_ = tag;
    if (partial) classInstantiationOf_ = decl.name;
    instantiatingArgs_ = args;
    heldForSpecialization_.clear();
    const bool wasDeferring = deferSpecializationBodies_;
    deferSpecializationBodies_ = true;
    StorageClass sc;
    Qualifiers quals;
    const Type *made = partial
        ? structOrUnionSpecifier(Kind::Struct, false)
        : specifiers(&sc, &quals);
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingArgs_.clear();
    deferSpecializationBodies_ = wasDeferring;
    std::vector<PendingBody> bodies;
    bodies.swap(heldForSpecialization_);

    unbindTemplateParameters(undo);
    at_ = resume;

    if (!made->isStructOrUnion())
        src_.fail(pos, "'" + decl.name + "' is not a class template");
    declareTypeName(tag, made);

    Specialization sp;
    sp.key = tag;
    sp.name = decl.name;
    sp.params = useParams;
    sp.binding = useBinding;
    sp.values = useValues;
    if (!partial) sp.packs = packs;
    sp.start = decl.afterParams;
    sp.pos = pos;
    sp.isClass = true;
    sp.bodies = bodies;
    specializations_.push_back(sp);
    return made;
}

// A template named in an expression. 5.2 wants the arguments written out:
// deducing them from the call is 5.3, and a class template is 5.4.
ExprPtr Parser::templateCall(Program *program) {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    const TemplateDecl decl = templates_[name];
    if (decl.isClass) refuseTemplateId();
    at_++;

    // **No argument list, so they come from the call.** The arguments have to
    // be parsed before anything can be deduced from them, which is the other
    // way round from the written case - and it is also the order overload
    // resolution wants, since the specialization competes with every ordinary
    // function of the same name.
    if (!peek().is("<")) {
        if (!peek().is("("))
            src_.fail(pos, "'" + name + "' is a function template, and naming "
                           "one without calling it is not supported yet");
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);

        std::vector<const Type *> deduced;
        std::vector<std::vector<const Type *> > deducedPacks;
        std::string why;
        if (!deduceTemplateArguments(decl, callArgs, &deduced, &deducedPacks,
                                     &why)) {
            // Not an error while an ordinary function of this name might
            // still take the call - it is one fewer candidate. With no such
            // function it is the whole answer, and saying why beats "not
            // declared".
            if (overloadsOf(name) == nullptr)
                src_.fail(pos, "'" + name + "' is a function template and " + why);
        } else {
            std::vector<long long> values(decl.params.size(), 0);
            std::vector<TemplateArg> args;
            for (std::size_t i = 0; i < deduced.size(); i++) {
                TemplateArg a;
                a.isType = true;
                if (decl.params[i].isPack) {
                    a.isPack = true;
                    a.pack = deducedPacks[i];
                } else {
                    a.type = deduced[i];
                }
                args.push_back(a);
            }
            // **[temp.deduct]/8, which is what SFINAE is.** The arguments
            // deduce, but substituting them into the signature may make
            // something ill-formed - `enable_if<false, int>::type` names no
            // type - and that removes the specialization from consideration
            // rather than ending the compile. It is the only failure in this
            // compiler that recovers, and it recovers exactly this far: a
            // failure inside a *body* is still an error, because a body is
            // not part of the signature and the standard does not put it in
            // the immediate context either.
            try {
                Trial trial(this);
                instantiate(decl, deduced, values, args, pos, deducedPacks);
            } catch (const SubstitutionFailure &f) {
                if (overloadsOf(name) == nullptr)
                    src_.fail(pos, "'" + name + "' is a function template and "
                                   "its arguments do not substitute: " + f.why);
            }
        }

        const Signature &sig = resolveOverload(name, callArgs, pos);
        return completeCall(sig.name, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, pos, std::move(callArgs));
    }

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    std::vector<std::vector<const Type *> > packs;
    templateArguments(decl, &binding, &values, &args, &packs);

    instantiate(decl, binding, values, args, pos, packs);
    if (!peek().is("("))
        src_.fail(peek().pos, "'" + name + "' is a function template, and "
                              "naming one without calling it is not supported "
                              "yet");
    at_++;
    (void)program;
    std::vector<ExprPtr> callArgs;
    parseArguments(callArgs);

    // Looked up by key rather than held across the arguments: an argument may
    // itself be a call that instantiates something, and a reference into
    // functions_ does not survive the vector growing.
    const std::string key = specializationKey(decl.name, args);
    const std::size_t which = (*overloadsOf(key))[0];
    // Written out rather than deduced, so no ranking chose it and nothing
    // else will mark it - and a specialization is defined only where it was
    // chosen. Saying so here is what makes this call get a body.
    functions_[which].used = true;
    const Signature &sig = functions_[which];
    return completeCall(sig.name, sig.symbol, nullptr, sig.returns, sig.params,
                        sig.variadic, pos, std::move(callArgs));
}

void Parser::refuseTemplateId() {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    at_++;
    if (peek().is("<")) skipTemplateArguments();
    src_.fail(pos, "'" + name + "' is a " +
                   (templates_[name].isClass ? "class" : "function") +
                   " template, and instantiating one is not supported yet");
}

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

        StorageClass msc;
        const Type *base = specifiers(&msc);

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
            if (d.type->isReference())
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
    type->setDataSize(members.empty() && totalBits == 0
                          ? 1 : static_cast<int>((totalBits + 7) / 8));
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
    if (copyConstructorOf(type) != nullptr) type->setNonTrivialCopy(true);
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

    // Replaying `~X() { ... }` from inside the class: the '~' belongs to the
    // name, and there is no '::' to hang it off.
    bool inlineDtor = false;
    if (!inlineOwner_.empty() && peek().is("~")) { at_++; inlineDtor = true; }

    if (nameOptional && peek().kind != TokenKind::Ident) name.clear();
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
            name = expectIdent("a member name");
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
        name = expectIdent("a member name after '::'");
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
// One candidate beats another when no conversion is worse and at least one is
// better. **And, all conversions being equal, when it is not a
// specialization** - [over.match.best]. That last line is not a tiebreak of
// convenience: deduction makes twice<int> match `twice(1)` exactly, and so
// does an ordinary `int twice(int)`, so without it every such call is
// ambiguous.
bool Parser::betterCandidate(const std::vector<Rank> &a,
                             const std::vector<Rank> &b,
                             const Signature &fa, const Signature &fb) const {
    bool better = false, worse = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        if (a[i] < b[i]) better = true;
        if (a[i] > b[i]) worse = true;
    }
    if (better && worse) return false;
    if (better) return true;
    if (worse) return false;
    return !fa.fromTemplate && fb.fromTemplate;
}

const Parser::Signature &Parser::resolveOverload(const std::string &name,
                                                 const std::vector<ExprPtr> &args,
                                                 std::size_t pos,
                                                 const Type *object) {
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr) {
        // **`C(...)` where C is a class** is a temporary, not a call to a
        // function nobody declared, and saying "no prototype" sends the
        // reader looking for a declaration that was never meant to exist.
        // Worth intercepting by name now that passing a class by value copies
        // it, which is exactly when somebody writes this.
        if (const Type *cls = findTypedef(name))
            if (cls->isStructOrUnion())
                src_.fail(pos, "'" + name + "(...)' makes a temporary of type '" +
                               cls->describe() + "', which is not supported yet - "
                               "name a variable of that type and use that");
        src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
    }

    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    // Set when the only thing that stopped a candidate was the constness of
    // the object, so that "no function takes these arguments" can be replaced
    // by the message that says what actually went wrong.
    bool droppedForConst = false;

    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.variadic ? args.size() < f.params.size()
                       : args.size() != f.params.size()) continue;

        // **The implicit object parameter goes first**, and ranking it is what
        // separates `get()` from `get() const`. Binding it is a reference
        // binding like any other: an exact match where the constness agrees, a
        // qualification conversion where a const member is called on a
        // non-const object - which is why the non-const one wins there - and
        // no match at all the other way round.
        std::vector<Rank> r;
        if (object != nullptr) {
            if (object->isConst() && !f.constThis) { droppedForConst = true; continue; }
            r.push_back(object->isConst() == f.constThis ? Rank::Identity
                                                         : Rank::Qualification);
        }

        const std::size_t first = r.size();
        r.resize(first + args.size(), Rank::Ellipsis);
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok; i++) {
            if (i >= f.params.size()) continue;      // reached by the ellipsis
            r[first + i] = rankArgument(*args[i], f.params[i]);
            if (r[first + i] == Rank::None) ok = false;
        }
        if (!ok) continue;
        viable.push_back((*set)[k]);
        ranks.push_back(r);
    }

    if (viable.empty() && droppedForConst)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(functions_[(*set)[k]]);
        src_.fail(pos, why);
    }
    if (viable.size() == 1) {
        functions_[viable[0]].used = true;
        return functions_[viable[0]];
    }

    std::size_t best = 0;
    for (std::size_t k = 1; k < viable.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            functions_[viable[k]], functions_[viable[best]]))
            best = k;
    for (std::size_t k = 0; k < viable.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             functions_[viable[best]], functions_[viable[k]])) {
            std::string why = "this call to '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < viable.size(); j++)
                why += "\n    candidate: " + describeSignature(functions_[viable[j]]);
            src_.fail(pos, why);
        }
    }
    functions_[viable[best]].used = true;
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

// The deleting destructor's name. Built through the manglers rather than by
// concatenation, because a nested class's is a whole nested-name -
// ??_GInner@Outer@@UEAAPEAXI@Z, not ??_GOuter::Inner@@...
std::string Parser::deletingDestructorSymbol(const std::string &cls) {
    return target_.microsoftNames()
         ? microsoftDeletingDestructorName(cls, findTypedef(cls))
         : itaniumDeletingDestructorName(cls, findTypedef(cls));
}

void Parser::declareDestructor(const std::string &cls, std::size_t pos,
                               Access access, bool isVirtual) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    if (!params.empty() || variadic)
        src_.fail(pos, "a destructor takes no parameters");

    if (overloadsOf(destructorKey(cls)) != nullptr)
        src_.fail(pos, "'" + cls + "' has two destructors, and a class has one");
    registerDestructor(cls, pos, access, isVirtual, false);
}

// Everything a destructor needs in the tables, whether a program wrote it or
// the compiler did: the name, the entry, and the vtable slots a virtual one
// claims.
void Parser::registerDestructor(const std::string &cls, std::size_t pos,
                                Access access, bool isVirtual, bool implicit) {
    const std::vector<const Type *> params;
    const std::string key = destructorKey(cls);

    // **A base with a virtual destructor makes this one virtual**, keyword or
    // not - [class.dtor], and the same rule declareMember follows for an
    // ordinary override. The base's slots are already down in this class's
    // table, so the question is answered by looking for the "~" entry, and it
    // has to be answered here rather than after the name is built: the code
    // letter below reads it.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == "~") { slot = i; isVirtual = true; break; }

    // **A virtual destructor is U on Microsoft whatever its access**, the same
    // rule a virtual member function already followed - measured with cl,
    // which writes ??1VB@@UEAA@XZ where a non-virtual public one is QEAA.
    const char code = isVirtual                   ? 'U'
                    : access == Access::Public    ? 'Q'
                    : access == Access::Protected ? 'I'
                                                  : 'A';
    std::string out;
    if (target_.microsoftNames()) out = microsoftDestructorName(cls, findTypedef(cls), code);
    else                          itaniumDestructorName(cls, findTypedef(cls), true, &out);

    functionIndex_[key].push_back(functions_.size());
    functions_.push_back(Signature{ "~" + localOf(cls), out, types_.get(Kind::Void),
                                    params, false, false, pos, false, cls, false,
                                    access, isVirtual });
    functions_.back().implicit = implicit;

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
    const std::string deleting = deletingDestructorSymbol(cls);

    std::vector<const Type *> none;
    if (slot < slots.size()) {
        slots[slot].symbol = ms ? deleting : out;       // the complete form
        if (!ms && slot + 1 < slots.size() &&
            slots[slot + 1].name == "~$deleting")
            slots[slot + 1].symbol = deleting;
        return;
    }
    slots.push_back(VSlot{ "~", ms ? deleting : out, none, false });
    if (!ms) slots.push_back(VSlot{ "~$deleting", deleting, none, false });
}

// **Setting the vptr, for whoever is building the object.** Pulled out of the
// constructor path when implicit constructors arrived: an implicitly declared
// default or copy constructor stores exactly the same pointers a written one
// does, and a second copy of this would be a second place to get the header
// offset wrong.
//
// What is stored is the table's address plus the header: Itanium writes
// offset-to-top and typeinfo first, so the vptr points at table + 16 -
// measured from clang's own `addq $16` - and Microsoft has no header, so the
// address is the table's own.
std::vector<StmtPtr> Parser::storeVptrs(const std::string &cls,
                                        const Type *memberOf, int thisSlot) {
    const bool ms = target_.microsoftNames();
    const std::string table = ms ? "??_7" + cls + "@@6B@"
                                 : "_ZTV" + std::to_string(cls.size()) +
                                   cls;
    const Type *entry = types_.pointerTo(types_.get(Kind::Void));
    const Type *entries = types_.pointerTo(entry);

    // **The table's ADDRESS, not its contents.** A global Var is an
    // lvalue and reading one loads from it - which stored the table's
    // first word in the vptr and crashed on the first call. Giving it the
    // array type and decaying it is what yields the address, the same road
    // any array name takes.
    std::size_t entryCount = vtables_[cls].size() + (ms ? 0 : 2);
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

    ExprPtr self(Var::local("this", thisSlot));
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
            secondaryVptr_.find(cls + "::" + bs[bi].type->tag());
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

        ExprPtr self2(Var::local("this", thisSlot));
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

    return withVptr;
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
    const std::string symbol = deletingDestructorSymbol(cls);
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
    // Calling one is what asks for a body, which is the only thing that makes
    // an implicit destructor a function at all.
    functions_[static_cast<std::size_t>(&dtor - &functions_[0])].used = true;
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
// **One region per stretch, and the stretches do not overlap.** Objects a, b
// and c built in that order give three ranges - after a, after b, after c -
// and each pad destroys exactly what exists by then. That is what lets a
// call-site table hold them: sorted and disjoint, where nesting them would
// not be.
//
// The statements that *do* the constructing are outside every region on
// purpose: an exception from a constructor leaves that object unbuilt, and
// the region before it destroys what came earlier.
std::vector<StmtPtr> Parser::wrapCleanups(
    std::vector<StmtPtr> body,
    const std::vector<std::pair<std::size_t, std::size_t> > &built,
    std::size_t aliveAtEntry, std::size_t pos) {
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    functionHasPads_ = true;

    std::vector<StmtPtr> out;
    for (std::size_t i = 0; i < built[0].first; i++)
        out.push_back(std::move(body[i]));

    for (std::size_t k = 0; k < built.size(); k++) {
        const std::size_t from = built[k].first;
        const std::size_t to = k + 1 < built.size() ? built[k + 1].first
                                                    : body.size();
        std::vector<StmtPtr> guarded;
        for (std::size_t i = from; i < to; i++) guarded.push_back(std::move(body[i]));
        if (guarded.empty()) continue;
        out.push_back(StmtPtr(new Try(
            std::move(guarded),
            cleanupPad(aliveAtEntry, built[k].second, pointerSlot, pos),
            pointerSlot, selectorSlot, std::vector<std::string>())));
    }
    return out;
}

// **What an exception has to do on its way out of a scope.** The objects are
// the same ones a `return` unwinds - `alive_` holds them and nothing new had
// to track them - and the only difference is where the code runs from: a
// landing pad rather than the return path, ending in _Unwind_Resume rather
// than in a return.
StmtPtr Parser::cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                           std::size_t pos) {
    // **Bounded rather than truncated.** Resizing `alive_` down and back up
    // would default-construct what it had thrown away, and the second pad
    // would then destroy an object with no class - silently one destructor
    // short.
    std::vector<StmtPtr> steps;
    emitDestructors(steps, from, pos, -1, to);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
    ptr->setType(voidPtr);
    args.push_back(std::move(ptr));
    steps.push_back(StmtPtr(new ExprStmt(
        runtimeCall("_Unwind_Resume", types_.get(Kind::Void), std::move(args)))));

    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

void Parser::emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                             std::size_t pos, int except, std::size_t to) {
    if (to > alive_.size()) to = alive_.size();
    for (std::size_t i = to; i > from; i--) {
        const Alive &a = alive_[i - 1];
        if (except >= 0 && a.offset == except && !a.byAddress) continue;
        const Signature *dtor = destructorOf(a.cls);
        if (dtor == nullptr) continue;

        ExprPtr addr;
        if (a.byAddress) {
            // The slot holds the caller's pointer, and that pointer IS the
            // object's address.
            addr = ExprPtr(Var::local(a.name, a.offset));
            addr->setType(types_.pointerTo(a.cls));
        } else {
            ExprPtr object(Var::local(a.name, a.offset));
            object->setType(a.cls);
            addr = ExprPtr(new Unary('&', std::move(object)));
            addr->setType(types_.pointerTo(a.cls));
        }
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
            ? microsoftConstructorName(cls, findTypedef(cls), fn, code, &out, &why)
            : itaniumConstructorName(cls, findTypedef(cls), fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + cls + "::" + cls + "' cannot be given a name the "
                       "linker can hold: " + why);

    set.push_back(functions_.size());
    functions_.push_back(Signature{ cls, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, cls, false, access });
}

// The class a member is built from: the element type when the member is an
// array, and nothing at all when it is not of class type.
static const Type *memberClass(const Type *t) {
    while (t != nullptr && t->isArray()) t = t->pointee();
    return (t != nullptr && t->isStructOrUnion()) ? t->unqualified() : nullptr;
}

// One element of an array member, by address: the member's own address,
// decayed, plus the index times the element's size.
//
// **In bytes, and deliberately.** A Binary built here is not the parser's
// pointer arithmetic and gets none of its scaling - the same trap the vptr
// store hit, where `+ 2` added two bytes rather than two entries.
static ExprPtr indexBytes(TypeTable &types, ExprPtr decayed, const Type *elem,
                          int indexSlot, const Target &target) {
    const Type *idx = types.intType();
    ExprPtr i(Var::local("$i", indexSlot));
    i->setType(idx);
    ExprPtr size(new Num(static_cast<long long>(elem->size(target))));
    size->setType(idx);
    ExprPtr off(new Binary(BinOp::Mul, std::move(i), std::move(size)));
    off->setType(idx);
    const Type *ptr = types.pointerTo(elem);
    ExprPtr at(new Binary(BinOp::Add, std::move(decayed), std::move(off)));
    at->setType(ptr);
    return at;
}

const Parser::Signature *Parser::defaultConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++)
        if (functions_[(*set)[k]].params.empty()) return &functions_[(*set)[k]];
    return nullptr;
}

const Parser::Signature *Parser::copyConstructorOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(cls->tag()));
    if (set == nullptr) return nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.params.size() != 1 || !f.params[0]->isReference()) continue;
        if (f.params[0]->referent()->unqualified() != cls->unqualified()) continue;
        return &f;
    }
    return nullptr;
}

const Parser::Signature *Parser::copyAssignOf(const Type *cls) const {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty())
        return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(assignmentKey(cls->tag()));
    return set == nullptr ? nullptr : &functions_[(*set)[0]];
}

std::string Parser::baseConstructorSymbol(const Signature &ctor, const Type *base) {
    if (target_.microsoftNames()) return ctor.symbol;
    const Type *fnType = types_.functionType(types_.get(Kind::Void), ctor.params,
                                             false);
    std::string sub, why;
    if (itaniumConstructorName(base->tag(), base, fnType, false, &sub, &why))
        return sub;
    return ctor.symbol;
}

// **A trivial special member is not a function**, and that is measured rather
// than reasoned: cl emits no symbol at all for the default constructor,
// copy constructor or copy assignment of a class with no virtual function and
// no member that needs building, and clang emits none either, on both Itanium
// targets. A class like that leaves its storage alone and `X x;` is a frame
// slot and no call - which is exactly what this compiler already did for a C
// struct, and why the old path is left to handle it untouched.
//
// So an implicit member is declared only where it has work to do. What makes
// work: a virtual function, whose vptr somebody has to store, or a base or
// member that has a constructor of its own to run.
//
// A class that writes any constructor gets no implicit default one - that is
// [class.ctor], and it is also what makes `Point p;` still an error for a
// class whose only constructor takes arguments.
void Parser::declareImplicitSpecials(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (tag.empty() || type->kind() == Kind::Union) return;
    // Asked before the copy constructor is declared, because declaring one
    // would answer it yes. A class that writes any constructor gets no
    // implicit default one; a class that writes any constructor still gets an
    // implicit copy constructor.
    const bool wroteConstructor = overloadsOf(constructorKey(tag)) != nullptr;
    declareImplicitDestructor(tag, type, pos);
    declareImplicitCopyCtor(tag, type, pos);
    declareImplicitCopyAssign(tag, type, pos);
    if (wroteConstructor) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (!bs[i].type->tag().empty() &&
            overloadsOf(constructorKey(bs[i].type->tag())) != nullptr)
            work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc != nullptr && !mc->tag().empty() &&
            overloadsOf(constructorKey(mc->tag())) != nullptr)
            work = true;
    }
    if (!work) return;

    const std::vector<const Type *> params;
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a default constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
}

// **The destructor the class did not write.** It becomes a function exactly
// when a base or a member has one of its own to run - measured with cl, which
// emits `??1Has@@QEAA@XZ` for a class holding members with destructors and no
// destructor symbol at all for a class of plain members.
//
// **A virtual function does not make it non-trivial**, which is the one that
// would have been guessed wrong: cl emits nothing for a class with a virtual
// `f()` and no destructor anywhere. What makes it *virtual* is a base whose
// destructor is virtual, and then it takes over that slot and gets a deleting
// form beside it like any other virtual destructor.
void Parser::declareImplicitDestructor(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(destructorKey(tag)) != nullptr) return;

    bool work = false;
    bool isVirtual = false;
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++)
        if (const Signature *d = destructorOf(bs[i].type)) {
            work = true;
            if (d->isVirtual) isVirtual = true;
        }
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (destructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    registerDestructor(tag, pos, Access::Public, isVirtual, true);
}

// Its body: the members this class added, in the reverse of the order they
// were declared, and then the bases in the reverse of the order they were
// written. A base's own destructor deals with the members it brought, which is
// why they are skipped here - they are in this class's member list too,
// because data members are copied down.
void Parser::synthesizeDestructor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const bool isVirtual = functions_[which].isVirtual;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    const std::vector<Member> &ms = type->members();

    for (std::size_t n = ms.size(); n-- > 0; ) {
        bool fromBase = false;
        for (std::size_t k = 0; k < bs.size() && !fromBase; k++)
            if (ms[n].offset >= bs[k].offset &&
                ms[n].offset < bs[k].offset + bs[k].type->dataSize())
                fromBase = true;
        if (fromBase) continue;

        const Type *mt = ms[n].type;
        const Type *elem = mt->isArray() ? mt->pointee() : mt;
        const Signature *dtor = destructorOf(memberClass(mt));
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of '" +
                           memberClass(mt)->tag() + "', the type of '" +
                           ms[n].name + "', is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = mt->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[n].name + "' has no length, "
                               "so the destructor the compiler would write does "
                               "not know how many elements to destroy");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr obj(new Unary('*', std::move(me)));
        obj->setType(type);
        ExprPtr acc(new MemberAccess(std::move(obj), ms[n].name, ms[n].offset));
        acc->setType(mt);

        ExprPtr address;
        if (mt->isArray()) {
            // **Backwards**, because an array is destroyed in the reverse of
            // the order it was built: the index counts up and the element it
            // reaches is (count - 1 - i).
            const Type *idx = types_.intType();
            ExprPtr last(new Num(count - 1));
            last->setType(idx);
            ExprPtr i(Var::local("$i", indexSlot));
            i->setType(idx);
            ExprPtr back(new Binary(BinOp::Sub, std::move(last), std::move(i)));
            back->setType(idx);
            ExprPtr size(new Num(static_cast<long long>(elem->size(target_))));
            size->setType(idx);
            ExprPtr off(new Binary(BinOp::Mul, std::move(back), std::move(size)));
            off->setType(idx);
            const Type *ptr = types_.pointerTo(elem->unqualified());
            ExprPtr at(new Binary(BinOp::Add, decay(std::move(acc)),
                                  std::move(off)));
            at->setType(ptr);
            address = std::move(at);
        } else {
            address = ExprPtr(new Unary('&', std::move(acc)));
            address->setType(types_.pointerTo(elem->unqualified()));
        }

        StmtPtr one(new ExprStmt(destructorCall(std::move(address), *dtor, pos)));
        body.push_back(mt->isArray()
                       ? eachElement(indexSlot, count, std::move(one))
                       : std::move(one));
    }

    for (std::size_t n = bs.size(); n-- > 0; ) {
        const Type *base = bs[n].type;
        const Signature *dtor = destructorOf(base);
        if (dtor == nullptr) continue;
        if (dtor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be destroyed by the destructor "
                           "the compiler would write: the destructor of its "
                           "base '" + base->tag() + "' is " +
                           (dtor->access == Access::Private ? "private"
                                                            : "protected"));
        // The base-subobject form, D2, which is what a derived class calls -
        // the same name a written destructor reaches for.
        std::string sym = dtor->symbol;
        if (!target_.microsoftNames())
            itaniumDestructorName(base->tag(), base, false, &sym);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[n].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall("~" + base->tag(), sym, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args)))));
    }

    current_->functions.push_back(Function(cls + "::~" + localOf(cls),
                                           types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    if (!target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(cls, type, false, &d2);
        current_->functions.back().setAlias(d2);
    }
    frameSize_ = savedFrame;

    // A virtual one carries the deleting form into the vtable beside it, the
    // same as a written virtual destructor does.
    if (isVirtual) synthesizeDeleting(cls, type, Access::Public, pos);
}

// The copy assignment operator the class did not write - which is every class,
// since `operator` is refused by name until operator overloading arrives, so
// nothing can write one yet. The trivial line is drawn in the same place and
// was measured the same way: cl emits ??4Poly@@QEAAAEAU0@AEBU0@@Z for a
// polymorphic class, and nothing at all for a class of plain members, where
// `a = b` is the struct assignment this compiler has always emitted.
//
// A polymorphic class is non-trivial here even though the body does not touch
// the vptr. That is [class.copy] and it is what cl does; the vptr is not
// copied because assignment writes into an object that is already of this
// class.
void Parser::declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                       std::size_t pos) {
    if (overloadsOf(assignmentKey(tag)) != nullptr) return;

    // **A const member has no assignment to give**, so the operator the
    // compiler would write is deleted rather than non-trivial and none is
    // declared - which is what makes `a = b` say there is no such function
    // rather than quietly writing through a const. Asked over every member
    // before anything else, because the search below stops at the first
    // member that gives the operator work to do and a const one after it
    // would never be reached. A reference member is refused where it is
    // declared and cannot get this far.
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++)
        if (ms[i].type->isConst()) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyAssignOf(bs[i].type) != nullptr) work = true;
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyAssignOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *self = types_.referenceTo(type);
    const Type *fn = types_.functionType(self, params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftCopyAssignName(tag, type, fn, 'Q', &out, &why)
            : itaniumCopyAssignName(tag, type, fn, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy assignment the compiler would "
                       "write, and it cannot be given a name the linker can "
                       "hold: " + why);

    functionIndex_[assignmentKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ "operator=", out, self, params, false, false,
                                    pos, false, tag, false, Access::Public,
                                    false });
    functions_.back().implicit = true;
}

// The copy constructor the class did not write. The trivial/non-trivial line
// is the same one and measured the same way - cl emits `??0Poly@@QEAA@AEBU0@@Z`
// for a polymorphic class and nothing at all for a class of plain members -
// but what it is drawn on is different. A class writing *any* constructor
// still gets an implicit copy constructor; only writing a copy constructor
// takes it away.
//
// What makes one non-trivial: a virtual function, because the new object's
// vptr is its own and not a copy of the source's, or a base or member whose
// own copy constructor has to run.
void Parser::declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                     std::size_t pos) {
    if (copyConstructorOf(type) != nullptr) return;

    bool work = type->polymorphic();
    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size() && !work; i++)
        if (copyConstructorOf(bs[i].type) != nullptr) work = true;
    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size() && !work; i++)
        if (copyConstructorOf(memberClass(ms[i].type)) != nullptr) work = true;
    if (!work) return;

    std::vector<const Type *> params;
    params.push_back(types_.referenceTo(types_.withConst(type)));
    const Type *fn = types_.functionType(types_.get(Kind::Void), params, false);
    std::string out, why;
    const bool ok = target_.microsoftNames()
            ? microsoftConstructorName(tag, type, fn, 'Q', &out, &why)
            : itaniumConstructorName(tag, type, fn, true, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + tag + "' needs a copy constructor the compiler "
                       "would write, and it cannot be given a name the linker "
                       "can hold: " + why);

    functionIndex_[constructorKey(tag)].push_back(functions_.size());
    functions_.push_back(Signature{ tag, out, types_.get(Kind::Void), params,
                                    false, false, pos, false, tag, false,
                                    Access::Public, false });
    functions_.back().implicit = true;
}

// The body of a default constructor nobody wrote: the bases built in the order
// they were written, then the vptrs, then the members that have constructors
// of their own. Scalars are left alone, which is what [dcl.init] means by
// default-initialisation and what makes an uninitialised `int` member still
// uninitialised here.
void Parser::synthesizeDefaultCtor(std::size_t which) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    params.push_back(Param{ self, thisSlot });

    std::vector<StmtPtr> body;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        if (base->tag().empty()) continue;
        if (overloadsOf(constructorKey(base->tag())) == nullptr) continue;
        const Signature *ctor = defaultConstructorOf(base);
        if (ctor == nullptr)
            src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its base '" +
                           base->tag() + "', which has no constructor taking "
                           "nothing - write a constructor for '" + cls + "' with "
                           "': " + base->tag() + "(...)' in its initialiser list");
        if (ctor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of its base '" +
                           base->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        functions_[static_cast<std::size_t>(ctor - &functions_[0])].used = true;
        const std::string sym = baseConstructorSymbol(*ctor, base);

        const Type *basePtr = types_.pointerTo(base);
        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, types_.get(Kind::Void), ps,
                         false, pos, std::move(args)))));
    }

    if (type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        const Type *mc = memberClass(ms[i].type);
        if (mc == nullptr || mc->tag().empty()) continue;
        if (overloadsOf(constructorKey(mc->tag())) == nullptr) continue;
        const Signature *ctor = defaultConstructorOf(mc);
        if (ctor == nullptr)
            src_.fail(pos, "'" + cls + "' has no constructor of its own, and the "
                           "one the compiler would write cannot build its member '" +
                           ms[i].name + "': '" + mc->tag() + "' has no "
                           "constructor taking nothing");
        if (ctor->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be built by the constructor the "
                           "compiler would write: the constructor of '" +
                           mc->tag() + "' taking nothing is " +
                           (ctor->access == Access::Private ? "private"
                                                            : "protected"));
        functions_[static_cast<std::size_t>(ctor - &functions_[0])].used = true;

        // An array of them is built one element at a time, in order - a loop
        // rather than N calls, because N is a property of the type.
        int indexSlot = 0;
        long long count = 0;
        if (ms[i].type->isArray()) {
            count = ms[i].type->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the constructor the compiler would write does "
                               "not know how many elements to build");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr obj(new Unary('*', std::move(me)));
        obj->setType(type);
        ExprPtr acc(new MemberAccess(std::move(obj), ms[i].name, ms[i].offset));
        acc->setType(ms[i].type);

        ExprPtr addr;
        if (ms[i].type->isArray()) {
            addr = indexBytes(types_, decay(std::move(acc)), mc, indexSlot,
                              target_);
        } else {
            addr = ExprPtr(new Unary('&', std::move(acc)));
            addr->setType(types_.pointerTo(mc));
        }

        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        std::vector<const Type *> ps;
        ps.push_back(types_.pointerTo(mc));
        StmtPtr one(new ExprStmt(
            completeCall(mc->tag(), ctor->symbol, nullptr, types_.get(Kind::Void),
                         ps, false, pos, std::move(args))));
        body.push_back(ms[i].type->isArray()
                       ? eachElement(indexSlot, count, std::move(one))
                       : std::move(one));
    }

    current_->functions.push_back(Function(cls + "::" + cls, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    // The same two names a written constructor is emitted under: C1 for a
    // complete object and C2 for a base subobject, the second a label in front
    // of the first. Microsoft has one name and wants no alias.
    if (!target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(), false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why))
            current_->functions.back().setAlias(c2);
    }
    frameSize_ = savedFrame;
}

StmtPtr Parser::eachElement(int indexSlot, long long count, StmtPtr one) {
    const Type *idx = types_.intType();

    ExprPtr i0(Var::local("$i", indexSlot));
    i0->setType(idx);
    ExprPtr zero(new Num(0LL));
    zero->setType(idx);
    ExprPtr init(new Assign(std::move(i0), std::move(zero)));
    init->setType(idx);

    ExprPtr i1(Var::local("$i", indexSlot));
    i1->setType(idx);
    ExprPtr n(new Num(count));
    n->setType(idx);
    ExprPtr cond(new Binary(BinOp::Lt, std::move(i1), std::move(n)));
    cond->setType(idx);

    ExprPtr i2(Var::local("$i", indexSlot));
    i2->setType(idx);
    ExprPtr step1(new Num(1LL));
    step1->setType(idx);
    ExprPtr sum(new Binary(BinOp::Add, std::move(i2), std::move(step1)));
    sum->setType(idx);
    ExprPtr i3(Var::local("$i", indexSlot));
    i3->setType(idx);
    ExprPtr step(new Assign(std::move(i3), std::move(sum)));
    step->setType(idx);

    std::vector<StmtPtr> inner;
    inner.push_back(std::move(one));
    inner.push_back(StmtPtr(new ExprStmt(std::move(step))));

    std::vector<StmtPtr> all;
    all.push_back(StmtPtr(new ExprStmt(std::move(init))));
    all.push_back(StmtPtr(new While(std::move(cond),
                                    StmtPtr(new Block(std::move(inner))))));
    return StmtPtr(new Block(std::move(all)));
}


// The body of a copy constructor nobody wrote: the bases that have one of
// their own, then the vptrs, then every member that no base already copied.
//
// **The vptr is set and not copied**, which is the whole difference between
// this and the copy assignment beside it: a copy constructor is making a new
// object, and the new object is of *this* class whatever the source was.
// Measured, in cl's own listing: it stores `OFFSET FLAT:??_7Poly@@6B@` and
// then moves the members across.
//
// Members are copied one at a time rather than the whole object at once. A
// base subobject occupies its data size and not its sizeof, so a derived
// class may have put a member of its own in this class's tail padding - and a
// copy of `sizeof` bytes through the C2 form would take that member with it.
void Parser::synthesizeCopy(std::size_t which, bool assigning) {
    const std::string cls = functions_[which].owner;
    const std::size_t pos = functions_[which].pos;
    const std::string symbol = functions_[which].symbol;
    const Type *srcRef = functions_[which].params[0];
    const Type *type = findTypedef(cls);
    if (type == nullptr || !type->isStructOrUnion()) return;

    const int savedFrame = frameSize_;
    frameSize_ = 0;
    const Type *self = types_.pointerTo(type);
    const Type *srcPtr = types_.pointerTo(srcRef->referent());
    std::vector<Param> params;
    const int thisSlot = allocateFrameSlot(self);
    const int thatSlot = allocateFrameSlot(srcPtr);
    params.push_back(Param{ self, thisSlot });
    params.push_back(Param{ srcPtr, thatSlot });

    std::vector<StmtPtr> body;

    // What a base's own copy constructor has already dealt with. Its members
    // are in this class's member list too - they were copied down - and
    // copying them again would run past a base that did the work itself.
    std::vector<std::pair<int, int> > taken;

    const std::vector<Type::BaseSpec> &bs = type->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        const Type *base = bs[i].type;
        const Signature *cc = assigning ? copyAssignOf(base)
                                        : copyConstructorOf(base);
        if (cc == nullptr) continue;              // trivial: its members copy below
        if (cc->access != Access::Public)
            src_.fail(pos, "'" + cls + "' cannot be copied by the " +
                           (assigning ? "assignment" : "constructor") +
                           " the compiler would write: the copy " +
                           (assigning ? "assignment" : "constructor") +
                           " of its base '" + base->tag() + "' is " +
                           (cc->access == Access::Private ? "private" : "protected"));
        functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;
        const std::string sym = assigning ? cc->symbol
                                          : baseConstructorSymbol(*cc, base);
        const Type *basePtr = types_.pointerTo(base);

        ExprPtr me(Var::local("this", thisSlot));
        if (bs[i].offset == 0) {
            me->setType(basePtr);
        } else {
            me->setType(self);
            me = convert(std::move(me), basePtr);
        }
        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        from = convert(std::move(from), types_.pointerTo(types_.withConst(base)));
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(base);

        std::vector<ExprPtr> args;
        args.push_back(std::move(me));
        args.push_back(std::move(fromObj));
        std::vector<const Type *> ps;
        ps.push_back(basePtr);
        ps.push_back(cc->params[0]);
        body.push_back(StmtPtr(new ExprStmt(
            completeCall(base->tag(), sym, nullptr, cc->returns, ps,
                         false, pos, std::move(args)))));
        taken.push_back(std::make_pair(bs[i].offset,
                                       bs[i].offset + base->dataSize()));
    }

    // **A copy constructor sets the vptr; a copy assignment leaves it alone.**
    // That is the whole difference between the two bodies, and it is measured:
    // cl's ??0Poly stores OFFSET FLAT:??_7Poly@@6B@ before moving the members
    // and its ??4Poly moves the members and nothing else. The reason is that
    // assignment writes into an object that already exists and is already of
    // this class, where a constructor is making one.
    if (!assigning && type->polymorphic()) {
        std::vector<StmtPtr> vp = storeVptrs(cls, type, thisSlot);
        for (std::size_t i = 0; i < vp.size(); i++)
            body.push_back(std::move(vp[i]));
    }

    const std::vector<Member> &ms = type->members();
    for (std::size_t i = 0; i < ms.size(); i++) {
        bool done = false;
        for (std::size_t k = 0; k < taken.size() && !done; k++)
            if (ms[i].offset >= taken[k].first && ms[i].offset < taken[k].second)
                done = true;
        if (done) continue;

        const Type *mt = ms[i].type;
        const Type *elem = mt->isArray() ? mt->pointee() : mt;
        const Signature *cc = assigning ? copyAssignOf(memberClass(mt))
                                        : copyConstructorOf(memberClass(mt));
        if (cc != nullptr) {
            if (cc->access != Access::Public)
                src_.fail(pos, "'" + cls + "' cannot be copied by the " +
                               (assigning ? "assignment" : "constructor") +
                               " the compiler would write: the copy " +
                               (assigning ? "assignment" : "constructor") +
                               " of '" + memberClass(mt)->tag() + "', the type of '" +
                               ms[i].name + "', is " +
                               (cc->access == Access::Private ? "private"
                                                              : "protected"));
            functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;
        }

        int indexSlot = 0;
        long long count = 0;
        if (mt->isArray()) {
            count = mt->length();
            if (count < 0)
                src_.fail(pos, "'" + cls + "::" + ms[i].name + "' has no length, "
                               "so the copy constructor the compiler would write "
                               "does not know how much to copy");
            indexSlot = allocateFrameSlot(types_.intType());
        }

        // Both sides of the copy, as lvalues: this->m and that->m, or one
        // element of each when the member is an array.
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        ExprPtr meObj(new Unary('*', std::move(me)));
        meObj->setType(type);
        ExprPtr dst(new MemberAccess(std::move(meObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        dst->setType(mt);

        ExprPtr from(Var::local("that", thatSlot));
        from->setType(srcPtr);
        ExprPtr fromObj(new Unary('*', std::move(from)));
        fromObj->setType(srcRef->referent());
        ExprPtr src(new MemberAccess(std::move(fromObj), ms[i].name, ms[i].offset,
                                     ms[i].width, ms[i].bitOffset));
        src->setType(mt);

        if (mt->isArray()) {
            ExprPtr dstAt = indexBytes(types_, decay(std::move(dst)), elem,
                                       indexSlot, target_);
            ExprPtr srcAt = indexBytes(types_, decay(std::move(src)), elem,
                                       indexSlot, target_);
            dst = ExprPtr(new Unary('*', std::move(dstAt)));
            dst->setType(elem);
            src = ExprPtr(new Unary('*', std::move(srcAt)));
            src->setType(elem);
        }

        StmtPtr one;
        if (cc != nullptr) {
            ExprPtr addr(new Unary('&', std::move(dst)));
            addr->setType(types_.pointerTo(elem->unqualified()));
            std::vector<ExprPtr> args;
            args.push_back(std::move(addr));
            args.push_back(std::move(src));
            std::vector<const Type *> ps;
            ps.push_back(types_.pointerTo(elem->unqualified()));
            ps.push_back(cc->params[0]);
            one = StmtPtr(new ExprStmt(
                completeCall(elem->unqualified()->tag(), cc->symbol, nullptr,
                             cc->returns, ps, false, pos, std::move(args))));
        } else {
            ExprPtr store(new Assign(std::move(dst), std::move(src)));
            store->setType(elem);
            one = StmtPtr(new ExprStmt(std::move(store)));
        }

        body.push_back(mt->isArray() ? eachElement(indexSlot, count, std::move(one))
                                     : std::move(one));
    }

    // **`a = b` is an expression and has to have a value**, and the value is
    // the object assigned to. The declared return type is `X &`, and a
    // reference is a pointer everywhere below the parser - so what the
    // function actually returns is `this`.
    const Type *returns = types_.get(Kind::Void);
    if (assigning) {
        returns = types_.pointerTo(type);
        ExprPtr me(Var::local("this", thisSlot));
        me->setType(self);
        body.push_back(StmtPtr(new Return(std::move(me))));
    }

    current_->functions.push_back(Function(cls + "::" + (assigning ? "operator="
                                                                  : cls),
                                           returns, std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), false, 0,
                                           false, 0, pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(symbol);
    // A constructor is emitted under both of Itanium's names; an operator has
    // one name in either ABI.
    if (!assigning && !target_.microsoftNames()) {
        std::vector<const Type *> ps;
        ps.push_back(srcRef);
        const Type *fnType = types_.functionType(types_.get(Kind::Void), ps, false);
        std::string c2, why;
        if (itaniumConstructorName(cls, type, fnType, false, &c2, &why))
            current_->functions.back().setAlias(c2);
    }
    frameSize_ = savedFrame;
}

// **To a fixed point, because a body can be what first calls another.** Giving
// Owner its constructor is what calls Held's, and Held's may not have been
// wanted by anything the program wrote.
void Parser::defineImplicitFunctions() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < functions_.size(); i++) {
            if (!functions_[i].implicit || !functions_[i].used ||
                functions_[i].defined)
                continue;
            functions_[i].defined = true;
            if (!functions_[i].name.empty() && functions_[i].name[0] == '~')
                                                    synthesizeDestructor(i);
            else if (functions_[i].name == "operator=") synthesizeCopy(i, true);
            else if (functions_[i].params.empty())   synthesizeDefaultCtor(i);
            else                                    synthesizeCopy(i, false);
            again = true;
        }
    }
}

std::string Parser::staticMemberSymbol(const std::string &cls,
                                       const std::string &name, const Type *t,
                                       Access access, std::size_t pos) {
    if (!target_.microsoftNames()) return itaniumStaticMemberName(cls, findTypedef(cls), name);
    // Microsoft writes the access as a digit where a member function writes a
    // letter, so a static member that changes from private to public changes
    // its symbol on Windows and keeps it on Linux - the same asymmetry member
    // functions already have, measured the same way.
    const char code = access == Access::Public    ? '2'
                    : access == Access::Protected ? '1'
                                                  : '0';
    std::string out, why;
    if (!microsoftStaticMemberName(cls, findTypedef(cls), name, t, code, &out, &why))
        src_.fail(pos, "'" + cls + "::" + name + "' cannot be given a name the "
                       "linker can hold: " + why);
    return out;
}

// `static int total;` inside a class. It declares one object shared by every
// object of the class and takes no room in any of them, so nothing here
// touches the layout - what it needs is a name the linker can hold and a
// definition outside the class to go with it.
void Parser::declareStaticMember(const std::string &cls, Type *owner,
                                 const Declared &d, Access access) {
    if (cls.empty())
        src_.fail(d.pos, "a static member needs a class with a name - this one "
                         "is anonymous");
    if (owner->findMember(d.name) != nullptr)
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' is a static member and "
                         "an ordinary one, and it can only be one of them");
    for (const Type::StaticMember &had : owner->staticMembers())
        if (had.name == d.name)
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is declared twice");

    Type::StaticMember s;
    s.name = d.name;
    s.type = d.type;
    s.access = access;

    // **`static const int k = 5;` written in the class needs no definition**,
    // and that is measured rather than assumed: cl emits no symbol for one and
    // folds the value in wherever it is read. Anything else with an
    // initialiser here is refused, because the definition outside the class is
    // where the storage comes from and the value belongs with it.
    if (consume("=")) {
        if (!d.type->isConst() || !d.type->isInteger())
            src_.fail(d.pos, "'" + cls + "::" + d.name + "' is initialised "
                             "inside the class, and only a 'static const' of "
                             "integer type may be - write the value on the "
                             "definition outside the class instead");
        s.folded = true;
        s.value = constantExpression("a static member's value");
    } else if (d.type->isArray() && d.type->length() < 0) {
        src_.fail(d.pos, "'" + cls + "::" + d.name + "' has no length, and a "
                         "static member cannot take one from its definition - "
                         "the class is what says how big it is");
    }

    s.symbol = staticMemberSymbol(cls, d.name, d.type, access, d.pos);
    owner->addStaticMember(s);
}

// `int Counter::total = 0;` at file scope - the definition the declaration
// inside the class asked for. It is an ordinary global that the class gave its
// name to, so all this adds to the global path is finding which member it is
// and taking the symbol from it.
void Parser::defineStaticMember(Declared &d, Program &program) {
    const Type *owner = findTypedef(d.qualifier);
    if (owner == nullptr || !owner->isStructOrUnion())
        src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
    const Type::StaticMember *s = owner->findStaticMember(d.name);
    if (s == nullptr)
        src_.fail(d.pos, "'" + d.qualifier + "' declares no static member '" +
                         d.name + "'");
    if (s->type->unqualified() != d.type->unqualified() ||
        s->type->isConst() != d.type->isConst())
        src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' was declared '" +
                         s->type->describe() + "' and this defines it as '" +
                         d.type->describe() + "'");
    for (const Global &g : program.globals)
        if (g.symbol == s->symbol)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is defined "
                             "twice");

    // **A static member of class type has to be constructed before main**,
    // which is the mechanism a static local with a constructor needs and is
    // not here yet. Refused where the storage is made, which is the line that
    // has to change, rather than where it is read. A class with no
    // constructor is an aggregate and initialises like any other global.
    if (const Type *cls = memberClass(s->type))
        if (!cls->tag().empty() &&
            overloadsOf(constructorKey(cls->tag())) != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is a static "
                             "member of '" + cls->tag() + "', which has a "
                             "constructor - running one before main is not "
                             "supported yet");

    std::vector<GlobalPiece> pieces;
    bool hasInit = false;
    if (consume("=")) {
        Init in = parseInitialiser();
        flattenInit(s->type, in, 0, pieces);
        hasInit = true;
    }
    expect(";");

    program.globals.push_back(Global{ d.qualifier + "::" + d.name, s->symbol,
                                      s->type, std::move(pieces), hasInit, false,
                                      s->type->isConst() });
}

// Naming a static member, however it was reached. A folded one is its value
// and has no storage at all; every other is the one global the class named.
ExprPtr Parser::staticMemberRef(const Type *owner, const Type::StaticMember &s,
                                const std::string &cls, std::size_t pos) {
    if (s.access != Access::Public && currentClass_ != owner->unqualified())
        src_.fail(pos, "'" + cls + "::" + s.name + "' is " +
                       (s.access == Access::Private ? "private" : "protected"));
    if (s.folded) {
        ExprPtr n(new Num(s.value));
        n->setType(s.type);
        return n;
    }
    Var *v = Var::global(cls + "::" + s.name);
    v->setSymbol(s.symbol);
    ExprPtr n(v);
    n->setType(s.type);
    return n;
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

    // **A slot is taken once and then kept.** An override replaces the entry
    // the base put there rather than adding one, which is what makes a
    // Base * and a Derived * agree about where to look. Matching is by name,
    // parameters and constness - the signature, minus the return type, which
    // is what [class.virtual] calls overriding.
    //
    // **Finding that slot is itself what makes this function virtual.**
    // [class.virtual]: a function that overrides one is virtual whether or
    // not the keyword is written again, and the derived class's slots have
    // already come down from the base by the time any member is declared. So
    // the search runs before the name is built rather than after - the
    // Microsoft ABI spells a virtual member U and a plain one Q, and a
    // silently-non-virtual override would have been given the wrong name as
    // well as the wrong dispatch.
    std::vector<VSlot> &slots = vtables_[cls];
    std::size_t slot = slots.size();
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (slots[i].name != d.name || slots[i].constThis != constThis) continue;
        if (slots[i].params.size() != params.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < params.size(); k++)
            if (slots[i].params[k] != params[k]) { same = false; break; }
        if (!same) continue;
        slot = i;
        isVirtual = true;
        break;
    }

    const std::string symbol = memberSymbol(cls, d.name, fn, access, constThis,
                                            d.pos, isVirtual);
    set.push_back(functions_.size());
    functions_.push_back(Signature{
        d.name, symbol,
        fn->returns(), params, fn->isVariadicFn(), false, d.pos, false,
        cls, constThis, access, isVirtual });

    if (!isVirtual) return;
    if (slot < slots.size()) { slots[slot].symbol = symbol; return; }
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
            ? microsoftMemberName(cls, findTypedef(cls), name, fn, code, constThis, &out, &why)
            : itaniumMemberName(cls, findTypedef(cls), name, fn, constThis,
                                &out, &why);
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
    // While a specialization is being replayed the function it declares is
    // the specialization, keyed and mangled as "twice<int>". Its entry was
    // made when the call asked for it, so this finds that one and marks it
    // defined rather than computing a second symbol.
    const std::string &key = instantiationName(name);
    const bool cName = cLinkage_ > 0 || key == "main";
    std::vector<std::size_t> &set = functionIndex_[key];

    for (std::size_t k = 0; k < set.size(); k++) {
        Signature &f = functions_[set[k]];
        // A specialization sits in this list under the template's plain name
        // so that resolution can see it. It is not a declaration of that
        // name, so a function written with the same parameters is a new one.
        if (f.fromTemplate && instantiationKey_.empty()) continue;
        if (f.params.size() != params.size() || f.variadic != variadic) continue;
        bool same = true;
        for (std::size_t i = 0; i < params.size(); i++)
            if (f.params[i] != params[i]) { same = false; break; }
        if (!same) continue;

        if (f.returns != returns)
            src_.fail(pos, "'" + key + "' was declared to return '" +
                           f.returns->describe() + "' and this says '" +
                           returns->describe() + "' - two functions cannot "
                           "differ in the return type alone");
        if (defining) {
            if (f.defined) src_.fail(pos, "'" + key + "' is defined twice");
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
            src_.fail(pos, "'" + key + "' cannot be overloaded - " +
                           (key == "main" ? std::string("'main' is one function")
                                           : std::string("a name with C linkage "
                                             "carries one symbol")));
    }

    set.push_back(functions_.size());
    functions_.push_back(Signature{ key,
                                    functionSymbol(key, returns, params, variadic,
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
    if (const std::vector<std::size_t> *set = overloadsOf(instantiationName(name))) {
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

// **`Ts... rest` - one thing written, several parameters made.**
//
// In a pattern the pack stands for itself and this is one parameter of type
// `Ts...`, which is what Itanium spells `DpT0_` and says at every size. In a
// real instantiation it is as many parameters as the pack has members, named
// `rest$0`, `rest$1` - and those names are what `rest...` expands to at a
// call, which is the whole mechanism.
bool Parser::packParameter(std::vector<const Type *> *types,
                           std::vector<std::string> *names) {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("...")) return false;
    auto pk = packs_.find(peek().text);
    if (pk == packs_.end()) return false;

    const std::vector<const Type *> members = pk->second.types;
    const bool pattern = members.size() == 1 &&
                         members[0]->kind() == Kind::TemplateParam;
    at_ += 2;
    std::string base;
    if (peek().kind == TokenKind::Ident) { base = peek().text; at_++; }

    if (pattern) {
        types->push_back(types_.packExpansion(members[0]));
        if (names != nullptr) names->push_back(base);
        return true;
    }
    std::vector<std::string> made;
    for (std::size_t i = 0; i < members.size(); i++) {
        types->push_back(types_.withoutConst(members[i]));
        made.push_back(base + "$" + std::to_string(i));
        if (names != nullptr) names->push_back(made.back());
    }
    // Recorded under the *written* name as well, so `rest...` at a call and
    // `sizeof...(rest)` both find it beside `sizeof...(Ts)`.
    if (!base.empty()) {
        PackBinding pb;
        pb.types = members;
        pb.names = made;
        packs_[base] = pb;
    }
    pk->second.names = made;
    return true;
}

void Parser::parameterTypes(std::vector<const Type *> &params, bool &variadic) {
    expect("(");
    variadic = false;
    if (consume(")")) return;
    if (peek().is("void") && peekAt(1).is(")")) { at_ += 2; return; }

    for (;;) {
        if (consume("...")) { variadic = true; expect(")"); break; }
        if (packParameter(&params, nullptr)) {
            if (consume(")")) break;
            expect(",");
            continue;
        }
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
        "typeid", "using", "virtual",
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

    // A template named in an expression. A function template with its
    // arguments written is instantiated; everything else is refused by name,
    // and with the argument list stepped over first so that the reader is
    // told about the template rather than about the `<`.
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text))
        return templateCall(program);

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
        // Inside parentheses a `>` is an operator again, which is exactly why
        // C++ makes a comparison in a template argument need them.
        const bool wasInArgs = inTemplateArgs_;
        inTemplateArgs_ = false;
        ExprPtr e = expr();
        inTemplateArgs_ = wasInArgs;
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

    // **`Counter::total` - a static member named through its class.** Asked
    // before the identifier is read as a name of its own, and only taken when
    // the class really does have such a member, so `Point::get()` and anything
    // else spelled with a '::' falls through untouched.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident) {
        // The longest prefix that names a class and has such a member wins, so
        // that `Outer::Inner::shared` finds Inner's rather than stopping at
        // Outer.
        std::string q = peek().text;
        const Type *owner = nullptr;
        std::string member;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (const Type *cls = findTypedef(q))
                if (cls->isStructOrUnion() &&
                    cls->findStaticMember(component) != nullptr) {
                    owner = cls;
                    member = component;
                    consumed = k + 2;
                }
            q += "::" + component;
        }
        if (owner != nullptr) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            return staticMemberRef(owner, *owner->findStaticMember(member),
                                   owner->tag(), qpos);
        }
    }

    if (peek().kind == TokenKind::Ident) {
        std::string name = peek().text;
        std::size_t pos = peek().pos;

        const Local *l = findLocal(name);
        const GlobalSym *g = l != nullptr ? nullptr : findGlobal(name);
        const Type *held = l != nullptr ? l->type : (g != nullptr ? g->type : nullptr);
        bool callsThroughObject = held != nullptr && held->isFunctionPointer();

        // **An unqualified static member, inside a member function.** It needs
        // no object, which is what lets it be answered here rather than
        // through `this` the way an ordinary member is. A local or a global of
        // the same name is nearer and was already found above.
        if (l == nullptr && g == nullptr && currentClass_ != nullptr &&
            !peekAt(1).is("(")) {
            if (const Type::StaticMember *s =
                    currentClass_->findStaticMember(name)) {
                at_++;
                return staticMemberRef(currentClass_, *s, currentClass_->tag(),
                                       pos);
            }
        }

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
    // **A comma has the value category of its right operand** - [expr.comma],
    // and it is C++'s rule rather than C's, where the result is always a
    // value. It matters here because `b.count` for a static member is built
    // as one: the object is evaluated and thrown away and what the expression
    // names is the shared object, which can be assigned to and have its
    // address taken like any other.
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return isLvalue(c->right());
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
        // **`rest...` - one thing written, one argument per member.** The
        // names were made when the parameter list expanded, so this is a
        // lookup and not a substitution: whatever `rest$0` and `rest$1` are
        // now, that is what goes here.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            auto pk = packs_.find(peek().text);
            if (pk != packs_.end() && !pk->second.names.empty()) {
                at_ += 2;
                for (std::size_t i = 0; i < pk->second.names.size(); i++) {
                    ExprPtr one = objectRef(pk->second.names[i]);
                    if (one == nullptr)
                        src_.fail(peek().pos, "'" + pk->second.names[i] +
                                              "' went missing from this pack");
                    args.push_back(decay(std::move(one)));
                }
                if (consume(")")) break;
                expect(",");
                continue;
            }
            // An empty pack expands to no arguments at all.
            if (pk != packs_.end() && pk->second.types.empty()) {
                at_ += 2;
                if (consume(")")) break;
                expect(",");
                continue;
            }
        }
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

// The caller's half of passing a class by value: a temporary in this frame,
// the copy constructor run into it, and its address handed over. The whole
// thing is one expression - `(ctor(&tmp, arg), &tmp)` - so it needs no
// statement to sit in and works wherever a call does.
//
// The temporary belongs to the caller on the Itanium targets, which is also
// who destroys it. The Microsoft ABI puts that on the callee; see
// docs/CONFORMANCE.md, which records the difference and what it costs.
// The end of a full expression, where the temporaries it made are destroyed,
// in the reverse of the order they were made. The value has to be put
// somewhere first, because the destructors run between the expression and its
// value being used: `(r = <expr>, ~T(&tmp), r)`.
//
// Called from the places an expression becomes a statement or a condition. A
// site that forgets to call it does not lose the destructor - the temporary
// stays on the list and goes at the next full expression - so the failure
// mode is late rather than absent.
// The same end-of-full-expression rule where the expression has already
// become statements - a declaration's initialiser - so there is no value to
// carry past the destructors and they are simply appended.
void Parser::flushTemporaries(std::vector<StmtPtr> &into) {
    if (pendingTemps_.empty()) return;
    std::vector<std::pair<int, const Type *> > mine;
    mine.swap(pendingTemps_);
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].second);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].first));
        what->setType(mine[k].second);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].second));
        into.push_back(StmtPtr(new ExprStmt(
            destructorCall(std::move(at), *dtor, 0))));
    }
}

ExprPtr Parser::endFullExpression(ExprPtr e) {
    if (pendingTemps_.empty()) return e;
    std::vector<std::pair<int, const Type *> > mine;
    mine.swap(pendingTemps_);

    const Type *t = e->type();
    const bool hasValue = t != nullptr && !t->isVoid() && !t->isFunction();
    int keep = 0;
    if (hasValue) {
        keep = allocateFrameSlot(t);
        ExprPtr where(Var::local("$full", keep));
        where->setType(t);
        ExprPtr save(new Assign(std::move(where), std::move(e)));
        save->setType(t);
        e = std::move(save);
    }
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].second);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].first));
        what->setType(mine[k].second);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].second));
        ExprPtr gone = destructorCall(std::move(at), *dtor, 0);
        ExprPtr seq(new Comma(std::move(e), std::move(gone)));
        seq->setType(t);
        e = std::move(seq);
    }
    if (hasValue) {
        ExprPtr back(Var::local("$full", keep));
        back->setType(t);
        ExprPtr seq(new Comma(std::move(e), std::move(back)));
        seq->setType(t);
        e = std::move(seq);
    }
    return e;
}

ExprPtr Parser::materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                                const std::string &what,
                                std::vector<std::pair<int, const Type *> > &destroy) {
    const Type *cls = type->unqualified();
    const Signature *cc = copyConstructorOf(cls);

    // **A class that only has a destructor still goes by address on Itanium**,
    // and the copy the caller makes for it is a move of bytes rather than a
    // call - there is no copy constructor, because copying it is trivial. What
    // is not trivial is destroying it, which is why it travels this way at
    // all.
    if (cc == nullptr) {
        checkAssignable(*arg, cls, pos, what);
        const int plain = allocateFrameSlot(cls);
        const Type *to = types_.pointerTo(cls);
        if (destructorOf(cls) != nullptr)
            destroy.push_back(std::make_pair(plain, cls));

        ExprPtr slot(Var::local("$copy", plain));
        slot->setType(cls);
        ExprPtr store(new Assign(std::move(slot), std::move(arg)));
        store->setType(cls);

        ExprPtr again(Var::local("$copy", plain));
        again->setType(cls);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(to);

        ExprPtr node(new Comma(std::move(store), std::move(at)));
        node->setType(to);
        return node;
    }
    if (cc->access != Access::Public && currentClass_ != cls)
        src_.fail(pos, "'" + cls->describe() + "' is passed by value as " + what +
                       ", which copies it, and its copy constructor is " +
                       (cc->access == Access::Private ? "private" : "protected"));
    checkAssignable(*arg, cls, pos, what);

    const int tmp = allocateFrameSlot(cls);
    const Type *ptr = types_.pointerTo(cls);
    if (destructorOf(cls) != nullptr)
        destroy.push_back(std::make_pair(tmp, cls));

    // **Elision, where the argument is already one of these coming back
    // through a hidden pointer.** The call can build its result straight into
    // the temporary this argument needs, and then no copy constructor runs at
    // all - which is what clang does at -O0 and what C++11 permits.
    if (Call *made = dynamic_cast<Call *>(arg.get())) {
        if (made->type() == cls && returnsIndirectly(cls)) {
            made->setResultSlot(tmp);
            ExprPtr built(Var::local("$copy", tmp));
            built->setType(cls);
            ExprPtr at(new Unary('&', std::move(built)));
            at->setType(ptr);
            ExprPtr node(new Comma(std::move(arg), std::move(at)));
            node->setType(ptr);
            return node;
        }
    }

    functions_[static_cast<std::size_t>(cc - &functions_[0])].used = true;

    ExprPtr slot(Var::local("$copy", tmp));
    slot->setType(cls);
    ExprPtr addr(new Unary('&', std::move(slot)));
    addr->setType(ptr);

    std::vector<ExprPtr> ctorArgs;
    ctorArgs.push_back(std::move(addr));
    ctorArgs.push_back(std::move(arg));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    ps.push_back(cc->params[0]);
    ExprPtr build = completeCall(cls->tag(), cc->symbol, nullptr,
                                 types_.get(Kind::Void), ps, false, pos,
                                 std::move(ctorArgs));

    ExprPtr again(Var::local("$copy", tmp));
    again->setType(cls);
    ExprPtr result(new Unary('&', std::move(again)));
    result->setType(ptr);

    ExprPtr node(new Comma(std::move(build), std::move(result)));
    node->setType(ptr);
    return node;
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

    // Temporaries this call makes for its by-value class arguments, and which
    // this call therefore has to destroy once it returns.
    std::vector<std::pair<int, const Type *> > destroy;

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
        // **A class whose copy is a constructor call is copied by the
        // caller**, into a temporary the caller owns, and what the callee
        // receives is that temporary's address. Measured on all three
        // targets: clang and cl each emit the copy constructor at the call
        // site and then pass a pointer.
        if (passedByAddress(params[i])) {
            args[i] = materialiseCopy(params[i], std::move(args[i]), pos, what,
                                      destroy);
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

    // **The caller destroys the copies it made** - measured from clang, which
    // emits the destructor of the argument temporary in the caller. The
    // Microsoft ABI puts that on the callee instead; docs/CONFORMANCE.md has
    // the difference and what it costs. They are handed to the full
    // expression rather than destroyed here, because that is when the
    // standard says they go.
    if (!target_.microsoftNames())
        for (std::size_t k = 0; k < destroy.size(); k++)
            pendingTemps_.push_back(destroy[k]);

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
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
                // [expr.ref] evaluates the object expression even though what
                // the whole thing names is the one shared object. Where that
                // expression is pure there is nothing to evaluate, and
                // dropping it leaves an ordinary lvalue rather than a comma -
                // which is what `b.count = 1` and `&b.count` need.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
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
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
                // [expr.ref] evaluates the object expression even though what
                // the whole thing names is the one shared object. Where that
                // expression is pure there is nothing to evaluate, and
                // dropping it leaves an ordinary lvalue rather than a comma -
                // which is what `b.count = 1` and `&b.count` need.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
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

    // `sizeof...(Ts)` and `sizeof...(args)` - how many, not how big. Both
    // spellings name the same pack: the parameter and the function parameter
    // made from it are one entry here.
    if (peek().is("sizeof") && peekAt(1).is("...")) {
        const std::size_t spos = peek().pos;
        at_ += 2;
        expect("(");
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'sizeof...' takes the name of a parameter "
                                  "pack");
        auto pack = packs_.find(peek().text);
        if (pack == packs_.end())
            src_.fail(peek().pos, "'" + peek().text + "' is not a parameter "
                                  "pack");
        const long long n = static_cast<long long>(pack->second.types.size());
        at_++;
        expect(")");
        (void)spos;
        ExprPtr n2(new Num(n));
        n2->setType(types_.get(target_.sizeType()));
        return n2;
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
        // **A signature that depends on its parameters through an
        // *expression* cannot be given a name.** Itanium spells a
        // specialization's return type from the pattern, and a pattern
        // holding `sizeof(T) == 4` is spelled as the expression itself -
        // `N9enable_ifIXeqstT_Li4EEiE4typeE`, measured with clang. Nothing
        // here can write that, so it is refused where it is written rather
        // than left to reach a type that has no size.
        if (patternOnly_ && (measured->kind() == Kind::TemplateParam ||
                             measured->kind() == Kind::DependentMember))
            src_.fail(pos, "'sizeof' of a template parameter in a signature is "
                           "not supported yet - the linker name would have to "
                           "spell the expression, and that is its own step");
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
ExprPtr Parser::runtimeCall(const char *symbol, const Type *returns,
                            std::vector<ExprPtr> args) {
    std::vector<int> argSlots(args.size(), 0);
    Call *call = new Call(symbol, nullptr, std::move(args), false, 0,
                          static_cast<int>(argSlots.size()),
                          std::move(argSlots));
    call->setSymbol(symbol);
    ExprPtr n(call);
    n->setType(returns);
    return n;
}

// **`throw x;` is three calls and a store, and no new machinery.**
//
//     void *e = __cxa_allocate_exception(sizeof x);
//     *(T *)e = x;
//     __cxa_throw(e, &_ZTI<T>, 0);
//
// The Itanium ABI puts the object in memory the runtime owns, hands it over
// with the type that identifies it, and never returns. Written as one comma
// expression so that it is a statement wherever an expression is one.
//
// **The type_info pointer is the whole of the work.** cxx1 has no RTTI: the
// vtable's typeinfo slot is a plain zero and `typeid` is refused. For a
// *fundamental* type the object is already in the standard library and naming
// it is enough, which is why this rung starts there and refuses everything
// else by name.
StmtPtr Parser::throwStatement(ExprPtr value, std::size_t pos) {
    const Type *thrown = value->type()->unqualified();
    std::string info, why;
    if (!itaniumTypeInfoName(thrown, &info, &why))
        src_.fail(pos, "'throw' cannot name the type of this: " + why);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int slot = allocateFrameSlot(voidPtr);
    const std::string temp = ".ex" + std::to_string(refTemps_++);

    std::vector<ExprPtr> sizeArg;
    ExprPtr howBig(new Num(static_cast<long long>(thrown->size(target_))));
    howBig->setType(types_.get(target_.sizeType()));
    sizeArg.push_back(std::move(howBig));
    ExprPtr got = runtimeCall("__cxa_allocate_exception", voidPtr,
                              std::move(sizeArg));

    ExprPtr held(Var::local(temp, slot));
    held->setType(voidPtr);
    ExprPtr save(new Assign(std::move(held), std::move(got)));
    save->setType(voidPtr);

    const Type *thrownPtr = types_.pointerTo(thrown);
    ExprPtr asT(Var::local(temp, slot));
    asT->setType(voidPtr);
    ExprPtr cast(new Cast(thrownPtr, std::move(asT)));
    cast->setType(thrownPtr);
    ExprPtr into(new Unary('*', std::move(cast)));
    into->setType(thrown);
    ExprPtr store(new Assign(std::move(into), convert(std::move(value), thrown)));
    store->setType(thrown);

    // The exception object, the type that identifies it, and the destructor
    // it does not have. A fundamental type needs none, so the third argument
    // is the null the ABI asks for there.
    std::vector<ExprPtr> throwArgs;
    ExprPtr object(Var::local(temp, slot));
    object->setType(voidPtr);
    throwArgs.push_back(std::move(object));

    Var *ti = Var::global(info);
    ti->setSymbol(info);
    ExprPtr tiRef(ti);
    tiRef->setType(types_.get(Kind::Char));
    ExprPtr tiAddr(new Unary('&', std::move(tiRef)));
    tiAddr->setType(voidPtr);
    throwArgs.push_back(std::move(tiAddr));

    ExprPtr none(new Num(0LL));
    none->setType(voidPtr);
    throwArgs.push_back(std::move(none));

    ExprPtr thrower = runtimeCall("__cxa_throw", types_.get(Kind::Void),
                                  std::move(throwArgs));

    ExprPtr first(new Comma(std::move(save), std::move(store)));
    first->setType(thrown);
    ExprPtr whole(new Comma(std::move(first), std::move(thrower)));
    whole->setType(types_.get(Kind::Void));
    return StmtPtr(new ExprStmt(std::move(whole)));
}

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
    const Signature &sig = resolveOverload(key, args, pos, cls);

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
        if (inTemplateArgs_ && peek().is(">>")) return n;
        if (consume("<<"))      op = BinOp::Shl;
        else if (consume(">>")) op = BinOp::Shr;
        else return n;

        n = shiftOf(op, std::move(n), add());
    }
}

ExprPtr Parser::relational() {
    ExprPtr n = shift();
    for (;;) {
        // [temp.names]: a `>` inside a template argument list closes it. This
        // is the whole reason C++ makes `f<(a > b)>` need its parentheses,
        // and the parentheses are where the flag is cleared.
        if (inTemplateArgs_ && (peek().is(">") || peek().is(">>"))) return n;
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

    // **A class with a copy assignment of its own is assigned by calling it**,
    // not by moving its bytes. Where the copy is trivial there is no such
    // function and nothing was declared, and this is the struct assignment it
    // has always been.
    if (const Signature *op = copyAssignOf(to->unqualified())) {
        functions_[static_cast<std::size_t>(op - &functions_[0])].used = true;
        const Type *selfPtr = types_.pointerTo(to->unqualified());
        ExprPtr addr(new Unary('&', std::move(n)));
        addr->setType(selfPtr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        args.push_back(std::move(value));
        std::vector<const Type *> ps;
        ps.push_back(selfPtr);
        ps.push_back(op->params[0]);
        ExprPtr call = completeCall(to->unqualified()->tag() + "::operator=",
                                    op->symbol, nullptr, selfPtr, ps, false, pos,
                                    std::move(args));
        // It answers `X &`, which is a pointer below the parser - so what the
        // expression yields is that pointer read back, and `(a = b).m` goes on
        // working the way it does for a written assignment.
        ExprPtr result(new Unary('*', std::move(call)));
        result->setType(to->unqualified());
        return result;
    }

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
            } else if (consume("=")) {
                // **Copy-initialisation.** `X b = a;` is a constructor called
                // with one argument, chosen by the ordinary overload rules -
                // the copy constructor for an `X`, a converting constructor
                // for anything else. What separates it from `X b(a);` in the
                // standard is that an `explicit` constructor may not be picked
                // here, and `explicit` is refused by name until rung 7.
                args.push_back(assign());
            }

            int off = declare(d.name, d.type, d.pos);

            // **Copy elision, in the one case worth having it.** When the
            // initialiser is a call that already returns one of these through
            // a hidden pointer, the object is built straight into this
            // variable and no copy constructor runs at all. clang does this at
            // -O0 on both Itanium targets; cl at /O0 makes the copy instead,
            // and C++11 permits either - which is why a case that counts
            // constructor calls cannot have one recorded output for all three
            // machines, and why the suite's cases do not count them.
            Call *made = args.size() == 1 && d.type->nonTrivialCopy()
                       ? dynamic_cast<Call *>(args[0].get()) : nullptr;

            // **A trivial copy, in a class that does have constructors.** No
            // copy constructor was declared for it, because copying it is a
            // move of bytes and cl and clang both emit no function for one -
            // so there is nothing for overload resolution to find, and what
            // the standard asks for here is those bytes.
            const bool trivialCopy =
                args.size() == 1 &&
                copyConstructorOf(d.type->unqualified()) == nullptr &&
                args[0]->type() != nullptr &&
                args[0]->type()->unqualified() == d.type->unqualified();

            if (made != nullptr && made->type() == d.type &&
                returnsIndirectly(d.type)) {
                made->setResultSlot(off);
                inits.push_back(StmtPtr(new ExprStmt(std::move(args[0]))));
            } else if (trivialCopy) {
                ExprPtr target(Var::local(d.name, off));
                target->setType(d.type);
                ExprPtr store(new Assign(std::move(target), std::move(args[0])));
                store->setType(d.type);
                inits.push_back(StmtPtr(new ExprStmt(std::move(store))));
            } else {
                inits.push_back(constructLocal(d, off, std::move(args)));
            }
            flushTemporaries(inits);
            if (destructorOf(d.type) != nullptr)
                alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
            if (!consume(",")) break;
            continue;
        }

        // **`X q(p);` where X has no constructor at all.** Its copy is
        // trivial, so there is no constructor to call and none was declared -
        // what the standard asks for here is the bytes, which is the struct
        // assignment the backends already emit. This is the lowering trade
        // again: an operation that exists is cheaper than a fourth thing for
        // three code generators to know about.
        //
        // A parameter list begins with a type name and this does not, which is
        // what tells `X q(p);` from a function declaration. The same question
        // is asked above for a class that does have constructors; here it is
        // asked the other way round because there is no overload set to
        // resolve against.
        if (peek().is("(") && d.type->isStructOrUnion() && sc != StorageStatic) {
            const std::size_t save = at_;
            at_++;
            const bool looksLikeParameters = peek().is(")") || atDeclarationStart();
            if (!looksLikeParameters) {
                std::vector<ExprPtr> args;
                parseArguments(args);
                if (args.size() != 1)
                    src_.fail(d.pos, "'" + d.type->describe() + "' has no "
                                     "constructor, so '" + d.name + "(...)' can "
                                     "only be a copy of another '" +
                                     d.type->describe() + "' - and this gives " +
                                     std::to_string(args.size()) + " arguments");
                checkAssignable(*args[0], d.type, d.pos, "'" + d.name + "'");
                const int off = declare(d.name, d.type, d.pos);
                ExprPtr target(Var::local(d.name, off));
                target->setType(d.type);
                ExprPtr store(new Assign(std::move(target), std::move(args[0])));
                store->setType(d.type);
                inits.push_back(StmtPtr(new ExprStmt(std::move(store))));
                if (destructorOf(d.type) != nullptr)
                    alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
                if (!consume(",")) break;
                continue;
            }
            at_ = save;
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

        const int off = declare(d.name, d.type, d.pos);
        locals_.back().isConst = d.type->isConst();
        locals_.back().isRegister = (sc == StorageRegister);

        // **An object with a destructor is alive from here**, whether or not
        // it had a constructor to run. A class can have one and not the
        // other, and before implicit destructors existed nothing but the
        // constructor path ever added to this list - so a class with a member
        // that needed destroying and no constructor of its own was destroyed
        // by nobody.
        if (destructorOf(d.type) != nullptr)
            alive_.push_back(Alive{ d.name, off, d.type->unqualified() });

        if (hasInit) {
            std::vector<InitStep> path;
            emitInit(d.name, path, d.type, in, inits);
        }
        flushTemporaries(inits);
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
        else { ExprPtr e = endFullExpression(expr()); expect(";"); init = StmtPtr(new ExprStmt(std::move(e))); }
    }

    ExprPtr cond;
    if (!peek().is(";")) cond = endFullExpression(decay(expr()));
    expect(";");

    ExprPtr step;
    if (!peek().is(")")) step = endFullExpression(decay(expr()));
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
    ExprPtr cond = endFullExpression(decay(expr()));
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
    // **Where each object became alive**, as a statement index and how many
    // objects were alive after it. A cleanup region runs from one of these to
    // the next, and destroys exactly what was built by then - which is why
    // the ranges are split rather than one region for the whole block: an
    // exception thrown before the second object exists must not destroy it.
    std::vector<std::pair<std::size_t, std::size_t> > built;
    std::vector<StmtPtr> body;
    while (!peek().is("}")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "unclosed '{'");
        const std::size_t aliveBefore = alive_.size();
        body.push_back(atDeclarationStart() ? declaration() : statement());
        if (alive_.size() > aliveBefore)
            built.push_back(std::make_pair(body.size(), alive_.size()));
    }

    // Everything this block constructed is destroyed here, last first. The
    // objects are found by where they are in `alive_` rather than by walking
    // the block again: what a scope built is exactly what it added.
    emitDestructors(body, aliveAtEntry, peek().pos);

    // **Windows makes no cleanup regions and needs none.** `throw` is refused
    // for that target, so nothing can unwind through one of its frames, and
    // the destructors on the normal path are unchanged. Making them anyway
    // would put a landing pad in a backend whose tables cannot describe one.
    if (!built.empty() && !target_.microsoftNames()) {
        if (functionHasTry_ || inTryBody_)
            src_.fail(pos, "a local with a destructor and a 'try' in one "
                           "function is not supported yet - each is a range in "
                           "the call-site table and one would have to split "
                           "the other");
        body = wrapCleanups(std::move(body), built, aliveAtEntry, pos);
    }
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

// **`try` is a block, a landing pad, and no new statement machinery.**
//
// The pad is where the runtime arrives, and everything from there on is built
// here out of nodes that already existed: the selector the personality
// routine chose is compared against 1, 2, 3 - the order the handlers are
// written in, which is the order their types go into the table - and each arm
// is `__cxa_begin_catch`, a copy into the caught variable, the handler's own
// body, and `__cxa_end_catch`. What no handler matches falls through to
// `_Unwind_Resume`, which is what "this frame does not want it after all"
// means.
//
// The chain is nested if/else rather than labels and jumps, because that is a
// shape the backends already walk.
StmtPtr Parser::tryStatement(std::size_t pos) {
    if (target_.microsoftNames())
        src_.fail(pos, "'try' is not supported yet for x86_64-windows - the "
                       "Microsoft ABI's tables are a different design and not "
                       "a different spelling of this one");
    functionHasTry_ = true;
    if (inTryBody_)
        src_.fail(pos, "a 'try' inside another one is not supported yet - the "
                       "call-site table holds sorted ranges that do not "
                       "overlap, and a nested one has to split its parent");

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int pointerSlot = allocateFrameSlot(voidPtr);
    const int selectorSlot = allocateFrameSlot(types_.intType());
    functionHasPads_ = true;

    const bool wasInTry = inTryBody_;
    inTryBody_ = true;
    if (!peek().is("{"))
        src_.fail(peek().pos, "'try' takes a block");
    StmtPtr body = block();
    inTryBody_ = wasInTry;

    if (!peek().is("catch"))
        src_.fail(peek().pos, "a 'try' needs at least one 'catch'");

    // Read the handlers innermost-last, so the chain can be built from the
    // bottom: what nothing matches is _Unwind_Resume, and each handler wraps
    // what came before it as its else.
    struct Handler {
        std::string type;         // the _ZTI symbol, empty for catch (...)
        StmtPtr stmt;
    };
    std::vector<Handler> handlers;
    std::vector<int> indices;
    std::vector<std::string> types;
    bool sawCatchAll = false;

    while (peek().is("catch")) {
        const std::size_t cpos = peek().pos;
        at_++;
        expect("(");
        if (sawCatchAll)
            src_.fail(cpos, "'catch (...)' matches everything, so a handler "
                            "after it could never run");

        Handler h;
        std::string caughtName;
        const Type *caught = nullptr;
        if (consume("...")) {
            sawCatchAll = true;
        } else {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            if (d.type->isReference())
                src_.fail(d.pos, "catching by reference is not supported yet - "
                                 "catch by value");
            std::string why;
            if (!itaniumTypeInfoName(d.type->unqualified(), &h.type, &why))
                src_.fail(cpos, "'catch' cannot name this type: " + why);
            caught = d.type->unqualified();
            caughtName = d.name;
        }
        expect(")");
        types.push_back(h.type);
        indices.push_back(++functionTypeIndex_);

        // The handler's own scope, holding the caught object if it was named.
        enterScope();
        const int scope = enterBlock();
        std::vector<StmtPtr> steps;

        std::vector<ExprPtr> beginArgs;
        ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
        ptr->setType(voidPtr);
        beginArgs.push_back(std::move(ptr));
        ExprPtr began = runtimeCall("__cxa_begin_catch", voidPtr,
                                    std::move(beginArgs));

        if (caught != nullptr && !caughtName.empty()) {
            const int slot = declare(caughtName, caught, cpos);
            const Type *caughtPtr = types_.pointerTo(caught);
            ExprPtr cast(new Cast(caughtPtr, std::move(began)));
            cast->setType(caughtPtr);
            ExprPtr from(new Unary('*', std::move(cast)));
            from->setType(caught);
            ExprPtr to(Var::local(caughtName, slot));
            to->setType(caught);
            ExprPtr copy(new Assign(std::move(to), std::move(from)));
            copy->setType(caught);
            steps.push_back(StmtPtr(new ExprStmt(std::move(copy))));
        } else {
            steps.push_back(StmtPtr(new ExprStmt(std::move(began))));
        }

        if (!peek().is("{")) src_.fail(peek().pos, "'catch' takes a block");
        steps.push_back(block());

        ExprPtr ended = runtimeCall("__cxa_end_catch", types_.get(Kind::Void),
                                    std::vector<ExprPtr>());
        steps.push_back(StmtPtr(new ExprStmt(std::move(ended))));
        leaveScope();
        Block *b = new Block(std::move(steps));
        b->setScope(scope);
        h.stmt = StmtPtr(b);
        handlers.push_back(std::move(h));
    }

    // Nothing matched: hand it back to the unwinder.
    std::vector<ExprPtr> resumeArgs;
    ExprPtr again(Var::local(".ex.ptr", pointerSlot));
    again->setType(voidPtr);
    resumeArgs.push_back(std::move(again));
    StmtPtr chain(new ExprStmt(runtimeCall("_Unwind_Resume",
                                           types_.get(Kind::Void),
                                           std::move(resumeArgs))));

    for (std::size_t i = handlers.size(); i-- > 0; ) {
        if (handlers[i].type.empty()) {          // catch (...) matches always
            chain = std::move(handlers[i].stmt);
            continue;
        }
        ExprPtr sel(Var::local(".ex.sel", selectorSlot));
        sel->setType(types_.intType());
        ExprPtr want(new Num(static_cast<long long>(indices[i])));
        want->setType(types_.intType());
        ExprPtr test(new Binary(BinOp::Eq, std::move(sel), std::move(want)));
        test->setType(types_.intType());
        chain = StmtPtr(new If(std::move(test), std::move(handlers[i].stmt),
                               std::move(chain)));
    }

    std::vector<StmtPtr> guarded;
    guarded.push_back(std::move(body));
    return StmtPtr(new Try(std::move(guarded), std::move(chain), pointerSlot,
                           selectorSlot, std::move(types)));
}

StmtPtr Parser::statementBody() {
    if (peek().is("try")) {
        const std::size_t tpos = peek().pos;
        at_++;
        return tryStatement(tpos);
    }

    if (peek().is("throw")) {
        const std::size_t tpos = peek().pos;
        at_++;
        if (peek().is(";"))
            src_.fail(tpos, "a rethrow - 'throw' with nothing after it - is "
                            "not supported yet");
        // **Windows lags here and the reason is a shape, not an omission.**
        // The Microsoft ABI hands _CxxThrowException a ThrowInfo, which
        // points at a catchable-type array, which points at a copy record,
        // which points at the RTTI descriptor - four objects and an
        // image-relative relocation, where Itanium wants one pointer.
        if (target_.microsoftNames())
            src_.fail(tpos, "'throw' is not supported yet for x86_64-windows - "
                            "the Microsoft ABI wants a ThrowInfo chain this "
                            "compiler does not emit");
        ExprPtr value = decay(expr());
        expect(";");
        return throwStatement(std::move(value), tpos);
    }

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
        ExprPtr value = endFullExpression(returnType_->isReference() ? expr()
                                                                  : decay(expr()));
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
        // **The object being returned is not destroyed here.** Returning a
        // local by value puts it in the caller's storage, and the caller
        // destroys it there; running the local's destructor as well would
        // destroy the same object twice - once here and once in the caller -
        // which for a class that owns anything is a double free.
        //
        // That is copy elision, which [class.copy] permits and clang takes at
        // -O0 where cl does not. Taking it is what makes the two consistent:
        // the bytes go straight to the caller's storage with no copy
        // constructor called, and a copy that was not made must not be
        // destroyed either.
        int elided = -1;
        if (returnType_->isStructOrUnion() &&
            destructorOf(returnType_) != nullptr)
            if (const Var *v = dynamic_cast<const Var *>(value.get()))
                if (v->isLocal()) elided = v->offset();

        if (!alive_.empty()) {
            std::vector<StmtPtr> unwind;
            int slot = allocateFrameSlot(returnType_);
            std::string temp = ".ret" + std::to_string(refTemps_++);

            ExprPtr keep(Var::local(temp, slot));
            keep->setType(returnType_);
            ExprPtr save(new Assign(std::move(keep), std::move(value)));
            save->setType(returnType_);
            unwind.push_back(StmtPtr(new ExprStmt(std::move(save))));

            emitDestructors(unwind, 0, pos, elided);

            ExprPtr give(Var::local(temp, slot));
            give->setType(returnType_);
            unwind.push_back(StmtPtr(new Return(std::move(give))));
            return StmtPtr(new Block(std::move(unwind)));
        }
        return StmtPtr(new Return(std::move(value)));
    }
    if (consume("if")) {
        expect("(");
        ExprPtr cond = endFullExpression(decay(expr()));
        expect(")");
        StmtPtr thenArm = statement();
        StmtPtr elseArm;
        if (consume("else")) elseArm = statement();
        return StmtPtr(new If(std::move(cond), std::move(thenArm), std::move(elseArm)));
    }
    if (consume("while")) {
        expect("(");
        ExprPtr cond = endFullExpression(decay(expr()));
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
        ExprPtr cond = endFullExpression(decay(expr()));
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

    ExprPtr e = endFullExpression(expr());
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
    if (templateDeclaration()) return;

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

    // `int Counter::total = 0;` - a static member's definition. A member
    // *function*'s definition is spelled the same way up to here and is told
    // apart by the '(' that follows, which is the same question the class body
    // asks about a member.
    if (!d.qualifier.empty() && !peek().is("(") && d.paramsAt == 0 &&
        !d.type->isFunction()) {
        defineStaticMember(d, program);
        return;
    }

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
    std::size_t aliveParams = 0;

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

                // `Ts... rest` in a definition: as many parameters as the
                // pack has members, each with a name of its own, and those
                // names are what `rest...` expands to at a call.
                {
                    std::vector<const Type *> packTypes;
                    std::vector<std::string> packNames;
                    if (packParameter(&packTypes, &packNames)) {
                        for (std::size_t k = 0; k < packTypes.size(); k++) {
                            inParams_ = true;
                            int poff = declare(packNames[k], packTypes[k],
                                               peek().pos);
                            inParams_ = false;
                            params.push_back(types_.withoutConst(packTypes[k]));
                            paramSlots.push_back(Param{ packTypes[k], poff });
                        }
                        if (consume(")")) break;
                        expect(",");
                        continue;
                    }
                }

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

                // **A class whose copy is a constructor call arrives by
                // address**, on both ABIs and whatever its size - measured
                // with cl and with clang for both Itanium targets. So the
                // parameter is *lowered to a reference*: its frame slot holds
                // the caller's pointer, and every mention of it dereferences
                // that, which is the machinery a reference already has and
                // which no backend had to be told about.
                //
                // The declared type is untouched - `params` still says the
                // class - so the mangler and overload resolution go on seeing
                // a parameter passed by value, which is what it is.
                //
                // The object itself belongs to the caller: it built the copy
                // and it destroys it. That is the Itanium rule; the Microsoft
                // ABI has the callee destroy its parameter instead, which is
                // in docs/CONFORMANCE.md as a difference that only shows when
                // an object of cxx1's is linked with one of cl's.
                const bool byAddress = passedByAddress(pd.type);
                const Type *held = byAddress ? types_.referenceTo(pd.type)
                                             : pd.type;
                int off;
                if (pd.name.empty()) {
                    if (pd.type->isVoid())
                        src_.fail(pd.pos, "'void' is only a parameter list on its own");
                    unnamedParam = pd.pos;
                    sawUnnamed = true;
                    off = 0;
                } else {
                    inParams_ = true;
                    off = declare(pd.name, held, pd.pos);
                    inParams_ = false;
                    locals_.back().isConst = pd.type->isConst();
                    locals_.back().isRegister = (psc == StorageRegister);

                    // **On Microsoft the callee destroys its by-value class
                    // parameter**, whether it arrived in a register or as the
                    // address of a copy the caller made - measured with cl,
                    // whose ?useSmall calls ??1Small on its own parameter.
                    // Itanium puts that on the caller instead, which is where
                    // the temporary is made.
                    if (target_.microsoftNames() && pd.type->isStructOrUnion() &&
                        pd.type->hasDestructor()) {
                        alive_.push_back(Alive{ pd.name, off,
                                                pd.type->unqualified(),
                                                byAddress });
                        aliveParams++;
                    }
                }
                params.push_back(types_.withoutConst(pd.type));
                paramSlots.push_back(Param{ held->isReference()
                                            ? types_.pointerTo(held->referent())
                                            : held, off });
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
    // The mem-initializer list, [class.base.init]. Parsed here because `this`
    // and the parameters are in scope and the body has not begun - which is
    // exactly where the ':' sits in the grammar.
    //
    // What each entry may name: a non-static member, or a DIRECT base. The
    // members become assignments through `this`; a base's arguments are kept
    // for the chaining loop below, which is what actually calls its
    // constructor. **Emission follows declaration order, not list order** -
    // [class.base.init]/11 initialises in declaration order whatever the list
    // says, and an emitter that followed the list would make the program mean
    // something the standard says it does not.
    std::vector<StmtPtr> memberInits;
    std::map<std::string, std::vector<ExprPtr> > baseArgs;
    const bool isCtor = memberOf != nullptr && d.name == localOf(d.qualifier);
    if (memberOf != nullptr && peek().is(":")) {
        if (!isCtor)
            src_.fail(peek().pos, "an initialiser list belongs to a "
                                  "constructor, and '" + d.name + "' is not one");
        at_++;
        std::map<std::string, std::vector<ExprPtr> > memberExprs;
        std::map<std::string, std::size_t> where;
        for (;;) {
            std::size_t epos = peek().pos;
            std::string entry = expectIdent("a member or base to initialise");
            expect("(");
            std::vector<ExprPtr> args;
            parseArguments(args);

            bool isBase = false;
            const std::vector<Type::BaseSpec> &bs = memberOf->bases();
            for (std::size_t i = 0; i < bs.size(); i++)
                if (bs[i].type->tag() == entry) { isBase = true; break; }

            if (isBase) {
                if (baseArgs.count(entry))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                baseArgs[entry] = std::move(args);
            } else if (const Member *m = memberOf->findMember(entry)) {
                if (memberExprs.count(entry))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                if (m->type->isConst())
                    src_.fail(epos, "a const member in an initialiser list is "
                                    "not supported yet");
                if (args.size() != 1)
                    src_.fail(epos, "'" + entry + "' takes one value here, "
                                    "given " + std::to_string(args.size()));
                memberExprs[entry] = std::move(args);
                where[entry] = epos;
            } else if (entry == d.qualifier) {
                src_.fail(epos, "a delegating constructor is not supported "
                                "yet - it is C++11's own addition and comes "
                                "later");
            } else {
                src_.fail(epos, "'" + entry + "' is neither a member of '" +
                                d.qualifier + "' nor a direct base of it");
            }
            if (!consume(",")) break;
        }

        // Declaration order, walking the class's own member list.
        const std::vector<Member> &all = memberOf->members();
        for (std::size_t i = 0; i < all.size(); i++) {
            std::map<std::string, std::vector<ExprPtr> >::iterator found =
                memberExprs.find(all[i].name);
            if (found == memberExprs.end()) continue;
            const Member *m = &all[i];
            std::size_t epos = where[m->name];

            ExprPtr me(Var::local("this", thisOffset_));
            me->setType(types_.pointerTo(memberOf));
            ExprPtr obj(new Unary('*', std::move(me)));
            obj->setType(memberOf);
            ExprPtr field(new MemberAccess(std::move(obj), m->name, m->offset,
                                           m->width, m->bitOffset));
            field->setType(m->type);

            ExprPtr value = decay(std::move(found->second[0]));
            checkAssignable(*value, m->type, epos, "'" + m->name + "'");
            value = convert(std::move(value), m->type);
            ExprPtr assign(new Assign(std::move(field), std::move(value)));
            assign->setType(m->type);
            memberInits.push_back(StmtPtr(new ExprStmt(std::move(assign))));
        }
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

    // Anything already alive belongs to an enclosing function - a class can
    // be defined inside one, and its member functions are defined from there.
    const std::size_t paramsFrom = alive_.size() - aliveParams;

    atFunctionBody_ = true;
    StmtPtr body = block();
    resolveGotos();
    variadicBody_ = false;

    // **The by-value parameters Microsoft makes this function destroy.** A
    // `return` already unwinds everything the function owes, parameters
    // included, so these are appended for the one path that does not go
    // through one: falling off the end.
    if (aliveParams != 0) {
        std::vector<StmtPtr> withParams;
        withParams.push_back(std::move(body));
        emitDestructors(withParams, paramsFrom, d.pos);
        body = StmtPtr(new Block(std::move(withParams)));
        alive_.resize(paramsFrom);
    }

    // Members initialise after the bases and the vptr and before the body -
    // so they are stitched in front of the body here, and the vptr and base
    // blocks below then wrap the result in their own order.
    if (!memberInits.empty()) {
        std::vector<StmtPtr> withInits;
        for (std::size_t i = 0; i < memberInits.size(); i++)
            withInits.push_back(std::move(memberInits[i]));
        withInits.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withInits)));
    }

    // **A polymorphic object's vptr is set by its constructor**, before the
    // body and after the base's constructor - which is what makes the object
    // this class's during its own body even though the base already set the
    // pointer to its own table.
    //
    if (memberOf != nullptr && d.name == localOf(d.qualifier) &&
        memberOf->polymorphic()) {
        std::vector<StmtPtr> withVptr = storeVptrs(d.qualifier, memberOf, thisOffset_);
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
         (d.name == localOf(d.qualifier) ||
          d.name == "~" + localOf(d.qualifier)); bn++) {
        const bool building = d.name == localOf(d.qualifier);
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
            // The initialiser list's arguments for this base, or none - in
            // which case the default constructor is what runs, and a base
            // without one is refused where the reader can fix it.
            std::vector<ExprPtr> chosenArgs;
            std::map<std::string, std::vector<ExprPtr> >::iterator named =
                baseArgs.find(base->tag());
            const Signature *chosen = nullptr;
            if (building && named != baseArgs.end()) {
                chosenArgs.swap(named->second);
                chosen = &resolveOverload(key, chosenArgs, d.pos);
            } else {
                for (std::size_t k = 0; k < set->size(); k++)
                    if (functions_[(*set)[k]].params.empty())
                        chosen = &functions_[(*set)[k]];
            }
            if (chosen == nullptr)
                src_.fail(d.pos, "'" + base->tag() + "' has no constructor "
                                 "taking nothing - name one in the initialiser "
                                 "list, ': " + base->tag() + "(...)'");

            std::string symbol = chosen->symbol;
            if (!target_.microsoftNames()) {
                std::string sub;
                if (building) {
                    const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                             chosen->params,
                                                             false);
                    std::string why;
                    itaniumConstructorName(base->tag(), base, fnType, false,
                                           &sub, &why);
                } else {
                    itaniumDestructorName(base->tag(), base, false, &sub);
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
            std::vector<const Type *> params2;
            params2.push_back(basePtr);
            for (std::size_t i = 0; i < chosen->params.size(); i++) {
                args.push_back(std::move(chosenArgs[i]));
                params2.push_back(chosen->params[i]);
            }
            ExprPtr call = completeCall(base->tag(), symbol, nullptr,
                                        types_.get(Kind::Void), params2, false,
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
    program.functions.back().setHasLandingPads(functionHasPads_);
    functionHasPads_ = false;
    functionTypeIndex_ = 0;
    functionHasTry_ = false;
    // A constructor is emitted under both of Itanium's names: C1 for a
    // complete object, C2 for a base subobject, the second as a label in front
    // of the first. The Microsoft ABI has one name and wants no alias.
    if (memberOf != nullptr && d.name == localOf(d.qualifier) &&
        !target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void), params, false);
        std::string c2, why;
        if (itaniumConstructorName(d.qualifier, findTypedef(d.qualifier),
                                   fnType, false, &c2, &why))
            program.functions.back().setAlias(c2);
    }
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
        !target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(d.qualifier, memberOf, false, &d2);
        program.functions.back().setAlias(d2);
    }
    // The deleting form is emitted beside the destructor that was just
    // defined, because that is where its body comes from.
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
        member != nullptr &&
        member->isVirtual)
        synthesizeDeleting(d.qualifier, memberOf, member->access, d.pos);
    program.functions.back().setBlocks(std::move(blocks_));
}

Program Parser::parse() {
    Program program;
    current_ = &program;
    while (peek().kind != TokenKind::End)
        topLevel(program);
    instantiatePending();
    defineImplicitFunctions();
    if (program.functions.empty())
        src_.fail(0, "the file defines no functions");
    return program;
}
