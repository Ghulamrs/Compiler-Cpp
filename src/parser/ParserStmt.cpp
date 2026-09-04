// The parser: statements. Declarations as statements, every control-flow
// statement including try and the range-based for, and the goto labels resolved
// at a function's end.
//
// Two neighbours were split out of this file when it outgrew being read in one
// sitting: ParserConst.cpp holds the constant folding a statement is checked
// against, and ParserTopLevel.cpp holds what a translation unit is made of.
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
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same type,
            // which is what makes the C idiom "typedef struct S S;" legal now that
            // the tag names the type by itself. Only a different type is an error.
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
        // **A condition declares one name and must initialise it**, and both
        // are settled here: the loop below leaves by more than one path, so
        // asking at the end would mean asking in several places.
        if (conditionDecl_) {
            conditionName_ = d.name;
            if (peek().is(")"))
                src_.fail(d.pos, "'" + d.name + "' is declared in a condition "
                                 "and has no initialiser - there would be "
                                 "nothing to test");
        }
        if (mentionsDeduced(d.type)) d.type = deduceAuto(d.type, d.name, d.pos);

        // **A const object has to be initialised where it is declared**, and this
        // is asked before the branch below rather than after it: a class whose
        // constructor the *compiler* wrote initialises only what its members ask
        // for, so `const S s;` reaches a constructor and still leaves a member
        // holding the stack. sc is not read here - a `static` one is as const.
        if (d.type->isConst() && !peek().is("=") && !peek().is("(") &&
            !peek().is("{"))
            requireConstInitialised(d.type, d.name, d.pos);

        // An object of a class with constructors is built by calling one, asked before
        // the branch below - `Point p(1)` and a function declaration look alike until
        // the type is known. **And an array of one**, which used to fall through.
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
                // **Destruction is refused rather than half-built.** [class.dtor]
                // destroys the elements in reverse, and the shared code that emits a
                // scope's destructors knows one object per entry and not a count.
                if (destructorOf(plain) != nullptr)
                    src_.fail(d.pos, "an array of '" + plain->describe() +
                                     "' is not supported yet because it has a "
                                     "destructor, and the elements would have "
                                     "to be destroyed in reverse when the scope "
                                     "ends - an array of a class with only "
                                     "constructors works");
                int off = declare(d.name, d.type, d.pos);
                locals_.back().guardsJump = true;
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
            // **`C c{};` and `C c = {};` value-initialise.** [dcl.init]/11 sends
            // an empty list to /8, which is the default constructor - and the
            // zeroing before it that `constructLocal` puts in for an implicit one.
            bool valueInit = false;
            bool valueInitCopied = false;
            if ((peek().is("{") && peekAt(1).is("}")) ||
                (peek().is("=") && peekAt(1).is("{") && peekAt(2).is("}"))) {
                // `= {}` is copy-initialisation and `{}` is not, which is the
                // whole difference an `explicit` default constructor makes.
                valueInitCopied = consume("=");
                expect("{");
                expect("}");
                valueInit = true;
            }

            // **A braced initialiser, and the two different answers it has.**
            // [dcl.init.aggr]/1 in C++11 makes a class with a member initialiser no
            // aggregate; where it wrote none, the braces want C++11 list-init.
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
            bool copyInit = valueInitCopied;
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
                // **Copy-initialisation.** `X b = a;` is a constructor called with one
                // argument, chosen by the ordinary overload rules. What separates it
                // from `X b(a);` is that an `explicit` constructor may not be picked.
                copyInit = true;
                args.push_back(assign());
            }

            // **An elided copy still needs a copy constructor that may be chosen.**
            // [class.copy]/31 selects and checks it even where the copy itself is
            // elided. Checked here: both branches below reach past `constructLocal`.
            if (copyInit && args.size() == 1 && args[0]->type() != nullptr &&
                args[0]->type()->unqualified() == d.type->unqualified()) {
                // The constructor the rule checks is the one resolution would pick:
                // the move for a source that is not an lvalue and has one, the copy
                // otherwise. An explicit copy beside a plain move does not bite.
                const Signature *mc = moveConstructorOf(d.type->unqualified());
                const Signature *sel = !isLvalue(*args[0]) && mc != nullptr
                                     ? mc
                                     : copyConstructorOf(d.type->unqualified());
                if (sel != nullptr && sel->isExplicit)
                    src_.fail(d.pos, "'" + d.type->describe() + "' has an "
                                     "'explicit' " +
                                     (sel == mc ? "move" : "copy") +
                                     " constructor, so it "
                                     "will not be chosen for '" + d.name +
                                     " = ...' - write '" +
                                     d.type->describe() + " " + d.name +
                                     "(...)'. The copy may well be elided, "
                                     "and the rule is checked all the same");
            }

            int off = declare(d.name, d.type, d.pos);
            locals_.back().guardsJump = true;

            // **Copy elision, in the one case worth having it**: where the initialiser
            // is a call already returning through a hidden pointer, the object is built
            // straight into this variable. clang does it at -O0, cl does not; both may.
            // **A destructor makes the copy observable even where it is
            // trivial**, which is why this asks about more than the copy: a
            // class of plain members with a `~T` is copied by bytes and then
            // destroyed twice, once per object. clang elides it at -O0 and so
            // does this now.
            Call *made = args.size() == 1 &&
                         (d.type->nonTrivialCopy() ||
                          destructorOf(d.type) != nullptr)
                       ? dynamic_cast<Call *>(args[0].get()) : nullptr;

            // **A trivial copy, in a class that does have constructors** - none was
            // declared for it and the standard asks for the bytes. **But a class that
            // declares a move has no trivial copy**, and reading it so copied one.
            const Signature *mover = moveConstructorOf(d.type->unqualified());
            const bool sameClass =
                args.size() == 1 && args[0]->type() != nullptr &&
                args[0]->type()->unqualified() == d.type->unqualified();
            // An lvalue is what the deleted copy would be asked to take; an
            // xvalue moves, and so does a prvalue - `S d = make();` is a
            // temporary and the move constructor is exactly what it is for.
            if (mover != nullptr && sameClass && isLvalue(*args[0]) &&
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
                returnsIndirectly(d.type, made->hasThis())) {
                claimCallResult(*made, off);
                inits.push_back(StmtPtr(new ExprStmt(std::move(args[0]))));
            } else if (trivialCopy) {
                ExprPtr target(Var::local(d.name, off));
                target->setType(d.type);
                ExprPtr store(new Assign(std::move(target), std::move(args[0])));
                store->setType(d.type);
                inits.push_back(StmtPtr(new ExprStmt(std::move(store))));
            } else {
                inits.push_back(constructLocal(d, off, std::move(args), copyInit,
                                               valueInit));
            }
            flushTemporaries(inits);
            if (destructorOf(d.type) != nullptr)
                alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
            if (!consume(",")) break;
            continue;
        }

        // **`X q(p);` where X has no constructor at all.** Its copy is trivial, so what
        // the standard asks for is the bytes - the struct assignment the backends
        // already emit. A parameter list begins with a type name and this does not.
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
                locals_.back().guardsJump = true;
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
            locals_.back().guardsJump = true;
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
        checkNotAbstract(d.type, d.pos, "'" + d.name + "'");

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
            if (consume("=") || atBracedInitialiser(d.name)) {
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

        bool hasInit = peek().is("=") || atBracedInitialiser(d.name);
        Init in;
        if (hasInit) {
            consume("=");
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
        // An initialiser to skip, or a destructor that would run on what was
        // never built: either makes this a declaration no jump may land past.
        locals_.back().guardsJump = hasInit || destructorOf(d.type) != nullptr;
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

        // **An object with a destructor is alive from here**, whether or not it had a
        // constructor to run. Before implicit destructors existed only the constructor
        // path added to this list, so such a class was destroyed by nobody.
        if (destructorOf(d.type) != nullptr)
            alive_.push_back(Alive{ d.name, off, d.type->unqualified() });

        if (hasInit) {
            std::vector<InitStep> path;
            emitInit(d.name, path, d.type, in, inits);
        }
        flushTemporaries(inits);
    } while (!conditionDecl_ && consume(","));

    // A condition ends at the caller's `)` rather than at a `;`, and it
    // declares one name - [stmt.select]/2 allows a single declarator.
    if (conditionDecl_) {
        if (peek().is(","))
            src_.fail(peek().pos, "a condition declares one name - "
                                  "[stmt.select] allows a single declarator "
                                  "here");
        return StmtPtr(new Block(std::move(inits)));
    }
    expect(";");
    return StmtPtr(new Block(std::move(inits)));
}

// **A declaration followed by `:` rather than `;`.** Telling that from
// `for (int x = a ? b : c; ...)` is the whole difficulty: a `?` claims the next `:`,
// so they are counted. `::` is one token from the lexer and cannot be mistaken.
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

// **[stmt.ranged] is a rewrite, and this does the rewrite.** The standard says what
// `for (T x : a)` means by writing another loop, and every node that loop needs was
// already here. The range is evaluated once, which assigning it to `__b` buys.
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
    // **Through `arithmetic`, not a bare Binary.** `p + 1` on an `int *` advances four
    // bytes, and that scaling lives in the helper the ordinary expression path uses.
    // Built by hand it produced a loop that read the array one byte at a time.
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
    loopMarks_.push_back(alive_.size());
    breakMarks_.push_back(alive_.size());
    body.push_back(statement());
    breakMarks_.pop_back();
    loopMarks_.pop_back();
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

namespace {
// The flag has to come back off however the declaration ends, because a
// diagnostic raised inside a Trial is a substitution failure and unwinds.
struct ConditionFlag {
    bool &f;
    explicit ConditionFlag(bool &b) : f(b) { f = true; }
    ~ConditionFlag() { f = false; }
};
}

// **[stmt.select]/2: an `if` may declare a name in its condition**, and that
// name is in scope in both arms. It is evaluated once, before the branch, so
// the declaration is hoisted in front of the `If` and the two wrapped in a
// block - which is what gives the object its scope and its destructor at the
// end of the whole statement rather than at the end of an arm.
ExprPtr Parser::ifConditionDeclaration(std::vector<StmtPtr> &setup) {
    std::string name;
    {
        ConditionFlag guard(conditionDecl_);
        conditionName_.clear();
        setup.push_back(declaration());
        name = conditionName_;
    }
    ExprPtr v = objectRef(name);
    if (v == nullptr)
        src_.fail(peek().pos, "'" + name + "' was declared in this condition "
                              "and cannot be read back");
    return v;
}

// **[stmt.iter]/2 creates and destroys the variable on every turn**, which is
// the whole difference from the `if` form: hoisting the initialiser out would
// evaluate it once and then loop for ever on the value it got. The slot is
// declared once - it is one object as far as the frame is concerned - and the
// initialisation moves into the condition, which is exactly what the standard
// asks for as long as there is no constructor or destructor to run.
ExprPtr Parser::whileConditionDeclaration() {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    if (sc != StorageNone)
        src_.fail(peek().pos, "a condition declares an ordinary object, so it "
                              "may not have a storage class");
    Declared d = declarator(base);
    if (d.name.empty())
        src_.fail(d.pos, "a condition declares a name and this declares none");
    if (!consume("="))
        src_.fail(peek().pos, "'" + d.name + "' is declared in a condition and "
                              "has no initialiser - there would be nothing "
                              "to test");
    ExprPtr init = decay(assign());
    if (mentionsDeduced(d.type))
        d.type = deduceAutoFrom(d.type, init->type(), d.name, d.pos);
    // A class would have to be constructed and destroyed once per turn, and
    // the construction is written where the test is - so it is refused here
    // and not in an `if`, where the object is built once and the ordinary
    // declaration path does all of it.
    if (d.type->isStructOrUnion() || d.type->isReference() || d.type->isArray())
        src_.fail(d.pos, "a '" + d.type->describe() + "' declared in the "
                         "condition of a loop is not supported yet - "
                         "[stmt.iter] builds it afresh on every turn, and only "
                         "a scalar can be written where the test is");
    const int slot = declare(d.name, d.type, d.pos);
    locals_.back().isConst = d.type->isConst();
    ExprPtr x(Var::local(d.name, slot));
    x->setType(d.type);
    ExprPtr set(new Assign(std::move(x), convert(std::move(init), d.type)));
    set->setType(d.type);
    return set;
}

StmtPtr Parser::forStatement() {
    const std::size_t pos = peek().pos;
    expect("for");
    expect("(");
    enterScope();
    int scope = enterBlock();
    // What the init-statement builds lives to the end of the for statement
    // and no further - [stmt.for]/1 puts the whole loop in its own block.
    const std::size_t aliveAtEntry = alive_.size();

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

    // The mark is taken after the init-statement: `for (S s; ...)` builds s
    // once for the whole loop, and a break must not destroy it.
    loopDepth_++;
    loopMarks_.push_back(alive_.size());
    breakMarks_.push_back(alive_.size());
    StmtPtr body = statement();
    breakMarks_.pop_back();
    loopMarks_.pop_back();
    loopDepth_--;

    leaveBlock();
    leaveScope();
    For *f = new For(std::move(init), std::move(cond),
                     std::move(step), std::move(body));
    f->setScope(scope);
    if (alive_.size() == aliveAtEntry) return StmtPtr(f);

    // **`for (S s; ...)` destroys s when the loop is done**, here, the way a block
    // destroys what it built at its '}'. It used to stay on alive_ and be destroyed by
    // the enclosing block instead - once, but late, and after all that block did.
    if (functionHasTry_ || inTryBody_)
        src_.fail(pos, "a local with a destructor and a 'try' in one "
                       "function is not supported yet - each is a range in "
                       "the call-site table and one would have to split "
                       "the other");
    std::vector<StmtPtr> steps;
    steps.push_back(StmtPtr(f));
    emitDestructors(steps, aliveAtEntry, pos);
    alive_.resize(aliveAtEntry);
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

// **`static_assert(cond, "message");` - a declaration that declares nothing and emits
// nothing.** The message is required in C++11, so the one-argument form is refused by
// name; the condition must be an integral constant expression, narrower than the rule.

StmtPtr Parser::switchStatement() {
    std::size_t pos = peek().pos;
    expect("switch");
    expect("(");
    ExprPtr cond = contextualScalar(endFullExpression(decay(expr())), pos,
                                   "this condition");
    if (!cond->type()->isInteger())
        src_.fail(pos, "a switch needs an integer, not '" +
                       cond->type()->describe() + "'");
    const Type *governing = promote(cond->type());
    cond = convert(std::move(cond), governing);
    expect(")");

    switches_.push_back(SwitchCtx{ {}, nullptr, governing, jumpGuards() });
    switchDepth_++;
    breakMarks_.push_back(alive_.size());
    StmtPtr body = statement();
    breakMarks_.pop_back();
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

    // **Every case label is a jump from the `switch`**, so what is in scope
    // here and was not there has been jumped past. [stmt.dcl]/3 names the
    // switch alongside goto, and clang refuses it the same way.
    checkJump(switches_.back().guards, jumpGuards(), pos,
              isDefault ? std::string("'default:'")
                        : "'case " + std::to_string(value) + ":'",
              "the 'switch'");

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
    labels_.push_back(LabelDef{ name, pos, jumpGuards(), alive_, nullptr });

    if (atDeclarationStart())
        src_.fail(peek().pos, "a label cannot be followed by a declaration - "
                              "put it in a block");
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    return StmtPtr(new Label(std::move(name), statement()));
}

// **[stmt.dcl]/3: a jump may not enter the scope of an initialised object.** All three
// jumps this compiler has fall under it, and clang refuses each. The test is set
// membership: refused when the label's live list holds one the origin's does not.
std::vector<Parser::JumpGuard> Parser::jumpGuards() const {
    std::vector<JumpGuard> out;
    for (const Local &l : locals_)
        if (l.guardsJump) out.push_back(JumpGuard{ l.name, l.offset });
    return out;
}

// A jump that leaves a scope destroys what the scope built, innermost first, before it
// goes - the calls the scope's end would have made, made here instead. [stmt.jump]/2.
// `break` and `continue` destroy everything built since their loop was entered.
StmtPtr Parser::jumpLeaving(StmtPtr jump, std::size_t mark, std::size_t pos) {
    std::vector<StmtPtr> steps;
    emitDestructors(steps, mark, pos);
    if (steps.empty()) return jump;
    steps.push_back(std::move(jump));
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

void Parser::checkJump(const std::vector<JumpGuard> &from,
                       const std::vector<JumpGuard> &to, std::size_t pos,
                       const std::string &jump, const std::string &origin) const {
    for (const JumpGuard &g : to) {
        bool inScopeAtOrigin = false;
        for (const JumpGuard &f : from)
            if (f.offset == g.offset) { inScopeAtOrigin = true; break; }
        if (inScopeAtOrigin) continue;
        src_.fail(pos, jump + " jumps past the initialisation of '" + g.name +
                       "', which is in scope at the label and not at " +
                       origin + " - a jump may not enter the scope of a "
                       "variable that has an initialiser, a constructor or a "
                       "destructor, because the object would be used and "
                       "destroyed without ever having been built. Declare '" +
                       g.name + "' before " + origin + ", or put it in a "
                       "block that ends before the label");
    }
}

void Parser::resolveGotos() {
    for (const LabelDef &g : gotos_) {
        const LabelDef *target = nullptr;
        for (const LabelDef &l : labels_)
            if (l.name == g.name) { target = &l; break; }
        if (target == nullptr)
            src_.fail(g.pos, "no label '" + g.name + "' in this function");
        checkJump(g.guards, target->guards, g.pos, "'goto " + g.name + "'",
                  "the goto");

        // **A goto out of a scope destroys what it leaves**, innermost first, in the
        // block left in front of the Goto for them: alive at the goto and not at the
        // label is left behind, alive at both is stayed inside. Never twice.
        for (std::size_t i = g.alive.size(); i > 0; i--) {
            const Alive &a = g.alive[i - 1];
            bool atLabel = false;
            for (const Alive &b : target->alive)
                if (b.offset == a.offset) { atLabel = true; break; }
            if (atLabel) continue;
            std::vector<StmtPtr> call;
            destroyObject(call, a, g.pos);
            for (std::size_t k = 0; k < call.size(); k++)
                g.cleanups->append(std::move(call[k]));
        }
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
    // The regions of a function body reach back to its by-value parameters
    // on the target whose callee destroys them; every other block's start at
    // what was alive when it opened.
    const std::size_t regionFrom = isBody && bodyCleanupFrom_ < aliveAtEntry
                                 ? bodyCleanupFrom_ : aliveAtEntry;
    int scope = isBody ? 0 : enterBlock();
    // **Where each object became alive**, as a statement index and how many were alive
    // after it. A cleanup region runs from one of these to the next and destroys
    // exactly what was built by then, so an exception cannot destroy what is not.
    std::vector<std::pair<std::size_t, std::size_t> > built;
    std::vector<StmtPtr> body;
    // **What this block's statements made and destroyed within themselves.**
    // A temporary lives inside one full expression, so it is never in
    // `alive_` and the regions below would not know about it - but an
    // exception can still leave the statement in the middle of one.
    std::vector<Temporary> temps;
    std::vector<Temporary> outer;
    outer.swap(statementTemps_);
    while (!peek().is("}")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "unclosed '{'");
        const std::size_t aliveBefore = alive_.size();
        body.push_back(atDeclarationStart() ? declaration() : statement());
        for (std::size_t k = 0; k < statementTemps_.size(); k++)
            temps.push_back(statementTemps_[k]);
        statementTemps_.clear();
        if (alive_.size() > aliveBefore)
            built.push_back(std::make_pair(body.size(), alive_.size()));
    }
    statementTemps_.swap(outer);
    // A region has to cover the statements that made them, and the first may
    // be before anything was alive - so the regions start at the top of the
    // block rather than at the first construction.
    if ((!temps.empty() || regionFrom != aliveAtEntry) &&
        (built.empty() || built[0].first != 0))
        built.insert(built.begin(), std::make_pair(std::size_t(0), aliveAtEntry));

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
                   ? wrapMsCleanups(std::move(body), built, regionFrom, pos,
                                    temps)
                   : wrapCleanups(std::move(body), built, regionFrom, pos,
                                  temps);
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

// **`try` is a block, a landing pad, and no new statement machinery.** Everything from
// the pad on is built out of nodes that already existed: the selector compared in the
// order the handlers are written, each arm begin/copy/body/end, the rest resumed.
StmtPtr Parser::tryStatement(std::size_t pos) {
    // **The two ABIs disagree about who picks the handler**, so this reads one grammar
    // and builds two shapes: Itanium's if/else chain on a selector, and Microsoft's
    // handlers kept whole for the runtime to call.
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

        // **The Microsoft handler is the block and nothing else.** The runtime has
        // chosen it, made the caught object in the slot the table names, and ends the
        // catch when the funclet returns - so the three Itanium calls are not here.
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
                // The descriptor is emitted by the same pass that emits a thrown
                // type's, so a type that is only ever *caught* has to join that list
                // or the handler map would name a symbol nothing defines.
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
        // The runtime's scratch word, which the personality routine finds through the
        // FuncInfo's dispUnwindHelp and the parent sets to -2 on entry. A frame slot
        // like any other, so where it lives is decided where every local's is.
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

    // **`using namespace N;` inside a block**, the same directive as the one at file
    // scope, differing only in when it stops applying: at the end of this block, which
    // `block()` undoes by truncating the list. It becomes the empty statement.
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

    // **The using-*declaration* is refused inside a block**, where the one at
    // namespace scope is not: a name declared here lasts to the end of the
    // block and takes part in overload resolution against the locals beside it,
    // and neither is what the alias at namespace scope does.
    if (peek().is("using"))
        src_.fail(peek().pos, "a using-declaration inside a block is not "
                              "supported yet - it declares a name for the rest "
                              "of this block rather than naming one; at "
                              "namespace scope it works, and 'using namespace "
                              "N;' works here");

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
        ExprPtr returned = returnType_->isReference() ? expr() : decay(expr());
        // **The temporary that *is* the returned value must not be destroyed
        // here.** `return string(p, n);` builds a class temporary, and
        // classTemporary puts it on the pending list to die at the end of the
        // full expression - which is this statement. But the value travels out
        // through the hidden pointer as *bytes*: destroy the temporary first and
        // the caller is handed a shallow copy of an object whose resources have
        // been released. `substr` returned the right length and an empty buffer,
        // and `operator+` lost half its text, for exactly this reason.
        //
        // Releasing it hands ownership to the caller, which is what returning by
        // value means. The caller then copy-constructs from those bytes and the
        // hidden temporary is not destroyed there either - that leak is a
        // separate open finding, and a leak is not a wrong answer.
        // **A temporary released here is the returned object itself**, not a
        // source to copy from - its bytes travel out through the hidden
        // pointer and the caller owns them. Copying it as well built a second
        // object and left the first undestroyed, which is where
        // `return Owner(n);` leaked one per call.
        bool ownedTemporary = false;
        if (!returnType_->isReference() && returnType_->isStructOrUnion())
            ownedTemporary = releaseTemporary(*returned);
        ExprPtr value = endFullExpression(std::move(returned));
        bool returnedParameter = false;
        if (returnType_->isReference()) {
            value = bindReference(returnType_, std::move(value), pos,
                                  "this function's return type");
            if (dynamic_cast<const Comma *>(value.get()) != nullptr)
                src_.fail(pos, "this returns a reference to a temporary of "
                               "this function, which is gone by the time the "
                               "caller could read it");
        } else {
            // **[stmt.return]/2 copy-initialises the returned object**, and a
            // converting constructor is part of that: `return "v";` from a
            // function returning `std::string` is `string("v")`. The machinery
            // is `userConversion`, which a by-value argument already used -
            // what was missing was this second door to it.
            if (ExprPtr made = userConversion(returnType_, value, pos))
                value = std::move(made);
            checkAssignable(*value, returnType_, pos, "this function's return type");
            // Does the operand name a by-value parameter of this function? One that
            // arrived by address was lowered to a reference and reads back as `*slot`;
            // `byValueByAddress` is what tells it from a genuine `T &t` or `*p`.
            {
                const Expr *named = value.get();
                bool viaDeref = false;
                if (const Unary *u = dynamic_cast<const Unary *>(named))
                    if (u->op() == '*') { named = &u->operand(); viaDeref = true; }
                if (const Var *v = dynamic_cast<const Var *>(named))
                    if (v->isLocal())
                        if (const Local *l = findLocal(v->name()))
                            if (l->isParameter)
                                returnedParameter = viaDeref
                                                  ? l->byValueByAddress
                                                  : !l->type->isReference();
            }
            // **[class.copy]/31 again: returning by value copy-initializes the caller's
            // object**, so its copy constructor is selected and checked though the copy
            // is elided below. An explicit one makes the function ill-formed alone.
            if (returnType_->isStructOrUnion() && value->type() != nullptr &&
                value->type()->unqualified() == returnType_->unqualified()) {
                // **Which constructor `return` selects is decided rvalue-first for an
                // automatic object** - [class.copy]/32: resolution runs first as if the
                // operand were an rvalue, and only then as the lvalue it is.
                bool asRvalue = !isGlvalue(*value) || value->isXvalue() ||
                                returnedParameter;
                if (const Var *v = dynamic_cast<const Var *>(value.get()))
                    if (v->isLocal()) asRvalue = true;
                const Signature *mc = moveConstructorOf(returnType_->unqualified());
                const Signature *sel = asRvalue && mc != nullptr
                                     ? mc
                                     : copyConstructorOf(returnType_->unqualified());
                if (sel != nullptr && sel->isExplicit)
                    src_.fail(pos, "'" + returnType_->describe() + "' has an "
                                   "'explicit' " +
                                   (sel == mc ? "move" : "copy") +
                                   " constructor, so it "
                                   "cannot be returned by value - 'return' "
                                   "copy-initialises the caller's object, "
                                   "and that may not pick an explicit "
                                   "constructor even where the copy is "
                                   "elided");
            }
            value = convert(std::move(value), returnType_);
        }
        expect(";");

        // **A return runs every destructor the function still owes, and the value is
        // computed first**, into a slot of its own. **What is returned is not destroyed
        // here** - that is elision, and [class.copy]/31 excludes a parameter from it.
        bool elidable = false;
        if (const Var *v = dynamic_cast<const Var *>(value.get()))
            if (v->isLocal()) {
                const Local *l = findLocal(v->name());
                elidable = l == nullptr || !l->isParameter;
            }

        // **A `return` of a glvalue this function does not own has to call the copy
        // constructor**, and nothing did: the byte move let the destructor's elision
        // stand in. The copy is built into a slot of this frame, and that is elided.
        std::vector<StmtPtr> before;
        if (returnType_->isStructOrUnion() && !elidable && !ownedTemporary &&
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
            // **A returned parameter moves.** [class.copy]/32 again, on the copy that
            // is actually built: the operand is designated an rvalue first, so a
            // move-only class can be returned by value at all, as C++11 promises.
            if (returnedParameter &&
                moveConstructorOf(returnType_->unqualified()) != nullptr)
                value->setXvalue();
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
    if (peek().is("if")) {
        const std::size_t pos = peek().pos;
        at_++;
        expect("(");
        // **A declaration here is a scope that wraps both arms**, so the whole
        // statement goes inside a block of its own rather than the condition
        // being a bigger expression. Nothing changes for an ordinary
        // condition, which is what the `declares` test is guarding.
        const bool declares = atDeclarationStart();
        std::vector<StmtPtr> setup;
        int scope = -1;
        std::size_t aliveAtEntry = alive_.size();
        if (declares) {
            enterScope();
            scope = enterBlock();
            aliveAtEntry = alive_.size();
        }
        ExprPtr cond = declares
            ? contextualScalar(ifConditionDeclaration(setup), peek().pos,
                               "this condition")
            : contextualScalar(endFullExpression(decay(expr())), peek().pos,
                               "this condition");
        // **Where the condition's object became alive**, in the shape a
        // cleanup region wants: a statement index and how many were alive
        // after it. Without this an exception passing through the arms would
        // leave the object undestroyed - the normal path below is not the
        // only way out of the statement.
        std::vector<std::pair<std::size_t, std::size_t> > built;
        if (declares && alive_.size() != aliveAtEntry)
            built.push_back(std::make_pair(setup.size(), alive_.size()));
        expect(")");
        StmtPtr thenArm = statement();
        StmtPtr elseArm;
        if (consume("else")) elseArm = statement();
        StmtPtr made(new If(std::move(cond), std::move(thenArm),
                            std::move(elseArm)));
        if (!declares) return made;
        leaveBlock();
        leaveScope();
        setup.push_back(std::move(made));
        // What the condition built is destroyed at the end of the statement,
        // the way a block destroys what it built at its '}'.
        if (alive_.size() != aliveAtEntry) {
            if (functionHasTry_ || inTryBody_)
                src_.fail(pos, "a local with a destructor and a 'try' in one "
                               "function is not supported yet - each is a "
                               "range in the call-site table and one would "
                               "have to split the other");
            emitDestructors(setup, aliveAtEntry, pos);
            setup = target_.microsoftNames()
                        ? wrapMsCleanups(std::move(setup), built,
                                         aliveAtEntry, pos)
                        : wrapCleanups(std::move(setup), built,
                                       aliveAtEntry, pos);
            alive_.resize(aliveAtEntry);
        }
        Block *b = new Block(std::move(setup));
        b->setScope(scope);
        b->setPos(pos);
        return StmtPtr(b);
    }
    if (peek().is("while")) {
        const std::size_t pos = peek().pos;
        at_++;
        expect("(");
        // The declared object is one slot for the whole loop and is written
        // afresh each turn - see whileConditionDeclaration. It still needs a
        // scope of its own, or the name would outlive the loop.
        const bool declares = atDeclarationStart();
        int scope = -1;
        if (declares) { enterScope(); scope = enterBlock(); }
        ExprPtr cond = declares
            ? contextualScalar(endFullExpression(whileConditionDeclaration()),
                               peek().pos, "this condition")
            : contextualScalar(endFullExpression(decay(expr())), peek().pos,
                               "this condition");
        expect(")");
        loopDepth_++;
        loopMarks_.push_back(alive_.size());
        breakMarks_.push_back(alive_.size());
        StmtPtr body = statement();
        breakMarks_.pop_back();
        loopMarks_.pop_back();
        loopDepth_--;
        StmtPtr made(new While(std::move(cond), std::move(body)));
        if (!declares) return made;
        leaveBlock();
        leaveScope();
        std::vector<StmtPtr> wrap;
        wrap.push_back(std::move(made));
        Block *b = new Block(std::move(wrap));
        b->setScope(scope);
        b->setPos(pos);
        return StmtPtr(b);
    }

    if (peek().is("for")) return forStatement();

    if (consume("do")) {
        loopDepth_++;
        loopMarks_.push_back(alive_.size());
        breakMarks_.push_back(alive_.size());
        StmtPtr body = statement();
        breakMarks_.pop_back();
        loopMarks_.pop_back();
        loopDepth_--;
        expect("while");
        expect("(");
        ExprPtr cond = contextualScalar(endFullExpression(decay(expr())), peek().pos,
                                   "this condition");
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
        // **The jump destroys what it leaves, and cannot yet know what that is**: a
        // forward label has not been read. So the goto is placed behind an empty block
        // that resolveGotos() fills. What is alive here is copied now, not later.
        Block *cleanups = new Block({});
        cleanups->setScope(-1);
        gotos_.push_back(LabelDef{ name, pos, jumpGuards(), alive_, cleanups });
        std::vector<StmtPtr> steps;
        steps.push_back(StmtPtr(cleanups));
        steps.push_back(StmtPtr(new Goto(std::move(name))));
        Block *b = new Block(std::move(steps));
        b->setScope(-1);
        return StmtPtr(b);
    }

    if (peek().kind == TokenKind::Ident && peekAt(1).is(":")) return gotoLabel();

    // A jump can leave a scope without falling off its end, and this compiler runs
    // destructors at the end - so each of these destroys what was built since its loop
    // or switch was entered, the way `return` destroys what the function owes.
    if (peek().is("break")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (loopDepth_ == 0 && switchDepth_ == 0)
            src_.fail(pos, "'break' is not inside a loop or a switch");
        expect(";");
        return jumpLeaving(StmtPtr(new Break()), breakMarks_.back(), pos);
    }

    if (peek().is("continue")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (loopDepth_ == 0)
            src_.fail(pos, "'continue' is not inside a loop");
        expect(";");
        return jumpLeaving(StmtPtr(new Continue()), loopMarks_.back(), pos);
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
