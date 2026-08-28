#pragma once

#include <string>

class Type;

// The linkage name of something with C++ linkage, in the ABI of the platform
// it is being compiled for. Conforming to the platform ABI rather than
// inventing one is what lets clang and cl be oracles at the object level:
// these names can be diffed against theirs, and objects can be linked with
// theirs. Everything here was checked against
//
//   clang++ -target x86_64-linux-gnu       (Itanium, Linux and Darwin)
//   clang++ -target x86_64-pc-windows-msvc (Microsoft)
//
// rather than read off a description of the ABI.
//
// Each returns false and fills 'problem' when a type has no name to give -
// an unnamed class is the case that turns up - so the caller can refuse at
// the declaration with a position rather than emit something that will not
// link.
// 'internal' is what 'static' at file scope makes a function. Itanium says so
// in the name, with an L after the _Z; the Microsoft ABI does not distinguish
// them, and takes the flag only so the two can be called the same way.
bool itaniumFunctionName(const std::string &name, const Type *fn, bool internal,
                         std::string *out, std::string *problem);

bool microsoftFunctionName(const std::string &name, const Type *fn, bool internal,
                           std::string *out, std::string *problem);

// A non-static member function. Both ABIs spell the class into the name, and
// both record whether `this` is const - Itanium with a K after the _ZN,
// Microsoft with a B where a non-const one has an A.
//
// **The Microsoft name carries the access and the Itanium name does not**,
// which is measured rather than assumed: Q is public, I protected, A private,
// and clang writes ?priv@C@@AEAAHXZ for a private member of C. So a member
// that changes from private to public changes its symbol on Windows and keeps
// it on Linux.
bool itaniumMemberName(const std::string &cls, const std::string &name,
                       const Type *fn, bool constThis,
                       std::string *out, std::string *problem);

bool microsoftMemberName(const std::string &cls, const std::string &name,
                         const Type *fn, char access, bool constThis,
                         std::string *out, std::string *problem);

// A variable at namespace scope. The Microsoft ABI mangles it whatever its
// linkage; Itanium leaves an external one alone and marks an internal one,
// the same way it marks an internal function.
bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem);

std::string itaniumDataName(const std::string &name, bool internal);
