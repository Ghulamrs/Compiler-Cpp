// The parser: statements, and the top level.
//
// Declarations as statements, every control-flow statement including try and
// the range-based for, the goto labels resolved at the end of a function, and
// above them the top level that reads a translation unit and the parse() that
// drives the whole thing.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

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
        if (mentionsDeduced(d.type)) d.type = deduceAuto(d.type, d.name, d.pos);

        // An object of a class that declares constructors is built by calling
        // one, and that has to be asked before the branch below - `Point p(1)`
        // and a function declaration look the same until the type is known to
        // be a class with constructors.
        // **An array of a class with constructors.** The branch below asks
        // isStructOrUnion(), and an array of S is an array - so `S a[4]` fell
        // through to an ordinary uninitialised local and every element held
        // whatever was on the stack. It compiled, linked and ran, which made
        // it the one construction that was silently not happening: a member
        // array is built by the memberwise path and `new T[n]` is refused by
        // name, and only this had nothing at all.
        {
            const Type *elem = d.type;
            while (elem != nullptr && elem->isArray()) elem = elem->pointee();
            const Type *plain = elem == nullptr ? nullptr : elem->unqualified();
            if (d.type->isArray() && plain != nullptr &&
                plain->isStructOrUnion() && !plain->tag().empty() &&
                overloadsOf(constructorKey(plain->tag())) != nullptr) {
                if (sc == StorageStatic)
                    src_.fail(d.pos, "'" + d.name + "' is static and its "
                                     "elements have a constructor - running one "
                                     "before main is not supported yet");
                if (peek().is("(") || peek().is("="))
                    src_.fail(d.pos, "an initialiser for an array of '" +
                                     plain->describe() + "' is not supported "
                                     "yet - each element gets the default "
                                     "constructor");
                // **Destruction is refused rather than half-built.**
                // [class.dtor] destroys the elements in reverse, and the place
                // that emits a scope's destructors is shared with the
                // exception paths on all three targets. One object per entry
                // is what it knows; teaching it a count belongs with that
                // machinery and not beside a declaration. Refused by name here
                // so that nothing is silently left undestroyed - which is the
                // failure this whole branch exists to stop.
                if (destructorOf(plain) != nullptr)
                    src_.fail(d.pos, "an array of '" + plain->describe() +
                                     "' is not supported yet because it has a "
                                     "destructor, and the elements would have "
                                     "to be destroyed in reverse when the scope "
                                     "ends - an array of a class with only "
                                     "constructors works");
                int off = declare(d.name, d.type, d.pos);
                int indexSlot = allocateFrameSlot(types_.intType());
                inits.push_back(constructLocalArray(d, off, indexSlot));
                if (!consume(",")) break;
                continue;
            }
        }

        if (d.type->isStructOrUnion() && !d.type->tag().empty() &&
            overloadsOf(constructorKey(d.type->tag())) != nullptr) {
            if (sc == StorageStatic)
                src_.fail(d.pos, "'" + d.name + "' is static and has a "
                                 "constructor - running one before main is not "
                                 "supported yet");
            // **A braced initialiser, and the two different answers it
            // has.** [dcl.init.aggr]/1 in C++11 says a class with a member
            // initialiser is *not* an aggregate, so `S s = {1, 2}` on one is
            // ill-formed here and legal from C++14 - a rule this compiler has
            // to keep on the C++11 side of, and the only place where having
            // written member initialisers changes what an older program
            // means. Where the class wrote none, the braces are asking for
            // list-initialisation through a constructor, which is C++11 and
            // simply is not built yet.
            if (peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) {
                if (hasMemberInitialiser(d.type->tag()))
                    src_.fail(d.pos, "'" + d.type->describe() + "' writes an "
                                     "initialiser on a member, so in C++11 it "
                                     "is not an aggregate and a braced list "
                                     "cannot initialise it - C++14 changed "
                                     "that rule and this compiler is C++11. "
                                     "Give the class a constructor, or take "
                                     "the member initialiser off");
                src_.fail(d.pos, "list-initialisation - '" + d.name +
                                 "{...}' calling a constructor - is not "
                                 "supported yet; write the arguments in "
                                 "parentheses");
            }
            std::vector<ExprPtr> args;
            bool copyInit = false;
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
                // for anything else. What separates it from `X b(a);` is that
                // an `explicit` constructor may not be picked here, which
                // `constructLocal` is told below.
                copyInit = true;
                args.push_back(assign());
            }

            // **An elided copy still needs a copy constructor that may be
            // chosen.** [class.copy]/31 selects and checks the constructor
            // even where the copy itself is elided, so `S b = make();` is
            // refused when S's copy constructor is `explicit` though nothing
            // would have called it. Checked here because the two branches
            // below - copy elision and the trivial-copy store - both reach
            // past `constructLocal`, which is where the rule otherwise lives.
            if (copyInit && args.size() == 1 && args[0]->type() != nullptr &&
                args[0]->type()->unqualified() == d.type->unqualified())
                if (const Signature *cc = copyConstructorOf(d.type->unqualified()))
                    if (cc->isExplicit)
                        src_.fail(d.pos, "'" + d.type->describe() + "' has an "
                                         "'explicit' copy constructor, so it "
                                         "will not be chosen for '" + d.name +
                                         " = ...' - write '" +
                                         d.type->describe() + " " + d.name +
                                         "(...)'. The copy may well be elided, "
                                         "and the rule is checked all the same");

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
            // **A class that declares a move constructor has no trivial
            // copy**, and reading this branch as though it did is how a
            // move-only class came to be copied byte for byte.
            // [class.copy]/7: declaring a move constructor *deletes* the
            // implicit copy. Here the copy was never declared either, which
            // looks the same to `copyConstructorOf` and means something
            // entirely different - so the byte path answered, the move
            // constructor never ran, and two objects owned one resource.
            //
            // With the move constructor in the way, the initialisation goes
            // through `constructLocal` and overload resolution picks between
            // the constructors the class actually has: the move for an
            // xvalue, and nothing at all for an lvalue - which is the
            // deletion, and is refused just below rather than left to a
            // resolution failure that would name the wrong reason.
            const Signature *mover = moveConstructorOf(d.type->unqualified());
            const bool sameClass =
                args.size() == 1 && args[0]->type() != nullptr &&
                args[0]->type()->unqualified() == d.type->unqualified();
            if (mover != nullptr && sameClass && !args[0]->isXvalue() &&
                copyConstructorOf(d.type->unqualified()) == nullptr)
                src_.fail(d.pos, "'" + d.type->describe() + "' declares a move "
                                 "constructor, so its copy constructor is "
                                 "deleted and '" + d.name + "' cannot be built "
                                 "from an lvalue - write 'static_cast<" +
                                 d.type->describe() + " &&>(...)' to move out "
                                 "of it, or give the class a copy constructor");

            const bool trivialCopy =
                sameClass && mover == nullptr &&
                copyConstructorOf(d.type->unqualified()) == nullptr;

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
                inits.push_back(constructLocal(d, off, std::move(args), copyInit));
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
        if (hasInit) {
            long long value = 0;
            if (constantInitialiser(d.type, in, &value)) {
                locals_.back().isConstantValue = true;
                locals_.back().constantValue = value;
            } else if (quals.isConstexpr) {
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so its value "
                                 "has to be known while this is compiled, and "
                                 "this initialiser is not a constant expression");
            }
        } else if (quals.isConstexpr) {
            src_.fail(d.pos, "'" + d.name + "' is 'constexpr' and has no "
                             "initialiser - there is nothing for it to be");
        }

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

// **A declaration followed by `:` rather than `;`.** Telling that from
// `for (int x = a ? b : c; ...)` is the whole difficulty: a `?` claims the
// next `:`, so they are counted. `::` is one token from the lexer and cannot
// be mistaken for this one.
bool Parser::atRangeFor() const {
    int depth = 0;
    int question = 0;
    for (std::size_t k = 0; ; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return false;
        if (t.is("(") || t.is("[")) { depth++; continue; }
        if (t.is(")") || t.is("]")) {
            if (depth == 0) return false;
            depth--;
            continue;
        }
        if (depth != 0) continue;
        if (t.is(";")) return false;
        if (t.is("?")) { question++; continue; }
        if (t.is(":")) {
            if (question > 0) { question--; continue; }
            return true;
        }
    }
}

// **[stmt.ranged] is a rewrite, and this does the rewrite.** The standard
// says what `for (T x : a)` means by writing another loop, and every node
// that loop needs was already here:
//
//     T *__b = a;            the array, decayed
//     T *__e = __b + N;
//     for (; __b != __e; __b = __b + 1) { T x = *__b; <body> }
//
// The range is evaluated exactly once, which is what binding it to a name
// buys in the standard's version and what assigning it to `__b` buys here.
StmtPtr Parser::rangeForStatement(int scope) {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);
    expect(":");

    const std::size_t rpos = peek().pos;
    ExprPtr range = expr();
    expect(")");

    const Type *rt = range->type();
    if (!rt->isArray())
        src_.fail(rpos, "a range-based 'for' over anything but an array is "
                        "not supported yet - a class would need its begin() "
                        "and end() looked up and called, which is its own "
                        "step");
    if (rt->length() < 0)
        src_.fail(rpos, "this array has no length, so there is nothing to "
                        "stop at");
    if (d.type->isReference())
        src_.fail(d.pos, "a reference in a range-based 'for' is not supported "
                         "yet - the loop variable is copied for now");

    const Type *elem = rt->pointee();
    const Type *elemPtr = types_.pointerTo(elem);
    if (mentionsDeduced(d.type))
        d.type = deduceAutoFrom(d.type, elem, d.name, d.pos);

    // `T *__b = a;` - the array decayed, evaluated here and nowhere else.
    const int bSlot = declare(".rb" + std::to_string(refTemps_), elemPtr, rpos);
    const std::string bName = ".rb" + std::to_string(refTemps_++);
    ExprPtr b(Var::local(bName, bSlot));
    b->setType(elemPtr);
    std::vector<StmtPtr> setup;
    ExprPtr startAt(new Assign(std::move(b), decay(std::move(range))));
    startAt->setType(elemPtr);
    setup.push_back(StmtPtr(new ExprStmt(std::move(startAt))));

    // `T *__e = __b + N;`
    const int eSlot = declare(".re" + std::to_string(refTemps_), elemPtr, rpos);
    const std::string eName = ".re" + std::to_string(refTemps_++);
    ExprPtr from(Var::local(bName, bSlot));
    from->setType(elemPtr);
    ExprPtr count(new Num(rt->length()));
    count->setType(types_.get(target_.sizeType()));
    // **Through `arithmetic`, not a bare Binary.** `p + 1` on an `int *`
    // advances four bytes, and that scaling lives in the helper the ordinary
    // expression path uses. Building the node by hand and stamping a type on
    // it produced a loop that read the array one byte at a time - the first
    // element right and every one after it garbage, which is what a missing
    // scale looks like.
    ExprPtr past = arithmetic(BinOp::Add, std::move(from), std::move(count),
                              rpos);
    ExprPtr e(Var::local(eName, eSlot));
    e->setType(elemPtr);
    ExprPtr stopAt(new Assign(std::move(e), std::move(past)));
    stopAt->setType(elemPtr);
    setup.push_back(StmtPtr(new ExprStmt(std::move(stopAt))));

    // `__b != __e`
    ExprPtr atB(Var::local(bName, bSlot));
    atB->setType(elemPtr);
    ExprPtr atE(Var::local(eName, eSlot));
    atE->setType(elemPtr);
    ExprPtr cond = comparison(BinOp::Ne, std::move(atB), std::move(atE), rpos);

    // `__b = __b + 1`
    ExprPtr stepFrom(Var::local(bName, bSlot));
    stepFrom->setType(elemPtr);
    ExprPtr one(new Num(1LL));
    one->setType(types_.get(target_.sizeType()));
    ExprPtr next = arithmetic(BinOp::Add, std::move(stepFrom), std::move(one),
                              rpos);
    ExprPtr stepTo(Var::local(bName, bSlot));
    stepTo->setType(elemPtr);
    ExprPtr step(new Assign(std::move(stepTo), std::move(next)));
    step->setType(elemPtr);

    // The body, with the loop variable built from `*__b` in front of it.
    enterScope();
    const int inner = enterBlock();
    const int vSlot = declare(d.name, d.type, d.pos);
    ExprPtr through(Var::local(bName, bSlot));
    through->setType(elemPtr);
    ExprPtr at(new Unary('*', std::move(through)));
    at->setType(elem);
    ExprPtr var(Var::local(d.name, vSlot));
    var->setType(d.type);
    ExprPtr take(new Assign(std::move(var), convert(std::move(at), d.type)));
    take->setType(d.type);

    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(take))));
    loopDepth_++;
    body.push_back(statement());
    loopDepth_--;
    leaveBlock();
    leaveScope();
    Block *inside = new Block(std::move(body));
    inside->setScope(inner);

    For *f = new For(StmtPtr(), std::move(cond), std::move(step),
                     StmtPtr(inside));
    f->setScope(scope);
    setup.push_back(StmtPtr(f));

    leaveBlock();
    leaveScope();
    Block *whole = new Block(std::move(setup));
    whole->setScope(-1);
    return StmtPtr(whole);
}

StmtPtr Parser::forStatement() {
    expect("for");
    expect("(");
    enterScope();
    int scope = enterBlock();

    if (atRangeFor()) return rangeForStatement(scope);

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

// **`static_assert(cond, "message");` - a declaration that declares nothing and
// emits nothing.** All of it happens here: the condition is folded, and a zero
// is a diagnostic carrying the program's own words. Nothing reaches the AST, so
// no backend and no emitter had to learn it exists.
//
// **The message is required.** C++17 made it optional and C++11 did not, so
// the one-argument form is refused by name - accepting it would make a file
// that builds here stop building on the C++11 compiler it was written for,
// which is the same reason `namespace N::M {}` is refused.
//
// **The condition has to be an integral constant expression**, which is
// narrower than the standard's "contextually converted constant expression of
// type bool" - `static_assert(1.5, "")` and a string literal are both legal
// and both refused here. Neither is a thing anybody writes, and `fold` answers
// about integers; widening it is constant-evaluation work rather than
// static_assert work.
//
// Written as one function because the three places a static_assert may appear
// - file scope, a block, and a class body - are three different loops in three
// different files, and the rule is one rule.
bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In
// C++11 it is *not* part of the function's type - measured, `void f() noexcept`
// and `void f()` both mangle to `_Z1fv` on Itanium and `?f@@YAXXZ` on
// Microsoft - so nothing about a name, a signature match or an overload set
// changes here. That is a C++17 rule, and this compiler targets C++11.
//
// What it does buy is a compile-time answer: the `noexcept(e)` operator asks
// whether every function `e` calls has promised not to throw.
//
// `throw()` is the C++03 spelling of `noexcept` and is taken as one.
// `throw(int)` is the *dynamic* exception specification, a different feature
// that would need a run-time check against a type list - it is refused by name
// rather than quietly read as `throw()`, which would be a promise the program
// did not make.
bool Parser::exceptionSpecification() {
    if (consume("noexcept")) {
        if (!consume("(")) return true;
        const std::size_t at = peek().pos;
        const long long value = constantExpression("a constant in 'noexcept('");
        expect(")");
        (void) at;
        return value != 0;
    }
    if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (!consume(")"))
            src_.fail(at, "a dynamic exception specification - 'throw(T)' - is "
                          "not supported yet; it needs a run-time check of the "
                          "thrown type against a list, where 'noexcept' is a "
                          "promise the compiler only has to record. 'throw()' "
                          "with nothing in it is 'noexcept' and works");
        return true;
    }
    return false;
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
    // **A name, when it names a constant.** The object is real and has an
    // address; what is answered here is what it is worth when read, which
    // [expr.const] says a const integral object initialised by a constant
    // expression may be asked for. Locals first, because a local of the same
    // name shadows the global - the same order every other lookup uses.
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        // **Inside a constexpr call, a local name is a parameter.** The body
        // being folded belongs to another function entirely, so its Vars name
        // slots in a frame that does not exist - what they are worth is what
        // the call was given, and that is on the top of this stack. Only the
        // top: a recursive call pushes its own, and the same slot numbers mean
        // that call's arguments while it is being read.
        if (v->isLocal() && !constexprFrames_.empty()) {
            const std::vector<std::pair<int, long long> > &frame =
                constexprFrames_.back();
            for (std::size_t i = 0; i < frame.size(); i++)
                if (frame[i].first == v->offset()) { *out = frame[i].second; return true; }
        }
        if (v->isLocal()) {
            if (const Local *l = findLocal(v->name()))
                if (l->isConstantValue) { *out = l->constantValue; return true; }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name()))
            if (g->isConstantValue) { *out = g->constantValue; return true; }
        return false;
    }
    // **A call to a constexpr function.** C++11 lets its body be one return
    // statement, so running it is folding that expression with the parameters
    // standing for the arguments - no statements to step through, no state to
    // carry, and recursion falls out of the folding being recursive already.
    //
    // A call to anything else simply does not fold, which is the answer the
    // contexts that ask want: an array bound says it is not a constant
    // expression and names the place, rather than this deciding what to say.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        auto it = constexprFns_.find(c->symbol());
        if (it == constexprFns_.end()) return false;
        const ConstexprFn &fn = it->second;
        if (fn.value == nullptr || c->args().size() != fn.slots.size())
            return false;

        // A recursion that does not end is a compiler that does not either.
        // The standard lets an implementation set a limit and say so; this is
        // that limit, and it is said where it is reached.
        if (constexprFrames_.size() >= 256)
            src_.fail(pos, "this constant expression is more than 256 calls "
                           "deep - a 'constexpr' function that never stops "
                           "recursing cannot be worked out while compiling");

        std::vector<std::pair<int, long long> > frame;
        for (std::size_t i = 0; i < c->args().size(); i++) {
            long long v = 0;
            // **Folded outside the new frame, in the caller's.** An argument
            // is an expression where the call is written, so `fact(n - 1)`
            // reads the *caller's* n; folding it after the push would read
            // the callee's parameter of the same slot instead.
            if (!fold(*c->args()[i], &v, pos)) return false;
            frame.push_back(std::make_pair(fn.slots[i], v));
        }
        constexprFrames_.push_back(frame);
        long long result = 0;
        const bool ok = fold(*fn.value, &result, pos);
        constexprFrames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
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

// **[expr.const]/3: a named integral constant is a constant expression.** A
// const object of integral or enumeration type, initialised with a constant
// expression, is one - which is the rule that lets C++ write
// `const int n = 4; int a[n];` where C has to use a macro or an enum. It is
// the whole of what a `constexpr` *variable* adds over `const`, minus the
// demand that the initialiser really is constant.
//
// Only integral, because that is what fold() answers in. A const double is a
// constant expression in C++ too and is not one here; nothing asks for a
// floating constant expression, since every context that wants one - array
// bounds, case labels, enumerators, non-type template arguments - wants an
// integer.
// The one expression a C++11 `constexpr` function body is allowed to be.
// Answers null for anything else, and the caller turns that into the
// diagnostic - which has to name the restriction, because a body that would
// be perfectly ordinary in C++14 is refused here.
//
// A block wrapping a block is unwrapped: nothing in this parser makes one for
// a plain `{ return e; }`, but a body that has been wrapped for cleanups or a
// constructor's member initialisers would be, and being tolerant costs a loop.
const Expr *Parser::singleReturnValue(const Stmt &body) const {
    const Stmt *at = &body;
    for (;;) {
        const Block *b = dynamic_cast<const Block *>(at);
        if (b == nullptr) break;
        if (b->body().size() != 1) return nullptr;
        at = b->body()[0].get();
    }
    const Return *r = dynamic_cast<const Return *>(at);
    if (r == nullptr || !r->hasValue()) return nullptr;
    return &r->value();
}

bool Parser::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    if (t == nullptr || !t->isConst() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return fold(*in.value, out, in.pos);
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
    // A `using namespace` written inside this block reaches the '}' and no
    // further - [namespace.udir]/2 - so the list is cut back to what it held
    // on the way in.
    const std::size_t usingAtEntry = usingNamespaces_.size();
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

    if (!built.empty()) {
        if (functionHasTry_ || inTryBody_)
            src_.fail(pos, "a local with a destructor and a 'try' in one "
                           "function is not supported yet - each is a range in "
                           "the call-site table and one would have to split "
                           "the other");
        body = target_.microsoftNames()
                   ? wrapMsCleanups(std::move(body), built, aliveAtEntry, pos)
                   : wrapCleanups(std::move(body), built, aliveAtEntry, pos);
    }
    alive_.resize(aliveAtEntry);
    usingNamespaces_.resize(usingAtEntry);

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
    // **The two ABIs disagree about who picks the handler**, so this reads one
    // grammar and builds two shapes. Itanium hands the frame an exception
    // pointer and a selector and lets the frame's own code decide, which is
    // the if/else chain below. Microsoft decides in the runtime, from tables,
    // and *calls* the chosen handler as a function of its own - so there the
    // handlers are kept whole and none of __cxa_begin_catch, the selector or
    // _Unwind_Resume is built at all.
    const bool microsoft = target_.microsoftNames();
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
    std::vector<MsHandler> msHandlers;
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

        // **The Microsoft handler is the block and nothing else.** The runtime
        // has already chosen it, already made the caught object in the frame
        // slot the table names, and will end the catch itself when the funclet
        // returns - so every one of the three calls the Itanium shape wraps
        // the body in has a counterpart that is not this frame's business.
        if (microsoft) {
            MsHandler mh;
            if (caught != nullptr) {
                MicrosoftThrow names;
                std::string why;
                if (!microsoftThrowNames(caught, caught->size(target_),
                                         &names, &why))
                    src_.fail(cpos, "'catch' cannot name this type: " + why);
                mh.descriptor = names.descriptor;
                mh.objectSize = caught->size(target_);
                // The descriptor is emitted by the same pass that emits a
                // thrown type's, so a type that is only ever *caught* has to
                // join that list or the handler map would name a symbol
                // nothing defines.
                bool had = false;
                for (std::size_t k = 0; k < current_->thrown.size(); k++)
                    if (current_->thrown[k] == caught) had = true;
                if (!had) current_->thrown.push_back(caught);
                if (!caughtName.empty())
                    mh.objectSlot = declare(caughtName, caught, cpos);
            }
            if (!peek().is("{")) src_.fail(peek().pos, "'catch' takes a block");
            const bool wasInHandler = inMsHandler_;
            inMsHandler_ = true;
            mh.body = block();
            inMsHandler_ = wasInHandler;
            leaveScope();
            msHandlers.push_back(std::move(mh));
            continue;
        }

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

    // Microsoft: no chain to build, because nothing in this frame chooses.
    if (microsoft) {
        std::vector<StmtPtr> guardedMs;
        guardedMs.push_back(std::move(body));
        Try *t = new Try(std::move(guardedMs), nullptr, pointerSlot,
                         selectorSlot, std::move(types));
        t->setHandlers(std::move(msHandlers));
        // The runtime's scratch word, which the personality routine finds
        // through the FuncInfo's dispUnwindHelp and the parent sets to -2 on
        // entry. A frame slot like any other, so that the whole of where it
        // lives is decided by the code that decides where locals live.
        t->setUnwindHelpSlot(allocateFrameSlot(voidPtr));
        return StmtPtr(t);
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
    // A static_assert declares nothing and builds nothing, so the statement it
    // becomes is the empty one - the same shape the using-directive takes.
    if (staticAssertion()) return StmtPtr(new Block({}));

    // **`using namespace N;` inside a block**, which is the same directive as
    // the one at file scope and differs only in when it stops applying: at the
    // end of this block, which `block()` undoes by truncating the list. It
    // declares nothing and builds nothing, so the statement it becomes is the
    // empty one.
    if (peek().is("using") && peekAt(1).is("namespace")) {
        at_ += 2;
        std::string opened = expectIdent("a namespace name");
        while (peek().is("::")) {
            at_++;
            opened += "::" + expectIdent("a namespace name");
        }
        expect(";");
        if (namespaces_.find(opened) == namespaces_.end())
            src_.fail(peek().pos, "'" + opened + "' is not a namespace");
        usingNamespaces_.push_back(opened);
        return StmtPtr(new Block({}));
    }

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
        mayThrow_++;
        ExprPtr value = decay(expr());
        expect(";");
        return throwStatement(std::move(value), tpos);
    }

    if (peek().is("return") && inMsHandler_)
        src_.fail(peek().pos, "'return' inside a 'catch' is not supported yet "
                              "for x86_64-windows - a handler is compiled as a "
                              "function of its own there, and leaving it means "
                              "handing back the address to carry on at in the "
                              "register a return value would travel in");
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
            // **[class.copy]/31 again: returning by value copy-initializes the
            // caller's object**, so its copy constructor is selected and
            // checked even though the copy is elided a few lines below. A
            // class whose copy constructor is `explicit` cannot be returned by
            // value at all - the function is ill-formed on its own, before any
            // caller is looked at, which is the surprising half and the reason
            // it is checked here rather than at the call.
            if (returnType_->isStructOrUnion() && value->type() != nullptr &&
                value->type()->unqualified() == returnType_->unqualified())
                if (const Signature *cc = copyConstructorOf(returnType_->unqualified()))
                    if (cc->isExplicit)
                        src_.fail(pos, "'" + returnType_->describe() + "' has an "
                                       "'explicit' copy constructor, so it "
                                       "cannot be returned by value - 'return' "
                                       "copy-initialises the caller's object, "
                                       "and that may not pick an explicit "
                                       "constructor even where the copy is "
                                       "elided");
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
        // **[class.copy]/31 names one automatic object and excludes a
        // parameter**, and the exclusion is the whole point rather than a
        // detail: the caller made the argument and the caller destroys it, so
        // eliding here leaves the caller holding two objects over one set of
        // bytes and destroying both. `T pass(T t) { return t; }` built two
        // objects and destroyed three - a double free for any class that owns
        // anything.
        bool elidable = false;
        if (const Var *v = dynamic_cast<const Var *>(value.get()))
            if (v->isLocal()) {
                const Local *l = findLocal(v->name());
                elidable = l == nullptr || !l->isParameter;
            }

        // **A `return` of a glvalue this function does not own has to call
        // the copy constructor**, and until now nothing did: the return path
        // moved the bytes and left the elision of the *destructor* to stand
        // in for the copy. That works for exactly one case - an automatic
        // object of this function, where the standard also allows the copy
        // itself to go - and is wrong for every other: a parameter, a member,
        // `*p`. Each of those left the caller with a byte copy that no
        // constructor had made and that the source would also destroy.
        //
        // The copy is built into a slot of this frame and *that* slot is the
        // one elided: one constructor runs, its bytes become the caller's
        // object, and nothing destroys it here. That is the copy the standard
        // asks for plus the elision it allows, which is what clang emits.
        std::vector<StmtPtr> before;
        if (returnType_->isStructOrUnion() && !elidable &&
            value->type() != nullptr &&
            value->type()->unqualified() == returnType_->unqualified() &&
            isGlvalue(*value) &&
            (copyConstructorOf(returnType_->unqualified()) != nullptr ||
             moveConstructorOf(returnType_->unqualified()) != nullptr)) {
            Declared rv;
            rv.name = ".rv" + std::to_string(refTemps_++);
            rv.type = returnType_;
            rv.pos = pos;
            const int slot = declare(rv.name, rv.type, pos);
            std::vector<ExprPtr> one;
            one.push_back(std::move(value));
            before.push_back(constructLocal(rv, slot, std::move(one), true));
            ExprPtr built(Var::local(rv.name, slot));
            built->setType(returnType_);
            value = std::move(built);
            elidable = true;
        }

        int elided = -1;
        if (returnType_->isStructOrUnion() &&
            destructorOf(returnType_) != nullptr && elidable)
            if (const Var *v = dynamic_cast<const Var *>(value.get()))
                if (v->isLocal()) elided = v->offset();

        if (!alive_.empty()) {
            std::vector<StmtPtr> unwind;
            for (std::size_t i = 0; i < before.size(); i++)
                unwind.push_back(std::move(before[i]));
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
        if (!before.empty()) {
            before.push_back(StmtPtr(new Return(std::move(value))));
            return StmtPtr(new Block(std::move(before)));
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
    // `namespace N { ... }` - a scope that qualifies what is declared in it,
    // and nothing else. Everything inside is read by this same function, so a
    // namespace nests, may be reopened, and may hold anything a file may hold.
    if (peek().is("namespace")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (peek().is("{"))
            src_.fail(pos, "an unnamed namespace is not supported yet - what it "
                           "does is give everything inside internal linkage, "
                           "which 'static' says here");
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
        if (!peek().is("namespace"))
            src_.fail(pos, "a using-declaration is not supported yet - "
                           "'using namespace N;' opens a whole namespace and "
                           "works");
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

    // **A `constexpr` function is 7.5b and is refused by name until then.**
    // Accepting it and treating it as an ordinary function would compile and
    // run correctly - a constexpr function may always be called at run time -
    // and would then quietly fail to be constant in the one place the keyword
    // was written for, with the reader told only that an array bound is not
    // constant. Evaluating a call needs an interpreter over the AST, which is
    // a second execution model beside the three backends.
    // The '(' is still ahead at this point - a function is told from an
    // object here by what follows the declarator, not by its type, which is
    // still the *return* type. The three ways of arriving are a definition or
    // declaration written out (`peek().is("(")`), one whose parameters have
    // been recorded to re-read (`paramsAt`), and one declared through a
    // typedef, whose type really is a function type.
    const bool constexprFunction =
        quals.isConstexpr &&
        (peek().is("(") || d.paramsAt != 0 || d.type->isFunction());

    // **`constexpr` does not make the return type const**, and it is measured
    // rather than reasoned: cl and clang both spell `constexpr int sq(int)` as
    // ?sq@@YAHH@Z, which is `H` for int and not `?BH` for const int. The
    // keyword sets isConst because on an *object* that is exactly what it
    // means; on a function it must be taken off again or every constexpr
    // function would carry a name no other compiler writes.
    if (constexprFunction && !d.type->isFunction())
        d.type = types_.withoutConst(d.type);

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

            // **A class with a constructor, at file scope.** The local path
            // refuses a `static` one by name for want of the mechanism that
            // runs it before main; this path had no such test at all, so the
            // object was laid out as bytes and the constructor never ran -
            // `S s;` at file scope read 0 where the constructor had written
            // 7, and it compiled, linked and ran. A silently missing
            // construction is the one failure worth a refusal.
            //
            // The braced form is asked first and separately: a class that
            // writes a member initialiser is not an aggregate in C++11
            // ([dcl.init.aggr]/1) and *is* one from C++14, so what a reader
            // needs to be told there is which standard refuses them, not
            // which mechanism is missing.
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
            if (consume("=")) {
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

            // A variable declared in a namespace is keyed and mangled by its
            // qualified name, the same as a function. `extern "C"` does not
            // reach into one, so a name with C linkage keeps what it was
            // written with.
            const std::string gname =
                (namespaceStack_.empty() || cLinkage_ > 0)
                    ? d.name : namespacePrefix() + d.name;
            globalIndex_[gname] = globals_.size();
            bool objectIsConst = d.type->isConst();
            // A const object at namespace scope has internal linkage of its
            // own - [basic.link]/3 - which is why a header may define one and
            // C, where it would be external, may not. Nothing outside can
            // name it, so it keeps the name it was written with.
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

    // **A trailing return type is C++11, and it arrives here wearing the same
    // `auto`.** `auto f(int) -> int` says what the return type is rather than
    // asking for it to be deduced, so blaming C++14 is wrong twice over: the
    // standard is the wrong one, and the reader is told a type cannot be
    // worked out when they had written it down.
    // The parameter list is still ahead here - it was recorded to be read
    // again, not consumed - so the arrow is found by stepping over it.
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
        // `void S::f()` written inside `namespace N` defines `N::S::f`, and
        // every table downstream is keyed by the qualified tag - the member
        // lookup, the mangled name, the constructor test against
        // `localOf(qualifier)`. Take the name the class was found under rather
        // than the one that was written.
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
                // A default written on the *definition*. The parameter list
                // here is read by this loop and not by parameterTypes, so the
                // same recording has to happen twice - and it is recorded the
                // same way, as a place in the token stream.
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

    // Hand what this parameter list collected to whichever declare() runs
    // below, the same way parameterTypes hands over its own. **Before the
    // prototype branch and not after it**: `int f(int a, int b = 3);` declared
    // here and defined further down is the ordinary way to write one, and that
    // branch returns as soon as it has declared the function.
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
    // The same C++11 rule the class body applies: a `constexpr` member
    // function is implicitly const. This is the path a member *defined* inside
    // its class comes back through when its held body is replayed, so leaving
    // it out here makes the definition disagree with its own declaration -
    // "'B' declares no member 'twice' with these parameters".
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
        // **This used to write `member->pos` into the *first* overload's
        // entry**, which is a no-op when the member being defined is that
        // one and a corruption of somebody else's recorded position when it
        // is not: `int S::f(double)` defined out of line moved `f(int)`'s
        // declaration position onto itself. Nothing reads a user overload's
        // `pos` today, which is why it never showed - and the line was doing
        // nothing that was wanted in either case. The assignment on the next
        // line is the whole of what this branch has to record.
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
        // Which function's body is about to be read, so that an access check
        // inside it can ask whether a class befriended *this* function. A
        // member's is left empty on purpose: the qualified form that would
        // make a member somebody's friend is refused where it is written.
        currentFunction_ = lookupSignature(d.name, params, variadic, d.pos).symbol;
    }
    // Set for a member's body too, unlike the friend question above, because
    // a local class inside a member function is spelled by wrapping *that*
    // function's name - `_ZZN5Outer1mEvEN1A3getEv`, measured. It cannot make a
    // member look like somebody's friend by accident: a friend list only ever
    // holds the symbols of free functions, the qualified form that would name
    // a member being refused where it is written.
    // **Taken by value, and that is the whole of a bug that predates local
    // classes.** `member` points into `functions_`, which is a vector: any
    // declaration made while this function's body is read can grow it and
    // move it, and the pointer is then reading freed memory. Nothing did
    // that until a class could be defined inside a function - its member
    // functions are declared during the body - and `Outer::m` came out
    // carrying whatever string happened to be at that address, `m` in one
    // build and `a` in the next. Only the symbol is wanted afterwards, so
    // only the symbol is kept.
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
    // Which members this constructor's own list covers. Kept out here because
    // the initialisers the class wrote are applied to the rest, below, and the
    // list itself is scoped to the block that reads it.
    std::set<std::string> namedInInit;
    std::map<std::string, std::vector<ExprPtr> > baseArgs;
    std::map<std::string, std::vector<ExprPtr> > memberExprs;
    std::map<std::string, std::size_t> where;
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

    // **Every member, in declaration order, by the first of three rules that
    // applies to it** - [class.base.init]/8 and /9, and /11 for the order:
    //
    //   named in the list      built from what the list gave it: a class
    //                          with constructors is *constructed* with those
    //                          arguments, a reference is bound, anything
    //                          else is assigned one value;
    //   has its own initialiser   `int x = 1;` on the member, read afresh
    //                          here - so `S(int a) : x(a) {}` on a class
    //                          with `int x = 1; int y = 2;` sets x from a
    //                          and y from 2;
    //   a class with constructors   default-constructed. This is the rule
    //                          that was missing: a written constructor left
    //                          such a member unbuilt and the compiler's
    //                          destructor then destroyed it.
    //
    // A union's members are not built; which one is alive is the program's
    // business, and its constructor says so with the list or not at all.
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

            if (!m->type->isReference()) {
                StmtPtr built = constructMember(d.qualifier, memberOf, *m,
                                                thisOffset_, found->second,
                                                epos, false);
                if (built != nullptr) {
                    memberInits.push_back(std::move(built));
                    continue;
                }
            }
            if (found->second.size() != 1)
                src_.fail(epos, "'" + m->name + "' takes one value here, "
                                "given " + std::to_string(found->second.size()));

            ExprPtr me(Var::local("this", thisOffset_));
            me->setType(types_.pointerTo(memberOf));
            ExprPtr obj(new Unary('*', std::move(me)));
            obj->setType(memberOf);
            ExprPtr field(new MemberAccess(std::move(obj), m->name, m->offset,
                                           m->width, m->bitOffset));

            // **A reference member is bound, not assigned**, and this is the
            // one place it can be: the mem-initialiser list. What the slot
            // holds is an address, so the member is typed as the pointer it
            // really is and `bindReference` supplies the address - the same
            // road a reference local's initialiser takes.
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
    // **And by its destructor, for the same reason running the other way.**
    // [class.cdtor]/4: a virtual call from a destructor reaches the final
    // overrider *in that destructor's class*, so as each level is torn down
    // the object stops being what the level below it built. Constructors
    // stored the pointer and destructors never did, so during `~A` the object
    // still claimed to be a `B` and a virtual call from there ran B's
    // override against a subobject B had already finished destroying. The
    // store goes in front of the body and the base's destructor is appended
    // after it, which is the order the standard fixes.
    if (memberOf != nullptr && memberOf->polymorphic() &&
        (d.name == localOf(d.qualifier) ||
         d.name == "~" + localOf(d.qualifier))) {
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
            // **The defaults the entry left out are read here, as every
            // other call reads them.** This call is built by hand below,
            // one argument per parameter of `chosen`, and it used to walk
            // `chosen.params` against a `chosenArgs` that held only what
            // was written - `: Base(1)` against `Base(int, int = 6)` read
            // one past the end of the vector, and the compiler died.
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

    // Recorded before `body` is moved into the Function - the expression the
    // pointer names is heap-allocated and goes on living there, which is what
    // makes it safe to keep. Only a definition has one; a `constexpr`
    // declaration with no body is a promise nothing can be folded through
    // yet, and calling it in a constant expression says so where it is called.
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
    program.functions.push_back(Function(d.name, emittedReturn, std::move(paramSlots),
                                         std::move(body), frame,
                                         sc == StorageStatic, sretSlot,
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
    instantiatePending();
    defineImplicitFunctions();
    if (program.functions.empty())
        src_.fail(0, "the file defines no functions");
    return program;
}
