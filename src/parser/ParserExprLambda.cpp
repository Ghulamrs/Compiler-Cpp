// The parser: lambdas and the closures they become.
//
// A lambda is a class this compiler writes: the capture list becomes members,
// the body becomes `operator()`, and the object is built where the expression
// stood. Deducing the return type from the body is here too, since nothing
// else needs it.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


// `[](int a) { return a * 2; }` - rung 7.6.
//
// **A closure is a class with a call operator**, generated where the lambda is
// written, and both halves of that now exist: a class can be defined inside a
// function and `operator()` can be reached. What is left is the generating,
// and three decisions carry it.
//
// **The object lives in the enclosing frame.** A lambda expression is a class
// *temporary*, and this compiler has none - the same gap that refuses
// `return P(1);`. So the closure is given a frame slot of its own and the
// expression answers with a `Var` naming it. Its lifetime is the function
// rather than the full expression, which is longer than [expr.prim.lambda]
// asks for and harmless while a closure has nothing to destroy.
//
// **The body is replayed as a member function**, through the same held-body
// path a class written inside a function uses - so nothing in the definition
// machinery had to learn what a lambda is. That needs tokens shaped like a
// definition, which the lambda's own are not, so they are synthesised:
// `<ret> operator ( ) ( <params> ) const { <body> }`, built from the lambda's
// own parameter and body tokens and appended to the stream. An index into
// `tokens_` survives the vector growing, which is what makes this safe.
//
// **The return type is spelled as a hidden typedef.** Synthesising tokens for
// an arbitrary type is not possible in general - `int` is one token and
// `const char *` is three and a class is its tag - so the deduced type is
// registered under a made-up name and that one identifier is written instead.
const Type *Parser::deduceLambdaReturn(std::size_t paramsFrom,
                                       std::size_t paramsTo,
                                       std::size_t bodyFrom,
                                       std::size_t bodyTo,
                                       const std::vector<std::string> &capNames,
                                       const std::vector<const Type *> &capTypes) {
    // **The first `return` at the body's own level**, and not only a body that
    // is nothing but one.
    //
    // [expr.prim.lambda]/4 as C++11 wrote it says a body of the form
    // `{ return e; }` has e's type and *anything else is void* - so
    // `[](){ auto i = ...; return i(); }` would deduce void and then be
    // ill-formed for returning an int. clang accepts it under -std=c++11 all
    // the same, applying the relaxation C++14 made, and real C++11 code is
    // written expecting that. The oracle is followed here rather than the
    // letter, which is the one place in this compiler that happens - said out
    // loud because it is a choice and not an oversight.
    //
    // **Depth matters, and a nested lambda is why.** In
    // `{ auto i = [](){ return 1; }; return i(); }` the first `return` token
    // belongs to the inner lambda; the outer one's is the first at depth 1.
    std::size_t i = bodyFrom + 1;                       // past the '{'
    int depth = 1;
    while (i < bodyTo && depth > 0) {
        if (tokens_[i].is("{")) depth++;
        else if (tokens_[i].is("}")) { depth--; if (depth == 0) break; }
        else if (depth == 1 && tokens_[i].is("return")) break;
        i++;
    }
    if (i >= bodyTo || !tokens_[i].is("return")) return types_.get(Kind::Void);
    const std::size_t returnAt = i;
    i++;
    if (i < bodyTo && tokens_[i].is(";")) return types_.get(Kind::Void);

    const std::size_t resume = at_;
    // The parameters have to be in scope for the expression to be read, and
    // nothing else of the enclosing function should be: a lambda body sees its
    // own parameters and, without a capture, no local of the function around
    // it. So the locals are put aside exactly as a default argument does it.
    //
    // **`this` is the exception, and it is a local like any other.** A body
    // that names a member of the enclosing class reaches it through `this`,
    // and hiding it left `[this](){ return n; }` reporting that `n` is a
    // member with no object to read it from - which is what the machinery
    // says when `this` has gone missing.
    const Local *hadThis = findLocal("this");
    Local keptThis;
    const bool haveThis = hadThis != nullptr;
    if (haveThis) keptThis = *hadThis;

    std::vector<Local> outer;
    outer.swap(locals_);
    if (haveThis) locals_.push_back(keptThis);
    std::vector<std::size_t> starts;
    starts.swap(scopeStarts_);
    enterScope();

    // The captures are in scope in the body as well as the parameters - they
    // are members of the closure by the time it is really parsed, and a member
    // is what an unqualified name there will find. Declared as locals here
    // because this reading has no closure to be a member of yet.
    //
    // **In a scope of their own, outside the parameters and the body.** A
    // capture is a member and both of those may shadow one, so putting them
    // all in one scope made `[=](int a){...}` where the enclosing function
    // also has an `a` report that `a` was declared twice - and the same for a
    // body that declares a name it captured.
    for (std::size_t c = 0; c < capNames.size(); c++) {
        inParams_ = true;
        declare(capNames[c], capTypes[c], bodyFrom);
        inParams_ = false;
    }
    enterScope();

    if (paramsTo > paramsFrom) {
        const std::size_t save = at_;
        at_ = paramsFrom - 1;                           // at the '('
        std::vector<const Type *> ps;
        bool var = false;
        // Read the list again, this time declaring each name, so that the
        // expression below can mention them.
        at_ = save;
        std::size_t k = paramsFrom;
        while (k < paramsTo) {
            std::size_t from = k;
            int depth = 0;
            while (k < paramsTo &&
                   !(depth == 0 && tokens_[k].is(","))) {
                if (tokens_[k].is("(")) depth++;
                if (tokens_[k].is(")")) depth--;
                k++;
            }
            at_ = from;
            StorageClass psc;
            Qualifiers pq;
            const Type *pt = specifiers(&psc, &pq);
            Declared pd = declarator(pt, true);
            if (!pd.name.empty()) {
                inParams_ = true;
                declare(pd.name, pd.type, pd.pos);
                inParams_ = false;
            }
            (void)ps; (void)var;
            if (k < paramsTo) k++;                      // the ','
        }
    }

    // **The statements before the return are read too, not skipped to.** The
    // expression may name a local the body declared - `{ auto i = ...;
    // return i(); }` - and jumping straight to the return leaves that name
    // undeclared. So the body is parsed from its first statement up to the
    // return, in this throwaway scope, and everything it builds is discarded.
    // That is the same reading-twice 7.1 does for an `auto` initialiser.
    at_ = bodyFrom + 1;                                 // past the '{'
    // The same choice the block loop makes: a declaration or a statement.
    // Calling statement() alone reads `auto i = ...;` as an expression and
    // reports that one was expected.
    while (at_ < returnAt && !peek().is("}") &&
           peek().kind != TokenKind::End)
        if (atDeclarationStart()) declaration(); else statement();

    at_ = returnAt + 1;                                 // past that 'return'
    const Type *found = types_.get(Kind::Void);
    ExprPtr e = assign();
    if (e != nullptr && e->type() != nullptr) found = decayedType(e->type());

    leaveScope();                                       // parameters and body
    leaveScope();                                       // the captures
    locals_.swap(outer);
    scopeStarts_.swap(starts);
    at_ = resume;
    return found;
}

ExprPtr Parser::lambdaExpression() {
    const std::size_t pos = peek().pos;
    const std::size_t lamAt = at_;

    // Already built on an earlier reading of these same tokens: hand back
    // another object of the one class rather than making a second.
    std::map<std::size_t, MadeLambda>::const_iterator had = lambdaAt_.find(lamAt);
    if (had != lambdaAt_.end()) {
        at_ = had->second.end;
        return buildClosure(had->second, pos);
    }

    at_++;                                    // '['

    // Refused by name, and each for its own reason rather than one blanket
    // message: a capture is what the reader wrote and what they want back.
    // **A capture by value is a member of the closure**, copied from the
    // enclosing function where the lambda is written. Reading one inside the
    // body needs no new rule at all: `operator()` is a member function, and an
    // unqualified name there already means `this->name`.
    std::vector<std::string> capNames;
    std::vector<const Type *> capTypes;
    std::vector<int> capOffsets;
    bool captureAllByValue = false;
    bool captureAllByRef = false;
    const Type *capturedThisFrom = nullptr;
    if (peek().is("=") || peek().is("&")) {
        // `[&]` only where it is the whole list: `[&x]` is one named capture
        // and is read by the loop below.
        if (peek().is("=") || peekAt(1).is("]")) {
            captureAllByValue = peek().is("=");
            captureAllByRef = peek().is("&");
            at_++;
            if (!peek().is("]"))
                src_.fail(peek().pos, "naming a capture after a default one is "
                                      "not supported yet - '[=]' and '[&]' on "
                                      "their own take everything the body "
                                      "reads");
        }
    }
    while (!peek().is("]")) {
        // `[&x]` - this one by reference, whatever the default is.
        bool byRef = consume("&");
        // `[this]` - the closure holds a pointer to the enclosing object, and
        // an unqualified member name inside the body is reached through it.
        if (peek().is("this")) {
            if (byRef)
                src_.fail(peek().pos, "'&this' is not how 'this' is captured - "
                                      "write '[this]', which copies the "
                                      "pointer");
            if (currentClass_ == nullptr)
                src_.fail(peek().pos, "'[this]' is only inside a member "
                                      "function, and this lambda is not in one");
            at_++;
            capNames.push_back(capturedThis());
            capTypes.push_back(types_.pointerTo(currentClass_));
            capturedThisFrom = currentClass_;
            if (!peek().is("]")) expect(",");
            continue;
        }
        const std::size_t cpos = peek().pos;
        const std::string cname = expectIdent("a captured name");
        // **`[n = k]` is an init-capture, which is C++14.** It has to be
        // named here rather than left to the lookup below: the name it
        // introduces is the closure's own and need not be a local at all, so
        // what the reader would otherwise be told is that a name they were
        // declaring does not exist.
        if (peek().is("="))
            src_.fail(peek().pos, "an init-capture - '" + cname + " = ...' in "
                                  "the capture list - is C++14, and this "
                                  "compiler is C++11: a capture here names "
                                  "something the enclosing function already "
                                  "declared");
        const Local *have = findLocal(cname);
        const Type *fromOuter = nullptr;
        if (have == nullptr) {
            // A capture of the lambda around this one, which is a member of
            // that closure by now rather than a local.
            if (ExprPtr reach = outerCaptureAccess(cname))
                fromOuter = reach->type();
            if (fromOuter == nullptr)
                src_.fail(cpos, "'" + cname + "' is not a local of the function "
                                "around this lambda, nor a capture of a lambda "
                                "around it, so there is nothing here to "
                                "capture");
        }
        capNames.push_back(cname);
        // **By reference the closure holds a reference member**, which needs
        // the layout rule reference members have: the slot is a pointer where
        // `sizeof` the type is the referent's. By value it holds a copy - and
        // capturing a reference *by value* copies what it refers to, which
        // needs nothing at all, every mention of a reference here being
        // already lowered to a dereference.
        const Type *raw = have != nullptr ? have->type : fromOuter;
        const Type *base = raw->isReference()
                         ? raw->referent()->unqualified() : raw->unqualified();
        capTypes.push_back(byRef ? types_.referenceTo(base) : base);
        if (!peek().is("]")) expect(",");
    }
    at_++;                                    // ']'

    // The parameter list, which may be left out entirely.
    std::size_t paramsFrom = at_, paramsTo = at_;
    std::vector<const Type *> params;
    bool variadic = false;
    if (peek().is("(")) {
        paramsFrom = at_ + 1;
        parameterTypes(params, variadic);
        paramsTo = at_ - 1;                   // the ')' just consumed
    }
    if (variadic)
        src_.fail(pos, "a lambda cannot be variadic");

    // **`mutable` is the whole of the difference between a const call operator
    // and a non-const one**, and that is all it is: [expr.prim.lambda] makes
    // the closure's `operator()` const unless the lambda says otherwise, so a
    // by-value capture cannot be written through without it. What changes is
    // one flag on the declaration and one token in the synthesised body.
    //
    // What is written is the closure's *own* copy. The enclosing variable is
    // untouched, which is the point of capturing by value at all - `[&]` is
    // how you write through to the original, and that already works.
    const bool isMutable = consume("mutable");

    const Type *returns = nullptr;
    if (consume("->")) {
        StorageClass rsc;
        returns = specifiers(&rsc);
        returns = declarator(returns, true).type;
    }
    if (!peek().is("{"))
        src_.fail(peek().pos, "expected the lambda's body");
    const std::size_t bodyFrom = at_;
    skipBracedBlock();                        // leaves at_ past the '}'
    const std::size_t bodyTo = at_;

    // [expr.prim.lambda]/4: with no trailing return type, a body that is one
    // `return expression;` has that expression's type and anything else is
    // void. The expression is read with the parameters in scope and then put
    // back, which is what 7.1 does for `auto` and for the same reason.
    // **`[=]` takes everything the body reads that is a local out here**, and
    // finding that is a scan of the body's tokens - the "second pass over the
    // body" an earlier refusal said this parser does not make. It makes one
    // now, and it is a scan and not a parse: an identifier that names a local
    // of the enclosing function is captured unless it is being used as a
    // member name, which is what the test on the token before it is for -
    // `p.k` and `p->k` and `N::k` name no local.
    //
    // **Over-capturing is harmless and under-capturing is not**, which decides
    // every doubtful case here. A name the body declares itself shadows the
    // member, because a local is looked up before a member; a lambda parameter
    // does the same. So a copy nobody reads is the worst this can do, and
    // `docs/CONFORMANCE.md` records that a closure can therefore be larger
    // than the standard's minimum.
    if (captureAllByValue || captureAllByRef) {
        for (std::size_t i = bodyFrom + 1; i + 1 < bodyTo; i++) {
            if (tokens_[i].kind != TokenKind::Ident) continue;
            const Token &before = tokens_[i - 1];
            if (before.is(".") || before.is("->") || before.is("::")) continue;
            const std::string &n = tokens_[i].text;
            bool had = false;
            for (std::size_t k = 0; k < capNames.size(); k++)
                if (capNames[k] == n) { had = true; break; }
            if (had) continue;
            const Local *have = findLocal(n);
            const Type *raw = have != nullptr ? have->type : nullptr;
            if (raw == nullptr) {
                // **A lambda inside a lambda, taking the outer one's
                // capture.** By the time the inner one is read that name is a
                // member of the outer closure, so it is reached through the
                // outer `this` rather than found among the locals.
                if (ExprPtr reach = outerCaptureAccess(n)) raw = reach->type();
                if (raw == nullptr) continue;
            }
            capNames.push_back(n);
            const Type *base = raw->isReference()
                             ? raw->referent()->unqualified()
                             : raw->unqualified();
            capTypes.push_back(captureAllByRef ? types_.referenceTo(base) : base);
        }
    }

    if (returns == nullptr)
        returns = deduceLambdaReturn(paramsFrom, paramsTo, bodyFrom, bodyTo,
                                     capNames, capTypes);

    // The closure type. Named `$_0` upward within the enclosing function, which
    // is what clang calls one - the numbering is not the same as clang's and
    // does not have to be, a closure type having no name the standard obliges
    // anybody to match.
    const std::string local = "$_" + std::to_string(lambdaCount_++);
    // **The tag has to be unique and the function's *name* is not enough.**
    // Inside a replay `currentFunctionName_` is `operator()`, so every level of
    // a nested lambda built `operator()::$_0` and the third one was told its
    // own call operator was declared twice. The owners differ - each closure's
    // operator() has its own linkage name - so the owner is what decides, and
    // a counter separates the display tags, exactly as a local class does it.
    std::string tag = currentFunctionName_.empty()
                    ? local : currentFunctionName_ + "::" + local;
    if (!currentFunction_.empty()) {
        for (int n = 2; ; n++) {
            std::map<std::string, std::string>::const_iterator had =
                localClassOwner_.find(tag);
            if (had == localClassOwner_.end() || had->second == currentFunction_)
                break;
            tag = currentFunctionName_ + "$" + std::to_string(n) + "::" + local;
        }
    }
    Type *closure = types_.structType(Kind::Struct, tag);
    closure->setLocalName(local);
    closure->setDeclaredClass(false);
    if (!currentFunction_.empty()) {
        localClassOwner_[tag] = currentFunction_;
        closure->setLocalOwner(currentFunction_);
    }
    if (capturedThisFrom != nullptr) closureOuter_[tag] = capturedThisFrom;
    declareTypeName(tag, closure);
    // Laid out as any class is: each member at the next offset its own
    // alignment allows, and the whole thing aligned to the widest of them.
    std::vector<Member> members;
    int at = 0, widest = 1;
    for (std::size_t i = 0; i < capTypes.size(); i++) {
        // The same slot rule a reference data member follows: what it occupies
        // is a pointer, where `sizeof` the type is the referent's.
        const Type *slot = capTypes[i]->isReference()
                         ? types_.pointerTo(capTypes[i]->referent())
                         : capTypes[i];
        const int a = slot->align(target_);
        if (a > widest) widest = a;
        at = alignTo(at, a);
        capOffsets.push_back(at);
        members.push_back(Member{ capNames[i], capTypes[i], at, 0, 0,
                                  Access::Public });
        at += slot->size(target_);
    }
    const int size = members.empty() ? 1 : alignTo(at, widest);
    closure->complete(members, size, widest);

    // `operator()`, declared as a const member of it.
    Declared d;
    d.name = "operator()";
    d.type = types_.functionType(returns, params, false);
    d.pos = pos;
    declareMember(tag, d, !isMutable, Access::Public, false, false);

    // The tokens the replay will read. Appended rather than spliced: an index
    // into tokens_ is what PendingBody keeps, and an index survives the vector
    // growing where a pointer would not.
    const std::string retName = "$lret" + std::to_string(lambdaRetSeq_++);
    declareTypeName(retName, returns);
    const std::size_t start = tokens_.size();
    Token t;
    t.pos = pos;
    t.kind = TokenKind::Ident;  t.text = retName;      tokens_.push_back(t);
    t.kind = TokenKind::Keyword; t.text = "operator";  tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = "(";          tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = ")";          tokens_.push_back(t);
    t.kind = TokenKind::Punct;  t.text = "(";          tokens_.push_back(t);
    for (std::size_t i = paramsFrom; i < paramsTo; i++) tokens_.push_back(tokens_[i]);
    t.kind = TokenKind::Punct;  t.text = ")";          tokens_.push_back(t);
    if (!isMutable) {
        t.kind = TokenKind::Keyword; t.text = "const"; tokens_.push_back(t);
    }
    for (std::size_t i = bodyFrom; i < bodyTo; i++) tokens_.push_back(tokens_[i]);
    t.kind = TokenKind::End;    t.text = "";           tokens_.push_back(t);

    std::vector<PendingBody> mine;
    mine.push_back(PendingBody{ tag, start, local, tag + "::operator()" });
    replayInlineBodies(std::move(mine));

    // The object itself: a slot in this frame, and the expression is its name.
    MadeLambda record;
    record.type = closure;
    record.end = at_;
    record.names = capNames;
    record.types = capTypes;
    record.offsets = capOffsets;
    lambdaAt_[lamAt] = record;
    return buildClosure(record, pos);
}

// A slot for the closure, and each capture copied into it. Called on every
// reading of the lambda and not only the first: 7.1 reads an `auto`
// initialiser twice, and the second reading takes the cached class - so if the
// copying lived beside the building, the object the declaration actually kept
// would hold whatever was on the stack.
ExprPtr Parser::buildClosure(const MadeLambda &made, std::size_t pos) {
    const std::string name = ".lam" + std::to_string(refTemps_++);
    const int off = declare(name, made.type, pos);
    if (made.names.empty()) {
        ExprPtr obj(Var::local(name, off));
        obj->setType(made.type);
        return obj;
    }

    // `(c.x = x, c.y = y, &c)` and then a dereference of it - the same shape
    // classTemporary uses, and for the same reason: the address of a comma is
    // not something the backends take, and the address of `*p` is `p`.
    ExprPtr chain;
    for (std::size_t i = 0; i < made.names.size(); i++) {
        ExprPtr self(Var::local(name, off));
        self->setType(made.type);
        ExprPtr dst(new MemberAccess(std::move(self), made.names[i],
                                     made.offsets[i], 0, 0));
        ExprPtr src;
        if (made.names[i] == capturedThis()) {
            // The enclosing function's own `this`, copied in as a pointer.
            const Local *self = findLocal("this");
            src.reset(Var::local("this", self != nullptr ? self->offset
                                                         : thisOffset_));
            src->setType(self != nullptr ? self->type : made.types[i]);
        } else {
            src = objectRef(made.names[i]);
            if (src == nullptr) src = outerCaptureAccess(made.names[i]);
        }
        if (src == nullptr)
            src_.fail(pos, "'" + made.names[i] + "' went missing between the "
                           "capture list and the lambda");
        // Bound, not assigned, when the capture is by reference: the slot
        // holds an address, which is what `bindReference` supplies.
        if (made.types[i]->isReference()) {
            const Type *held = types_.pointerTo(made.types[i]->referent());
            dst->setType(held);
            ExprPtr addr = bindReference(made.types[i], std::move(src), pos,
                                         "'" + made.names[i] + "'");
            ExprPtr bind(new Assign(std::move(dst), std::move(addr)));
            bind->setType(held);
            if (chain == nullptr) { chain = std::move(bind); continue; }
            ExprPtr joined(new Comma(std::move(chain), std::move(bind)));
            joined->setType(held);
            chain = std::move(joined);
            continue;
        }
        dst->setType(made.types[i]);
        ExprPtr store(new Assign(std::move(dst), decay(std::move(src))));
        store->setType(made.types[i]);
        if (chain == nullptr) { chain = std::move(store); continue; }
        ExprPtr both(new Comma(std::move(chain), std::move(store)));
        both->setType(made.types[i]);
        chain = std::move(both);
    }
    ExprPtr whole(Var::local(name, off));
    whole->setType(made.type);
    ExprPtr at2(new Unary('&', std::move(whole)));
    at2->setType(types_.pointerTo(made.type));
    ExprPtr seq(new Comma(std::move(chain), std::move(at2)));
    seq->setType(types_.pointerTo(made.type));
    ExprPtr obj(new Unary('*', std::move(seq)));
    obj->setType(made.type);
    return obj;
}

// The enclosing object's pointer, inside a closure that captured it: the
// closure's own `this`, then its `$this` member. Null when this is not such a
// closure, which is every other context.
ExprPtr Parser::capturedThisPointer() {
    if (currentClass_ == nullptr) return nullptr;
    std::map<std::string, const Type *>::const_iterator outer =
        closureOuter_.find(currentClass_->unqualified()->tag());
    if (outer == closureOuter_.end()) return nullptr;
    const Member *held = currentClass_->unqualified()->findMember(capturedThis());
    if (held == nullptr) return nullptr;
    const Local *self = findLocal("this");
    if (self == nullptr) return nullptr;

    ExprPtr me(Var::local("this", self->offset));
    me->setType(self->type);
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(self->type->pointee());
    ExprPtr acc(new MemberAccess(std::move(obj), capturedThis(), held->offset,
                                 0, 0));
    acc->setType(types_.pointerTo(outer->second));
    return acc;
}

// A capture of the lambda around this one, read from inside it. By the time an
// inner lambda is parsed the outer one's capture is a member of the outer
// closure, so `findLocal` answers nothing and the capture machinery has to look
// here instead. Null for every name that is not that, which is most of them.
ExprPtr Parser::outerCaptureAccess(const std::string &name) {
    if (currentClass_ == nullptr) return nullptr;
    const Type *closure = currentClass_->unqualified();
    if (closureOuter_.find(closure->tag()) == closureOuter_.end() &&
        localClassOwner_.find(closure->tag()) == localClassOwner_.end())
        return nullptr;
    const Member *m = closure->findMember(name);
    if (m == nullptr) return nullptr;
    const Local *self = findLocal("this");
    if (self == nullptr) return nullptr;

    ExprPtr me(Var::local("this", self->offset));
    me->setType(self->type);
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(closure);
    ExprPtr acc(new MemberAccess(std::move(obj), name, m->offset, m->width,
                                 m->bitOffset));
    acc->setType(m->type);
    return useReference(std::move(acc));
}
