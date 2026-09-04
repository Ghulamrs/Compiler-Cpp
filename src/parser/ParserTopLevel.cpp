// The parser: the top level, and what a translation unit holds.
//
// `topLevel` is the one function that reads a file's every kind of declaration
// - a namespace, a linkage specification, a typedef, a class, a global, a
// function declared or defined - and `parse` is the loop over it that finishes
// with the implicit functions and the templates the file asked to instantiate.
//
// Split out of ParserStmt.cpp: a statement is read inside a function body, and
// everything here is read outside one.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

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
    // `namespace N { ... }` - a scope that qualifies what is declared in it,
    // and nothing else. Everything inside is read by this same function, so a
    // namespace nests, may be reopened, and may hold anything a file may hold.
    if (peek().is("namespace")) {
        const std::size_t pos = peek().pos;
        at_++;
        // **An unnamed namespace, opened under the name the ABI gives it.**
        // `_GLOBAL__N_1` is what the Itanium ABI calls this namespace, so a
        // mangled name here is clang's exactly; and the `L` that marks a
        // written `static` is *not* added, because the namespace name already
        // says the linkage - which is why only emission asks internalLinkage().
        // [namespace.unnamed] makes one per translation unit, reopened by every
        // `namespace {` in the file, and its names visible unqualified from
        // there on - which is a namespace, a directive, and internal linkage on
        // what it holds. All three are already here.
        if (peek().is("{")) {
            at_++;
            const std::string tag = "_GLOBAL__N_1";
            const bool outer = inUnnamedNamespace_;
            namespaceStack_.push_back(tag);
            namespaces_.insert(namespacePrefix().substr(
                                   0, namespacePrefix().size() - 2));
            inUnnamedNamespace_ = true;
            while (!peek().is("}")) {
                if (peek().kind == TokenKind::End)
                    src_.fail(pos, "this namespace never closes");
                topLevel(program);
            }
            at_++;                              // the '}'
            const std::string opened = namespacePrefix().substr(
                                           0, namespacePrefix().size() - 2);
            namespaceStack_.pop_back();
            inUnnamedNamespace_ = outer;
            // The directive is not undone at the closing brace: an unnamed
            // namespace's names answer an unqualified lookup for the rest of
            // the file, which is the whole of what it is for.
            usingNamespaces_.push_back(opened);
            return;
        }
        std::string name = expectIdent("a namespace name");
        // `namespace N::M { }` is C++17; nesting is written out here.
        if (peek().is("::"))
            src_.fail(peek().pos, "a nested namespace written 'N::M' is C++17 "
                                  "- open them one at a time");
        if (consume("=")) 
            src_.fail(pos, "a namespace alias is not supported yet");
        expect("{");
        namespaceStack_.push_back(name);
        namespaces_.insert(namespacePrefix().substr(
                               0, namespacePrefix().size() - 2));
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this namespace never closes");
            topLevel(program);
        }
        at_++;                                  // the '}'
        namespaceStack_.pop_back();
        return;
    }

    if (staticAssertion()) return;

    // `using namespace N;` - the names in N answer an unqualified lookup from
    // here on. A using-*declaration*, `using N::f;`, names one thing and is a
    // different rule; it is refused by name.
    if (peek().is("using")) {
        const std::size_t pos = peek().pos;
        at_++;
        // **`using N::f;` names one thing, and what it leaves behind is an
        // alias.** The name it declares is `f` under whatever namespace encloses
        // this, and it stands for the name written - `using ::size_t;` inside
        // `namespace std` being the case the library's first header needs.
        if (!peek().is("namespace")) {
            const bool fromGlobal = consume("::");
            std::string target = expectIdent("a name after 'using'");
            while (peek().is("::")) {
                at_++;
                target += "::" + expectIdent("a name after '::'");
            }
            expect(";");
            const std::string::size_type cut = target.rfind("::");
            if (!fromGlobal && cut == std::string::npos)
                src_.fail(pos, "a using-declaration names something declared "
                               "elsewhere, so it takes a qualified name: "
                               "'using N::f;', or 'using ::f;' for one at "
                               "global scope");
            // **Refused by name if it names nothing**, rather than recorded and
            // found missing at the use - the position of the declaration is the
            // one that says which name was meant.
            if (!hasTypeNamed(target) && !hasFunctionNamed(target) &&
                !hasGlobalNamed(target) &&
                namespaces_.find(target) == namespaces_.end())
                src_.fail(pos, "'" + target + "' is not declared, so there is "
                               "nothing here for this using-declaration to "
                               "name");
            const std::string shortName =
                cut == std::string::npos ? target : target.substr(cut + 2);
            const std::string declared = namespacePrefix() + shortName;
            if (declared != target) usingDeclarations_[declared] = target;
            return;
        }
        at_++;
        std::string opened = expectIdent("a namespace name");
        while (peek().is("::")) {
            at_++;
            opened += "::" + expectIdent("a namespace name");
        }
        expect(";");
        usingNamespaces_.push_back(opened);
        return;
    }

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
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same type,
            // which is what makes the C idiom "typedef struct S S;" legal now that the
            // tag already names the type by itself. Only a different type is an error.
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

    // **A `constexpr` function was refused by name until 7.5b**: accepting it as an
    // ordinary function would compile and then quietly fail to be constant where the
    // keyword was written for. The '(' still ahead tells a function from an object.
    const bool constexprFunction =
        quals.isConstexpr &&
        (peek().is("(") || d.paramsAt != 0 || d.type->isFunction());

    // **`constexpr` does not make the return type const**, and it is measured rather
    // than reasoned: cl and clang both spell `constexpr int sq(int)` as ?sq@@YAHH@Z.
    // The keyword sets isConst because on an *object* that is exactly what it means.
    if (constexprFunction && !d.type->isFunction())
        d.type = types_.withoutConst(d.type);

    // `int Counter::total = 0;` - a static member's definition. A member *function*'s
    // is spelled the same way up to here and told apart by the '(' that follows, which
    // is the same question the class body asks about a member.
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
            if (mentionsDeduced(d.type))
                d.type = deduceAuto(d.type, d.name, d.pos);
            if (d.type->isVoid()) src_.fail(d.pos, "'" + d.name + "' cannot have type void");
            // A reference at file scope has to be bound before main runs,
            // which is a whole mechanism - the same one static objects with
            // constructors will need - and it is not here yet.
            if (d.type->isReference())
                src_.fail(d.pos, "'" + d.name + "' is a reference at file "
                                 "scope, and binding one before main is not "
                                 "supported yet - make it a local or a "
                                 "pointer");

            // **A class with a constructor, at file scope.** This path had no test at
            // all, so the object was laid out as bytes and the constructor never ran.
            // The braced form is asked first: C++11 makes such a class no aggregate.
            if (d.type->isStructOrUnion() && !d.type->tag().empty()) {
                const bool braced = peek().is("=") && peekAt(1).is("{");
                if (braced && hasMemberInitialiser(d.type->tag()))
                    src_.fail(d.pos, "'" + d.type->describe() + "' writes an "
                                     "initialiser on a member, so in C++11 it "
                                     "is not an aggregate and a braced list "
                                     "cannot initialise it - C++14 changed "
                                     "that rule and this compiler is C++11");
                if (overloadsOf(constructorKey(d.type->tag())) != nullptr)
                    src_.fail(d.pos, "'" + d.name + "' is at file scope and '" +
                                     d.type->describe() + "' has a constructor "
                                     "- running one before main is not "
                                     "supported yet");
            }

            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            // Read while the initialiser tree is still in scope - `in` does
            // not outlive the branch, and flattenInit answers in bytes rather
            // than in the value this wants.
            bool constantKnown = false;
            long long constantValue = 0;
            if (consume("=") || atBracedInitialiser(d.name)) {
                Init in = parseInitialiser();
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                constantKnown = constantInitialiser(d.type, in, &constantValue);
                if (!constantKnown && quals.isConstexpr)
                    src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so its "
                                     "value has to be known while this is "
                                     "compiled, and this initialiser is not a "
                                     "constant expression");
                flattenInit(d.type, in, 0, pieces);
                hasInit = true;
            } else if (quals.isConstexpr && sc != StorageExtern) {
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr' and has no "
                                 "initialiser - there is nothing for it to be");
            } else if (d.type->isConst() && sc != StorageExtern) {
                // The same [dcl.init]/7 the local path asks about. `extern` is
                // exempt because it declares rather than defines: the definition
                // is somewhere else and is where the initialiser has to be.
                requireConstInitialised(d.type, d.name, d.pos);
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
                                                          internalLinkage(sc),
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

            // A variable declared in a namespace is keyed and mangled by its qualified
            // name, the same as a function. `extern "C"` does not reach into one, so a
            // name with C linkage keeps what it was written with.
            const std::string gname =
                (namespaceStack_.empty() || cLinkage_ > 0)
                    ? d.name : namespacePrefix() + d.name;
            globalIndex_[gname] = globals_.size();
            bool objectIsConst = d.type->isConst();
            // A const object at namespace scope has internal linkage of its own -
            // [basic.link]/3 - which is why a header may define one and C, where it
            // would be external, may not. Nothing outside can name it.
            bool internal = sc == StorageStatic ||
                            (objectIsConst && sc != StorageExtern);
            std::string symbol = dataSymbol(gname, d.type, internal, d.pos);
            globals_.push_back(GlobalSym{ gname, symbol, d.type, objectIsConst,
                                          sc != StorageExtern, hasInit,
                                          constantKnown, constantValue });
            if (sc != StorageExtern)
                program.globals.push_back(Global{ gname, symbol, d.type,
                                                  std::move(pieces), hasInit,
                                                  internal, objectIsConst });
            if (!consume(",")) break;
            d = declarator(base);
        }
        expect(";");
        return;
    }

    // **A trailing return type is C++11, and it arrives here wearing the same `auto`.**
    // `auto f(int) -> int` says what the return type is rather than asking for it to be
    // deduced. The parameter list is still ahead, so the arrow is found past it.
    bool trailingArrow = false;
    if (peek().is("(")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            if (tokens_[i].is("(")) depth++;
            else if (tokens_[i].is(")")) {
                if (--depth == 0) {
                    trailingArrow = i + 1 < tokens_.size() &&
                                    tokens_[i + 1].is("->");
                    break;
                }
            }
        }
    }
    if (mentionsDeduced(d.type) && trailingArrow)
        src_.fail(d.pos, "a trailing return type - `auto f(...) -> T` - is "
                         "C++11 and is not supported yet; write the return "
                         "type in front, which says the same thing wherever it "
                         "does not name a parameter");
    if (mentionsDeduced(d.type))
        src_.fail(d.pos, "a function's return type cannot be deduced - `auto` "
                         "there is C++14, and this compiler is C++11");

    std::size_t resumeAt = 0;
    if (d.paramsAt != 0) {
        resumeAt = at_;
        at_ = d.paramsAt;
    }

    expect("(");
    std::vector<const Type *> params;
    std::vector<Param> paramSlots;
    bool variadic = false;
    std::size_t aliveParams = 0;

    // **`this` is parameter zero, and it is declared before any written one so that it
    // takes the first slot.** That is the whole of how a member function differs at the
    // machine. It is not in `params`, which is the declared signature.
    const Type *memberOf = nullptr;
    if (!d.qualifier.empty()) {
        memberOf = findTypedef(d.qualifier);
        if (memberOf == nullptr || !memberOf->isStructOrUnion())
            src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
        // `void S::f()` written inside `namespace N` defines `N::S::f`, and every table
        // downstream is keyed by the qualified tag. Take the name the class was found
        // under rather than the one that was written.
        d.qualifier = memberOf->tag();
        currentClass_ = memberOf;
    }

    std::vector<std::size_t> defaults;
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
                if (mentionsDeduced(pd.type))
                    src_.fail(pd.pos, "a parameter's type cannot be deduced - "
                                      "`auto` there is C++14, and this "
                                      "compiler is C++11");
                if (pd.type->isArray())
                    pd.type = types_.pointerTo(pd.type->pointee());

                // **A class whose copy is a constructor call arrives by address**, on
                // both ABIs and whatever its size. The parameter is lowered to a
                // reference; the declared type is untouched, and the caller owns it.
                const bool byAddress = passedByAddress(pd.type);
                const Type *held = byAddress ? types_.referenceTo(pd.type)
                                             : pd.type;
                int off;
                {
                    // **A definition may leave a parameter unnamed** - C++ does
                    // not require one where C did, and `operator++(int)` is
                    // written that way by everybody: the parameter exists only
                    // to tell the postfix form from the prefix one. It still
                    // occupies a slot and a place in the calling convention, so
                    // it is declared under a name no program can write rather
                    // than skipped - the same device the lambda return typedef
                    // and a pack's members use.
                    if (pd.name.empty()) {
                        if (pd.type->isVoid())
                            src_.fail(pd.pos,
                                      "'void' is only a parameter list on its own");
                        pd.name = "$unnamed" + std::to_string(params.size());
                    }
                    inParams_ = true;
                    off = declare(pd.name, held, pd.pos);
                    inParams_ = false;
                    locals_.back().isConst = pd.type->isConst();
                    locals_.back().isRegister = (psc == StorageRegister);
                    locals_.back().byValueByAddress = byAddress;

                    // **On Microsoft the callee destroys its by-value class
                    // parameter**, in a register or by address alike - measured with
                    // cl. Itanium puts it on the caller, where the temporary is made.
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
                // A default written on the *definition*. The parameter list here is
                // read by this loop and not by parameterTypes, so the same recording
                // happens twice, and the same way: a place in the token stream.
                defaults.resize(params.size(), 0);
                if (consume("=")) {
                    if (peek().is("{"))
                        src_.fail(peek().pos, "a braced default argument is not "
                                              "supported yet - write the value");
                    defaults.back() = at_;
                    skipDefaultArgument();
                }
                if (consume(")")) break;
                expect(",");
            }
        }
    }
    if (resumeAt != 0) at_ = resumeAt;

    // Hand what this parameter list collected to whichever declare() runs below, the
    // same way parameterTypes hands over its own. **Before the prototype branch and
    // not after it**: that branch returns as soon as it has declared the function.
    bool sawDefault = false;
    for (std::size_t i = 0; i < defaults.size(); i++)
        if (defaults[i] != 0) sawDefault = true;
    if (sawDefault) {
        requireDefaultsAreASuffix(defaults, d.pos);
        pendingDefaults_ = defaults;
    }


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
    // The same C++11 rule the class body applies: a `constexpr` member function is
    // implicitly const. This is the path a member defined inside its class comes back
    // through, so leaving it out makes the definition disagree with its declaration.
    if (memberOf != nullptr && constexprFunction) constThis = true;

    // The exception specification comes after the constness, which is the
    // order C++ writes them in: `int get() const noexcept`.
    pendingNoexcept_ = exceptionSpecification();

    if (consume(";")) {
        if (memberOf != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is declared "
                             "inside the class - this says it again outside, "
                             "which declares nothing new");
        declareFunction(d.name, d.type, params, variadic, false, d.pos,
                        sc == StorageStatic);
        return;
    }
    const Signature *member = nullptr;
    if (memberOf != nullptr) {
        std::string key = d.qualifier + "::" + d.name;   // "Point::~Point" too
        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            for (std::size_t k = 0; k < set->size() && member == nullptr; k++) {
                const Signature &f = functions_[(*set)[k]];
                if (f.constThis == constThis && sameParameters(f.params, params))
                    member = &f;
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
        // **This used to write `member->pos` into the *first* overload's entry**, which
        // corrupts another overload's recorded position when the member being defined
        // is not that one. Nothing reads a user overload's `pos`, so it never showed.
        const_cast<Signature *>(member)->defined = true;

        // **A static member's body gets no `this` slot**, which is the whole of
        // what makes it static once the name is settled: the parameters it was
        // written with are the parameters it has, and the first one stays in the
        // first register rather than being pushed along by an object nobody
        // passed. Everything else about the body - access, the class's own
        // names in scope - is a member's.
        inStaticMember_ = member->isStaticMember;
        if (!member->isStaticMember) {
            // `this` takes the first slot, and its type carries the constness the member
            // was declared with - so a const member function cannot write through it, by
            // the ordinary rule that a const object's members are const.
            const Type *pointee = constThis ? types_.withConst(memberOf) : memberOf;
            const Type *thisType = types_.pointerTo(pointee);
            inParams_ = true;
            thisOffset_ = declare("this", thisType, d.pos);
            inParams_ = false;
            paramSlots.insert(paramSlots.begin(), Param{ thisType, thisOffset_ });
        }
    } else {
        inStaticMember_ = false;
        declareFunction(d.name, d.type, params, variadic, true, d.pos,
                        sc == StorageStatic);
        // Which function's body is about to be read, so that an access check inside it
        // can ask whether a class befriended *this* function. A member's is left empty:
        // the qualified form that would befriend a member is refused where written.
        currentFunction_ = lookupSignature(d.name, params, variadic, d.pos).symbol;
    }
    // Set for a member's body too, unlike the friend question above, because a local
    // class inside a member function is spelled by wrapping that function's name.
    // **Taken by value**: `member` points into a vector any declaration can move.
    std::string definedSymbol;
    if (member != nullptr) {
        definedSymbol = member->symbol;
        currentFunction_ = definedSymbol;
    }
    currentFunctionName_ = d.name;
    localTypes_.clear();
    // Closures are numbered within the function that writes them, which is
    // what clang does - `$_0` upward in each, not once across the file.
    lambdaCount_ = 0;
    // The mem-initializer list, [class.base.init], parsed here because `this` and the
    // parameters are in scope and the body has not begun. **Emission follows
    // declaration order, not list order** - /11, whatever the list says.
    std::vector<StmtPtr> memberInits;
    // Which members this constructor's own list covers. Kept out here because
    // the initialisers the class wrote are applied to the rest, below, and the
    // list itself is scoped to the block that reads it.
    std::set<std::string> namedInInit;
    std::map<std::string, std::vector<ExprPtr> > baseArgs;
    std::map<std::string, std::vector<ExprPtr> > memberExprs;
    std::map<std::string, std::size_t> where;
    // The members written `: m()`, and for one with a constructor the index of the one
    // to run; functions_.size() says there is none. Out here with the other two: the
    // declaration-order walk below reads all three from outside that block.
    std::map<std::string, std::size_t> valueInit;
    const bool isCtor = memberOf != nullptr && d.name == localOf(d.qualifier);
    if (memberOf != nullptr && peek().is(":")) {
        if (!isCtor)
            src_.fail(peek().pos, "an initialiser list belongs to a "
                                  "constructor, and '" + d.name + "' is not one");
        at_++;
        for (;;) {
            std::size_t epos = peek().pos;
            std::string entry = expectIdent("a member or base to initialise");
            expect("(");
            std::vector<ExprPtr> args;
            parseArguments(args);

            // **The name written is not always the base's tag.** A class in a
            // namespace has a qualified tag - `n::Base` - and the list names it
            // the way the source can see it, `Base`. So the written name is
            // resolved as a type and the *types* are compared; the string
            // comparison stays for the case where there is no type to find.
            // What goes in the map is the tag either way, because that is what
            // the walk over the bases looks it up by.
            bool isBase = false;
            std::string baseKey = entry;
            const Type *namedBase = findTypedef(entry);
            const std::vector<Type::BaseSpec> &bs = memberOf->bases();
            for (std::size_t i = 0; i < bs.size(); i++)
                if (bs[i].type->tag() == entry ||
                    (namedBase != nullptr &&
                     namedBase->unqualified() == bs[i].type->unqualified())) {
                    isBase = true;
                    baseKey = bs[i].type->tag();
                    break;
                }

            if (isBase) {
                if (baseArgs.count(baseKey))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                baseArgs[baseKey] = std::move(args);
            } else if (const Member *m = memberOf->findMember(entry)) {
                if (memberExprs.count(entry))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                if (m->type->isConst())
                    src_.fail(epos, "a const member in an initialiser list is "
                                    "not supported yet");
                // **`: m()` value-initialises the member** - [class.base.init] hands
                // the empty pair to [dcl.init]/8: a scalar zeroes, a class with no
                // constructor zeroes leaf by leaf, a user-provided one runs alone.
                if (args.empty()) {
                    if (m->type->isReference())
                        src_.fail(epos, "'" + entry + "()' would leave a "
                                        "reference member bound to nothing - "
                                        "give it what it refers to");
                    const Type *mt = m->type->unqualified();
                    const Type *mc = mt;
                    while (mc->isArray()) mc = mc->pointee();
                    mc = mc->isStructOrUnion() ? mc->unqualified() : nullptr;
                    std::size_t ctorIndex = functions_.size();
                    if (mc != nullptr && !mc->tag().empty() &&
                        overloadsOf(constructorKey(mc->tag())) != nullptr) {
                        if (mt->isArray())
                            src_.fail(epos, "'" + entry + "()' would run '" +
                                            mc->describe() + "''s constructor "
                                            "once per element, which an "
                                            "initialiser list cannot say yet");
                        const Signature *ctor = defaultConstructorOf(mc);
                        if (ctor == nullptr)
                            src_.fail(epos, "'" + entry + "()' needs a "
                                            "constructor of '" + mc->describe() +
                                            "' taking nothing, and it has none");
                        if (ctor->access != Access::Public &&
                            currentClass_ != mc && !isFriendOf(mc))
                            src_.fail(epos, "'" + entry + "()' would call a " +
                                            std::string(ctor->access == Access::Private
                                                        ? "private" : "protected") +
                                            " constructor of '" + mc->describe() +
                                            "'");
                        ctorIndex = static_cast<std::size_t>(ctor - &functions_[0]);
                        functions_[ctorIndex].used = true;
                    }
                    valueInit[entry] = ctorIndex;
                }
                // **How many values are too many is not a question this loop can
                // answer any more.** A class-typed member takes as many as one of its
                // constructors does, so the check moved down with the construction.
                memberExprs[entry] = std::move(args);
                namedInInit.insert(entry);
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

    }

    // **Every member, in declaration order, by the first of three rules that applies to
    // it** - [class.base.init]/8, /9 and /11: named in the list, its own initialiser, or
    // a class with constructors default-constructed. A union's members are not built.
    if (memberOf != nullptr && isCtor) {
        const std::vector<Member> &all = memberOf->members();
        for (std::size_t i = 0; i < all.size(); i++) {
            const Member *m = &all[i];
            std::map<std::string, std::vector<ExprPtr> >::iterator found =
                memberExprs.find(m->name);
            if (found == memberExprs.end()) {
                StmtPtr one = memberInitialiser(d.qualifier, memberOf, *m,
                                                thisOffset_, d.pos);
                std::vector<ExprPtr> none;
                if (one == nullptr && memberOf->kind() != Kind::Union)
                    one = constructMember(d.qualifier, memberOf, *m,
                                          thisOffset_, none, d.pos, false);
                if (one != nullptr) memberInits.push_back(std::move(one));
                continue;
            }
            std::size_t epos = where[m->name];

            // **An empty pair belongs to value-initialisation, and only to it.** `: m()`
            // is [dcl.init]/8 where `: m(a)` is construction, and for `: k()` on an int
            // there is no constructor to reach. So the walk asks who owns the list.
            const bool valueInitialised = valueInit.count(m->name) != 0;

            if (!m->type->isReference() && !valueInitialised) {
                StmtPtr built = constructMember(d.qualifier, memberOf, *m,
                                                thisOffset_, found->second,
                                                epos, false);
                if (built != nullptr) {
                    memberInits.push_back(std::move(built));
                    continue;
                }
            }
            // Anything that is neither constructed nor value-initialised takes
            // one value and assigns it, or binds it where the member is a
            // reference.
            if (!valueInitialised && found->second.size() != 1)
                src_.fail(epos, "'" + m->name + "' takes one value here, "
                                "given " + std::to_string(found->second.size()));

            ExprPtr field = thisMember(thisOffset_, memberOf, *m);

            // **A reference member is bound, not assigned**, and this is the one place
            // it can be. What the slot holds is an address, so the member is typed as
            // the pointer it really is and `bindReference` supplies that address.
            if (m->type->isReference()) {
                const Type *held = types_.pointerTo(m->type->referent());
                field->setType(held);
                ExprPtr addr = bindReference(m->type, std::move(found->second[0]),
                                             epos, "'" + m->name + "'");
                ExprPtr bind(new Assign(std::move(field), std::move(addr)));
                bind->setType(held);
                memberInits.push_back(StmtPtr(new ExprStmt(std::move(bind))));
                continue;
            }
            field->setType(m->type);

            std::map<std::string, std::size_t>::const_iterator vi =
                valueInit.find(m->name);
            if (vi != valueInit.end()) {
                const Type *mt = m->type->unqualified();
                const bool hasCtor = vi->second != functions_.size();
                // The zeroing first, cloned from the member's own access so
                // every store lands on the member; then the constructor, on
                // the member's address, when there is one to run.
                if (!hasCtor || functions_[vi->second].implicit)
                    if (ExprPtr chain = zeroChain(*field, mt))
                        memberInits.push_back(StmtPtr(new ExprStmt(std::move(chain))));
                if (hasCtor) {
                    const Type *ptr = types_.pointerTo(mt);
                    ExprPtr addr(new Unary('&', std::move(field)));
                    addr->setType(ptr);
                    std::vector<ExprPtr> one;
                    one.push_back(std::move(addr));
                    std::vector<const Type *> ps;
                    ps.push_back(ptr);
                    memberInits.push_back(StmtPtr(new ExprStmt(
                        completeCall(mt->tag(), functions_[vi->second].symbol,
                                     nullptr, types_.get(Kind::Void), ps, false,
                                     epos, std::move(one)))));
                }
                continue;
            }

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

    // A member function is the second argument: on the Microsoft ABI the hidden pointer
    // serves every class a member returns, whatever its size. Static member functions
    // are refused by name, so member and `this` cannot come apart here.
    int sretSlot = 0;
    if (d.type->isStructOrUnion() && returnsIndirectly(d.type, memberOf != nullptr)) {
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

    // **The by-value parameters Microsoft makes this function destroy.** A `return`
    // already unwinds everything the function owes, parameters included, so these are
    // appended for the one path that does not go through one: falling off the end.
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

    // **A polymorphic object's vptr is set by its constructor**, before the body and
    // after the base's - and by its destructor, for the same reason running the other
    // way: [class.cdtor]/4 makes a virtual call reach that level's own overrider.
    if (memberOf != nullptr && memberOf->polymorphic() &&
        (d.name == localOf(d.qualifier) ||
         d.name == "~" + localOf(d.qualifier))) {
        std::vector<StmtPtr> withVptr = storeVptrs(d.qualifier, memberOf, thisOffset_);
        withVptr.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withVptr)));
    }

    // **A constructor runs the base's first and a destructor runs it last**, which is
    // the order the standard fixes and clang emits. The base's C2 and D2 are what is
    // called, and on Windows there is one name for each, called directly.
    for (std::size_t bn = 0;
         memberOf != nullptr && bn < memberOf->bases().size() &&
         (d.name == localOf(d.qualifier) ||
          d.name == "~" + localOf(d.qualifier)); bn++) {
        const bool building = d.name == localOf(d.qualifier);
        // Bases are built in the order they were written and destroyed in the reverse.
        // **Both walk the list backwards**, because a constructor's call is prepended
        // to the body and a destructor's is appended; forwards put B before A.
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
            // **Held by value, like everything else that comes out of
            // overload resolution.** A pointer into `functions_` is a pointer
            // into a vector that anything parsed after this can move.
            Signature chosen;
            bool found = false;
            if (building && named != baseArgs.end()) {
                chosenArgs.swap(named->second);
                chosen = resolveOverload(key, chosenArgs, d.pos);
                found = true;
            } else if (building) {
                // No entry names this base, so its default constructor runs
                // - the one overload resolution with no arguments would
                // pick, which `S(int a = 1)` is as much as `S()` is.
                if (const Signature *dc = defaultConstructorOf(base)) {
                    chosen = *dc;
                    found = true;
                }
            } else {
                for (std::size_t k = 0; k < set->size(); k++)
                    if (functions_[(*set)[k]].params.empty()) {
                        chosen = functions_[(*set)[k]];
                        found = true;
                    }
            }
            if (!found)
                src_.fail(d.pos, "'" + base->tag() + "' has no constructor "
                                 "taking nothing - name one in the initialiser "
                                 "list, ': " + base->tag() + "(...)'");
            // **The defaults the entry left out are read here, as every other call
            // reads them.** This call is built by hand, one argument per parameter of
            // `chosen`, and it used to read one past the end of the vector and die.
            if (building) applyDefaults(chosen, chosenArgs, d.pos);

            std::string symbol = chosen.symbol;
            if (!target_.microsoftNames()) {
                std::string sub;
                if (building) {
                    const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                             chosen.params,
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
            for (std::size_t i = 0; i < chosen.params.size(); i++) {
                args.push_back(std::move(chosenArgs[i]));
                params2.push_back(chosen.params[i]);
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
    if (definedSymbol.empty())
        definedSymbol = lookupSignature(d.name, params, variadic, d.pos).symbol;

    // Recorded before `body` is moved into the Function - the expression the pointer
    // names is heap-allocated and goes on living there, which makes it safe to keep.
    // Only a definition has one; a declaration with no body folds through nothing.
    if (constexprFunction && body != nullptr) {
        const Expr *value = singleReturnValue(*body);
        if (value == nullptr)
            src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so in C++11 its "
                             "body has to be a single return statement and "
                             "nothing else - that restriction is what lets its "
                             "value be worked out while compiling");
        ConstexprFn fn;
        fn.value = value;
        fn.pos = d.pos;
        for (std::size_t i = 0; i < paramSlots.size(); i++)
            fn.slots.push_back(paramSlots[i].offset);
        constexprFns_[definedSymbol] = fn;
    }
    currentClass_ = nullptr;
    currentFunction_.clear();
    currentFunctionName_.clear();
    localTypes_.clear();
    // **`static` on a member says which member, not which linkage.**
    // [class.static]/1 against [basic.link]/3: on a free function the keyword
    // means internal linkage, and on a member it means there is no `this` - and
    // the member still has external linkage, so another translation unit can
    // call it. Read the wrong way round, a member defined inside its class came
    // out with no `.globl` and cl's object had nine symbols cxx1's did not.
    const bool internal = memberOf != nullptr ? inUnnamedNamespace_
                                              : internalLinkage(sc);
    program.functions.push_back(Function(d.name, emittedReturn, std::move(paramSlots),
                                         std::move(body), frame,
                                         internal, sretSlot,
                                         variadic, regSaveSlot, d.pos,
                                         std::move(fnVars_)));
    program.functions.back().setSymbol(definedSymbol);
    // The definition side of the same question: a member's first parameter is
    // its `this`, and on the Microsoft ABI that is what the hidden return
    // pointer has to come *after*.
    program.functions.back().setHasThis(!d.qualifier.empty());
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
    // **Each of these two can give the other more to do, so they alternate.**
    // A template's member body is replayed only once something uses it, and an
    // implicit constructor is synthesised only once something needs one - and a
    // synthesised constructor is a *use*. `inner<K> held_;` inside `outer<K>`
    // is the shape: `outer<int>`'s implicit constructor calls
    // `inner<int>::inner()`, and it was synthesised after the replay had
    // finished, so that constructor was declared under a name nothing emitted.
    // Run once each, the program linked only when the inner template happened
    // to be named at top level too.
    //
    // The loop ends when neither adds a function. The bound is a guard against
    // a cycle rather than a limit on depth: each pass emits at least one
    // function or stops, so a program needing more passes than this has
    // something else wrong with it.
    for (int pass = 0; pass < 64; pass++) {
        const std::size_t had = program.functions.size();
        instantiatePending();
        defineImplicitFunctions();
        if (program.functions.size() == had) break;
    }
    if (program.functions.empty())
        src_.fail(0, "the file defines no functions");
    return program;
}
