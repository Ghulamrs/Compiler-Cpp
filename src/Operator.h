#pragma once

#include <cstddef>
#include <string>

// The overloadable operators, and what each ABI calls them.
//
// **Every code here was measured, not read** - the habit the mangler was
// built with. `clang++ -target x86_64-linux-gnu` and
// `clang++ -target x86_64-pc-windows-msvc` over a class declaring every
// operator, with a *different parameter type on each one* so that no two
// share a signature: half the Microsoft codes are otherwise unreadable off a
// symbol listing, because `??8`/`??9` and `??M`..`??P` and `??A`/`??R` and
// `??Y`/`??Z` come out identical when the operators they name take the same
// arguments. The Windows box confirmed the Microsoft half with cl, which is
// the ABI where clang is a second implementation of it.
//
// **The two ABIs disagree about arity, and that is the one asymmetry worth
// knowing.** Itanium gives the unary and binary forms of a token different
// codes - `ml` for `a * b` and `de` for `*p`, `an` and `ad`, `mi` and `ng`,
// `pl` and `ps`. Microsoft writes one code for both and lets the parameter
// list tell them apart: `??D` is multiplication *and* dereference. So an
// Itanium name cannot be built without knowing how many operands the operator
// was written with, and a Microsoft one can.
struct OperatorCode {
    const char *spelling;      // as written after `operator`
    const char *itanium;       // the binary form, or the only form
    const char *itaniumUnary;  // the unary form where the token has both
    const char *microsoft;     // written after "??"; arity plays no part
};

// The row for `+`, `[]`, `<<=` and so on, or null when the spelling names no
// overloadable operator. The argument is the operator itself - "+" - and not
// the function name.
const OperatorCode *findOperator(const std::string &spelling);

// "operator+" -> "+", and empty for a name that is not an operator's. The
// name a declaration carries is the whole of `operator+`, so that the
// function tables can be keyed by it exactly as they are keyed by `get` or
// `size`, and this is what reads the operator back out of one.
std::string operatorSpelling(const std::string &name);

// `unary` decides between the two Itanium codes and is ignored by Microsoft.
// A caller works it out from the parameter list: a member operator with no
// parameters is unary and one with a parameter is binary, and a non-member is
// the same question shifted by one.
const char *itaniumOperatorCode(const OperatorCode &op, bool unary);
