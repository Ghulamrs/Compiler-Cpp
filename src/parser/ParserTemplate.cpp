// The parser: templates. Template parameters and the scope they are bound in,
// argument deduction, partial ordering and specialisation, and the token replay
// that turns a pattern into an instantiation. Rung 5.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// ------------------------------------------------------------------ templates
// **Rung 5.1: the table exists and nothing is instantiated.** `f<int>(x)` and
// `a<b>(c)` are the same tokens; only a name in the table opens an argument list.

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

// The name the template is being given, and nothing else about it. A class
// template's is read straight off the keyword; **a function template's sits behind
// a return type that mentions the parameters**, so `T` denotes a stand-in first.
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
    // stand-in used to be `int`, which was enough to find a name - but it would
    // instantiate `Box<int>` at a declaration that asks for no class at all.
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

// **The parameters are bound to the argument list, and the tables are put back
// exactly as they were.** A type parameter becomes a type name and a non-type one
// an enumerator, which is what makes `T x` and `int a[N]` read with no new path.
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
    // **Put back even if this throws.** Forming a signature is what a trial runs,
    // and a failed one must leave the parameter names unbound for the next
    // candidate - all that stands between a failure and a table still saying int.
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

    // **The declarator records where the parameter list is and does not read it**,
    // which is how a definition gets to read the parameters once with their names.
    // Here there is none, so they are read for their types as a prototype's are.
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

    // **A variable template is C++14, and it is told from the two C++11
    // declarations by a token scan**: a class or function reaches '(' or a class
    // key before any '=', and an out-of-line member writes '::' before its own.
    if (!peek().is("struct") && !peek().is("class") && !peek().is("union")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            const Token &t = tokens_[i];
            if (depth == 0) {
                if (t.is("(") || t.is("::") || t.is("{") || t.is(";")) break;
                if (t.is("="))
                    src_.fail(t.pos, "a variable template is C++14, and this "
                                     "compiler is C++11 - a class template "
                                     "with a static member says the same "
                                     "thing here");
            }
            if (t.is("[") || t.is("<")) depth++;
            else if (t.is("]") || t.is(">")) depth--;
            if (depth < 0) break;
        }
    }

    // Its own step, and refused by name until then: the declarator reads a class
    // *name* before the `::`, and reading a template-id there is what an
    // out-of-line constructor needs. A member function has a return type instead.
    std::string special;
    if (atOutOfLineSpecial(&special))
        src_.fail(decl.pos, "a " + special + " of a class template written "
                            "outside the class is not supported yet - write "
                            "it inside the class");

    // **`template <class T> struct Box<T *>` - a partial specialization.** It is
    // told from the primary by the `<` after the name: a class template being
    // *declared* has nothing there, and one already declared is being specialized.
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
        partialArguments(&ps, primary->second.params);
        // **`:` as well as `{`.** A specialization may have a base clause and a
        // recursive variadic one always does - `struct A<T, R...> : A<R...>`. The
        // replay goes through structOrUnionSpecifier, which reads both in order.
        if (!peek().is("{") && !peek().is(":"))
            src_.fail(peek().pos, "a partial specialization is a definition, "
                                  "and this one has no body");
        ps.bodyAt = at_;
        for (std::size_t i = 0; i < ps.params.size(); i++) {
            bool mentioned = false;
            for (std::size_t k = 0; k < ps.args.size(); k++)
                if (ps.args[k].isPackExpansion) {
                    // `R...` names its parameter and holds no pattern to
                    // walk, so it is asked about directly rather than through
                    // mentionsParam - whose argument would be null.
                    if (ps.args[k].param == i) mentioned = true;
                } else if (!ps.args[k].isType) {
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

    // **A member of a class template defined outside it belongs to the class**, not
    // to a template of its own. The declarator already reads a qualified name; what
    // is new is that the qualifier is a template-id, naming the pattern.
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
        // **One template per name, and the second is refused rather than dropped.**
        // This table holds one entry per name, so a second definition used to
        // replace nothing and simply disappear - a silently missing overload.
        src_.fail(decl.pos, "'" + decl.name + "' is already a template, and "
                            "two templates of one name are not supported yet");
    }
    return true;
}

// `template <> struct Box<int> { ... };` - rung 5.6. A class written out for one
// argument list: the tag is `Box<int>` as the template would have made it, so every
// lookup and mangling is the same. **The list is read against the primary's.**
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
    // specialization has to be declared before the first use that would instantiate
    // the template, or two different classes have been given one name.
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

// The argument list, read only far enough to step over it - and stepping over it is
// what proves the `>>` split, since `Box<Box<int>>` cannot be got past any other
// way. A nested list is recognised by its name being a template.
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

// `<int, 3>` at a use, read against the parameter list it is for. A type parameter
// takes a type-id and a non-type one a constant expression, so which is which is
// decided by the template and never by the shape of what is written.
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
            // **`first` and not `a.pack.empty()`.** An expansion may contribute
            // nothing - `A<R...>` where R is empty is how a recursion ends - so
            // emptiness cannot say whether a comma is due.
            bool first = true;
            while (!atClosingAngle()) {
                if (!first) expect(",");
                first = false;
                // **`R...` - one pack expanded into another's argument list**,
                // which is what makes a recursive variadic class possible: each
                // step passes on all but the head until the empty case stops it.
                if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
                    auto pk = packs_.find(peek().text);
                    if (pk != packs_.end()) {
                        const std::vector<const Type *> &members =
                            pk->second.types;
                        // In a pattern the pack stands for itself and there is
                        // nothing to splice; reading one here would put a
                        // Kind::TemplateParam into a real argument list.
                        if (members.size() == 1 &&
                            members[0]->kind() == Kind::TemplateParam)
                            src_.fail(peek().pos,
                                      "expanding a pack into another "
                                      "template's argument list needs the "
                                      "pack's members, and here it stands for "
                                      "itself");
                        at_ += 2;
                        for (std::size_t k = 0; k < members.size(); k++)
                            a.pack.push_back(members[k]);
                        continue;
                    }
                }
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

// The specialization these arguments ask for, made if it is new. **The two ABIs are
// handed two different things**: Itanium the pattern, since its name spells `T_`,
// and Microsoft the substituted signature. So the declaration is read twice.
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

    // **Under two keys, on purpose.** "twice<int>" is what the replayed definition
    // declares and what a repeat of the same arguments finds; "twice" is what
    // overload resolution must see, a specialization competing with the ordinary.
    const std::size_t at = functions_.size();
    functionIndex_[key].push_back(at);
    functionIndex_[decl.name].push_back(at);
    functions_.push_back(Signature{ key, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    functions_.back().fromTemplate = true;
    return functions_.back();
}

// **A body cannot be written where the call is**, so every specialization is
// recorded and the definitions replayed afterwards, to a fixed point. And whether
// anything under this key was chosen by a call, which gates every body it holds.
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
            // **Made where it was asked for, defined only where it was chosen** -
            // deduction instantiates a candidate before it can rank one, and an
            // ordinary function may win. A body skipped now may be wanted later.
            std::vector<PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (specializations_[i].isClass) {
                std::vector<PendingBody> later;
                for (std::size_t b = 0; b < specializations_[i].bodies.size(); b++) {
                    const PendingBody &body = specializations_[i].bodies[b];
                    (memberIsUsed(body.key) ? now : later).push_back(body);
                }
                specializations_[i].bodies = later;

                // **Looked up fresh, because the list can still be growing.** An
                // out-of-line definition may be written further down the file than
                // the use that asked for the class.
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

bool Parser::mentionsDeduced(const Type *t) {
    if (t == nullptr) return false;
    if (t->kind() == Kind::Deduced) return true;
    if (t->unqualified() != t) return mentionsDeduced(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsDeduced(t->pointee());
    if (t->isReference()) return mentionsDeduced(t->referent());
    return false;
}

// The declared type with `auto` replaced, keeping everything written around
// it: `const auto &` deduced as int is `const int &`.
const Type *Parser::substituteDeduced(const Type *t, const Type *with) {
    if (t->kind() == Kind::Deduced) return with;
    if (t->unqualified() != t)
        return types_.withConst(substituteDeduced(t->unqualified(), with));
    if (t->isPointer()) return types_.pointerTo(substituteDeduced(t->pointee(), with));
    if (t->isReference()) return types_.referenceTo(substituteDeduced(t->referent(), with));
    if (t->isArray())
        return types_.arrayOf(substituteDeduced(t->pointee(), with), t->length());
    return t;
}

// **The initialiser is read twice: once to learn its type, once to build it.** The
// tokens are put back in between, so the ordinary declaration path sees exactly
// what it would have seen with the type written out.
const Type *Parser::deduceAuto(const Type *declared, const std::string &name,
                               std::size_t pos) {
    if (!peek().is("="))
        src_.fail(pos, "'" + name + "' is declared 'auto' and has no "
                       "initialiser, so there is nothing to deduce its type "
                       "from");

    const std::size_t resume = at_;
    at_++;                                   // the '='
    if (peek().is("{"))
        src_.fail(peek().pos, "'auto' from a braced initialiser is not "
                              "supported yet - it deduces an "
                              "initializer_list, which this compiler has no "
                              "library for");
    ExprPtr init = assign();
    const Type *from = init->type();
    at_ = resume;

    return deduceAutoFrom(declared, from, name, pos);
}

const Type *Parser::deduceAutoFrom(const Type *declared, const Type *from,
                                   const std::string &name, std::size_t pos) {
    std::vector<const Type *> binding(1, static_cast<const Type *>(nullptr));
    std::string why;
    if (!deduceOne(declared, from, &binding, &why) || binding[0] == nullptr)
        src_.fail(pos, "'" + name + "' is declared '" + declared->describe() +
                       "' and its initialiser is '" + from->describe() +
                       "', which does not fit: " + why);
    return substituteDeduced(declared, binding[0]);
}

// **What a parameter sees of an argument.** [temp.deduct.call]: an array becomes a
// pointer to its first element, a function a pointer to itself, and the top-level
// qualifier goes - which is also just what passing something does.
const Type *Parser::decayedType(const Type *a) const {
    if (a->isReference()) a = a->referent();
    if (a->isArray()) return types_.pointerTo(a->pointee());
    if (a->isFunction()) return types_.pointerTo(a);
    return a->unqualified();
}

// One parameter of the pattern against one argument's type. The pattern still has
// Kind::TemplateParam in it, so "does this position deduce anything" is a question
// about the type and not about a table: one reached here binds.
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

    // Kind::Deduced is `auto`, and it is parameter zero of a deduction with
    // one parameter - which is what [dcl.spec.auto] says it is.
    if (pattern->kind() == Kind::TemplateParam ||
        pattern->kind() == Kind::Deduced) {
        const std::size_t i = pattern->kind() == Kind::Deduced
                                  ? 0
                                  : static_cast<std::size_t>(pattern->length());
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

    // Nothing to deduce here. A parameter written out in full does not have to match
    // exactly - an ordinary conversion may still get the argument there - so this is
    // not where a mismatch is reported; overload resolution ranks it after.
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

    // **A trailing pack takes every argument the written parameters leave.** It is
    // the last parameter by construction, so "the rest" needs no searching - and it
    // may be none, which is why this is a `<` and not a `!=` on the count.
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

// Whether parameter `i` appears anywhere in a pattern. One a specialization never
// mentions could not be worked out from any argument list, so it could never be
// chosen - worth refusing where it is written rather than leaving it silent.
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
      pattern(parser->patternOnly_), split(parser->angleSplit_) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
    p->angleSplit_ = split;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Parser::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why) const {
    // **The qualifier is asked about before anything else, and both sides must
    // agree.** `Box<const T>` matches `Box<const int>` and not `Box<int>`; `Box<T>`
    // matches both, binding T to the qualified type where there is one.
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
void Parser::partialArguments(TemplateDecl::Partial *ps,
                              const std::vector<TemplateParam> &primary) {
    const std::size_t count = primary.size();
    // **A variadic primary is not written a fixed number of arguments**: a pack
    // stands for a list, so the closing angle says where the pattern stops and not
    // the parameter count - which gave "more arguments than parameters" before.
    const bool variadic = !primary.empty() && primary.back().isPack;
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

    for (std::size_t i = 0; variadic ? !atClosingAngle() : i < count; i++) {
        if (i > 0) expect(",");
        TemplateDecl::Partial::Arg a;

        // `R...` - this specialization's own pack, standing for everything
        // the fixed arguments before it did not take.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            std::size_t k = ps->params.size();
            for (std::size_t j = 0; j < ps->params.size(); j++)
                if (ps->params[j].isPack && ps->params[j].name == peek().text)
                    k = j;
            if (k < ps->params.size()) {
                a.isType = true;
                a.isPackExpansion = true;
                a.param = k;
                at_ += 2;
                ps->args.push_back(a);
                continue;
            }
        }
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
    // A pack expansion may only be last: everything after it could never be
    // told apart from a member of it. Same rule, and the same reason, as a
    // pack having to be the last *parameter*.
    for (std::size_t i = 0; i + 1 < ps->args.size(); i++)
        if (ps->args[i].isPackExpansion)
            src_.fail(ps->params.empty() ? 0 : ps->params[0].pos,
                      "a pack expansion has to be the last argument of a "
                      "specialization - anything after it could never be told "
                      "apart from one of its members");

    unbindTemplateParameters(undo);
    inTemplateArgs_ = wasInArgs;
    patternOnly_ = wasPattern;
}

// [temp.class.order], asked the standard's own way: A is at least as specialized as
// B when B's pattern matches A's. A's parameters stand as opaque types while that
// happens, which is exactly what Kind::TemplateParam already is.
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
                                  std::vector<std::vector<const Type *> > *packs,
                                  std::size_t pos) {
    std::vector<std::size_t> fits;
    std::vector<std::vector<const Type *> > bindings;
    std::vector<std::vector<long long> > valueSets;
    std::vector<std::vector<std::vector<const Type *> > > packSets;

    // **The arguments arrive as one pack when the primary is variadic.**
    // `L<int, char>` against `template <class... Ts>` is a single TemplateArg
    // holding both, so the list is flattened here and every pattern matched on it.
    std::vector<TemplateArg> flat;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i].isPack) { flat.push_back(args[i]); continue; }
        for (std::size_t k = 0; k < args[i].pack.size(); k++) {
            TemplateArg one;
            one.isType = true;
            one.type = args[i].pack[k];
            flat.push_back(one);
        }
    }

    for (std::size_t p = 0; p < decl.partials.size(); p++) {
        const TemplateDecl::Partial &ps = decl.partials[p];

        // A trailing `R...` takes everything the fixed arguments leave, so
        // the arity it demands is a minimum rather than an equality.
        const bool takesRest = !ps.args.empty() &&
                               ps.args.back().isPackExpansion;
        const std::size_t fixed = takesRest ? ps.args.size() - 1
                                            : ps.args.size();
        if (takesRest ? flat.size() < fixed : ps.args.size() != flat.size())
            continue;

        std::vector<const Type *> b(ps.params.size());
        std::vector<long long> v(ps.params.size(), 0);
        std::vector<std::vector<const Type *> > pk(ps.params.size());
        std::string why;
        bool ok = true;
        for (std::size_t i = 0; i < fixed && ok; i++) {
            const TemplateDecl::Partial::Arg &a = ps.args[i];
            if (a.isType != flat[i].isType) { ok = false; break; }
            if (!a.isType) {
                if (a.isParam) v[a.param] = flat[i].value;
                else if (a.value != flat[i].value) ok = false;
                continue;
            }
            if (!matchPattern(a.type, flat[i].type, &b, &why)) ok = false;
        }
        if (ok && takesRest) {
            const TemplateDecl::Partial::Arg &tail = ps.args.back();
            for (std::size_t i = fixed; i < flat.size() && ok; i++) {
                if (!flat[i].isType) { ok = false; break; }
                pk[tail.param].push_back(flat[i].type);
            }
        }
        // A pack parameter binds through `pk` and never through `b`, so it is
        // not the unbound-parameter fault this is looking for.
        for (std::size_t i = 0; ok && i < ps.params.size(); i++)
            if (ps.params[i].type == nullptr && !ps.params[i].isPack &&
                b[i] == nullptr) ok = false;
        if (!ok) continue;
        fits.push_back(p);
        bindings.push_back(b);
        valueSets.push_back(v);
        packSets.push_back(pk);
    }

    if (fits.empty()) return static_cast<std::size_t>(-1);

    // **One has to beat every other, and "not beaten" is not the same as "beats".**
    // `P<A, int>` and `P<int, B>` given `P<int, int>`: neither matches the other, so
    // the program is ambiguous rather than settled by whichever came first.
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
    if (packs != nullptr) *packs = packSets[best];
    return fits[best];
}

// `Box<int, 3>` where a type was expected - rung 5.4. The class is made by replaying
// `struct Box { ... };` with the arguments bound, and all the class path had to be
// told is what tag to take: nested classes had made tag() an arbitrary string.
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

    // **A partial specialization is chosen before anything is replayed**, and what
    // it changes is which tokens get replayed and with which parameters bound. The
    // tag does not change, which keeps the mangling and every lookup the same.
    std::vector<const Type *> useBinding = binding;
    std::vector<long long> useValues = values;
    std::vector<TemplateParam> useParams = decl.params;
    std::vector<std::vector<const Type *> > usePacks;
    const std::size_t which = choosePartial(decl, args, &useBinding, &useValues,
                                            &usePacks, pos);
    const bool partial = which != static_cast<std::size_t>(-1);
    if (partial) useParams = decl.partials[which].params;

    // **Asked after the partial is chosen, not before.** A variadic template is
    // very often declared and never defined, every definition it has being a
    // specialization: there is a body to replay whenever one of them matched.
    if (!partial && !decl.defined)
        src_.fail(pos, "'" + decl.name + "' is declared but never defined, so "
                       "there is nothing to instantiate");

    const std::size_t resume = at_;
    std::vector<Shadow> undo;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? usePacks : packs, &undo);

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
    // **A partial's pack has to be recorded too.** Held bodies are replayed later
    // from this record, and one saying `Tuple<Rest...>` needs Rest as it was. Left
    // empty, `Tuple<char,long>::tail` returned `Tuple<long> &` and said `Tuple<> &`.
    sp.packs = partial ? usePacks : packs;
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

    // **No argument list, so they come from the call.** The arguments are parsed
    // before anything can be deduced from them, the other way round from the
    // written case - and the order overload resolution wants.
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
            // Not an error while an ordinary function of this name might still take
            // the call - it is one fewer candidate. With no such function it is the
            // whole answer, and saying why beats "not declared".
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
            // **[temp.deduct]/8, which is what SFINAE is.** Substituting deduced
            // arguments may make something ill-formed, and that removes the
            // specialization rather than ending the compile. A body is not in it.
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
    // A copy, not a reference: `sig.params` is handed to `completeCall` and
    // read for the length of it, and what that call does with the arguments
    // can declare a function and move `functions_` out from under it.
    const Signature sig = functions_[which];
    return completeCall(sig.name, sig.symbol, nullptr, sig.returns, sig.params,
                        sig.variadic, pos, std::move(callArgs));
}

void Parser::refuseTemplateId() {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    at_++;
    if (peek().is("<")) skipTemplateArguments();
    // **`A<int>::n` is not an instantiation this cannot do.** The type is made
    // perfectly well in a declaration; what is missing is reading a template-id as
    // the qualifier of a name. The typedef it names is the whole workaround.
    if (peek().is("::"))
        src_.fail(pos, "'" + name + "<...>::' - naming a member through a "
                       "class template's argument list is not supported yet; "
                       "the type itself is made, so 'typedef " + name +
                       "<...> Name;' and then 'Name::' reaches the member");
    src_.fail(pos, "'" + name + "' is a " +
                   (templates_[name].isClass ? "class" : "function") +
                   " template, and instantiating one is not supported yet");
}

