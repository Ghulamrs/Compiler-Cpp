#pragma once
// The constant folder, lifted out of the parser, and the three questions it
// has to ask back.
//
// **What this is not.** It is not the grammar of a constant expression -
// `static_assert`, `noexcept(e)` and an array bound are read by the parser,
// because reading them means reading tokens and calling the expression
// parser. This is the arithmetic underneath: given an expression that has
// already been built, what is it worth. `Parser::constantExpression` parses
// and then asks this; the two halves were in one file and only one of them
// was ever about folding.
//
// **Why an interface and not a back pointer.** Folding a name needs the name
// tables, which belong to the parser: a local that is a constant, a global
// that is, a static member kept out of the member list. Handing this class a
// `const Parser &` would have been shorter and would have bought nothing -
// the coupling would still be to all of it. Three questions is the whole of
// what folding needs, and writing them down is what makes the dependency
// small enough to see. It is the same shape `Visitor` gives the back end.
//
// **It owns its state, and that is load-bearing for the parallel build.**
// cxx1 compiles translation units on a thread pool and is safe by sharing
// nothing - `Driver::compile` builds its own `TypeTable` and its own `Parser`
// on the worker's stack. So this is a *member* of `Parser`, by value, and it
// inherits that per-thread lifetime. It must never become a service handed to
// several parsers, and it must never grow a `static` cache of folded results:
// that would be one object shared by every compiling thread, and because
// folding is deterministic the answers would usually be right, which is the
// hardest kind of race to see.

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Expr;
class Stmt;
class Source;
class Target;
class Type;

// What a name is worth, asked of whoever owns the name tables. Answering
// `false` means "not a constant here", which is a fold that does not happen
// rather than an error - the caller decides whether that is a diagnostic.
class ConstantNames {
public:
    virtual ~ConstantNames() = default;
    virtual bool localConstant(const std::string &name, long long *out) const = 0;
    virtual bool globalConstant(const std::string &name, long long *out) const = 0;
    virtual bool staticMemberConstant(const std::string &symbol,
                                      long long *out) const = 0;
};

class ConstantEvaluator {
public:
    // **A `constexpr` function, kept so that fold() can run it.**
    // [dcl.constexpr] in C++11 lets the body be one return statement, which
    // makes evaluating a call a fold of that expression with the parameters
    // standing for the arguments. The function is still compiled and callable
    // at run time.
    struct Fn {
        const Expr *value = nullptr;   // owned by the Function in the Program
        std::vector<int> slots;        // parameter frame slots, in order
        std::size_t pos = 0;
    };

    ConstantEvaluator(const Source &src, const Target &target,
                      const ConstantNames &names)
        : src_(src), target_(target), names_(names) {}

    // Recorded as each `constexpr` function is defined, keyed by mangled symbol.
    void define(const std::string &symbol, const Fn &fn) { fns_[symbol] = fn; }

    bool fold(const Expr &e, long long *out, std::size_t pos) const;
    long long narrowTo(long long v, const Type *t) const;

    // The one expression a `constexpr` body may be. A pure walk of the tree,
    // which is why it asks nothing of `names_`.
    const Expr *singleReturnValue(const Stmt &body) const;

private:
    const Source &src_;
    const Target &target_;
    const ConstantNames &names_;

    std::map<std::string, Fn> fns_;

    // One frame per call being folded, holding what each parameter slot is
    // worth. Mutable because fold() is const and answering a call means
    // pushing one.
    mutable std::vector<std::vector<std::pair<int, long long> > > frames_;
};
