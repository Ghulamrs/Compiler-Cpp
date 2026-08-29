#pragma once

#include "Type.h"

#include <string>
#include <vector>

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

// TemplateArg lives in Type.h: a specialization carries its arguments.

// A function template specialization. The two ABIs want two different things
// here and the difference is not cosmetic:
//
// **Itanium is given the template's PATTERN** - `T twice(T)`, with the
// parameters still in it as Kind::TemplateParam - because the name it writes
// spells `T_` where the parameter came from a template parameter, and the
// substituted signature has lost all record of that. It also encodes a return
// type, which an ordinary function's name does not. Measured:
// `_Z5twiceIiET_S0_`, and `_Z2f4IiEvT_S0_` for `void f4(T, T)`, whose second
// `T_` is `S0_` and not `S_` - the template *name* is substitution candidate
// zero.
//
// **Microsoft is given the substituted signature**, which is what it spells:
// `??$twice@H@@YAHH@Z` writes H for the return type where Itanium writes T_.
// Its template-id carries back-reference tables of its own - measured with
// cl on `??$same@US@@@@YA?AUS@@U0@`, where the parameter's name
// back-reference 0 is the S the *return type* pushed, not the S written
// inside the argument list.
bool itaniumTemplateFunctionName(const std::string &name, const Type *pattern,
                                 const std::vector<TemplateArg> &args,
                                 bool internal,
                                 std::string *out, std::string *problem);

bool microsoftTemplateFunctionName(const std::string &name, const Type *fn,
                                   const std::vector<TemplateArg> &args,
                                   std::string *out, std::string *problem);

// The Itanium name of a type's `std::type_info` object: `_ZTI` and then the
// type, spelled exactly as it is spelled in a signature - `_ZTIi`, `_ZTId`.
// For a *fundamental* type the object itself lives in the standard library,
// so naming it is all a compiler has to do; for anything else the compiler
// has to emit the object too, and this refuses those by name.
bool itaniumTypeInfoName(const Type *t, std::string *out, std::string *problem);

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
// `clsType` is the class's own Type, and the Itanium mangler needs it for the
// substitution table: the class spelled in the nested-name prefix is candidate
// zero, so a parameter that mentions the class again is S_ - measured,
// _ZN1X1mERKS_ where spelling it out gives _ZN1X1mERK1X, which clang does not
// write. The Microsoft mangler repeats names by index and needs nothing.
bool itaniumMemberName(const std::string &cls, const Type *clsType,
                       const std::string &name,
                       const Type *fn, bool constThis,
                       std::string *out, std::string *problem);

bool microsoftMemberName(const std::string &cls, const Type *clsType,
                         const std::string &name,
                         const Type *fn, char access, bool constThis,
                         std::string *out, std::string *problem);

// A constructor. **Itanium gives one constructor two names** - C1 for a
// complete object and C2 for a base subobject - and clang emits both. Only C1
// is spelled here, because C2 is called from a derived class's constructor and
// there is no inheritance yet - but both are emitted anyway, C2 as a second
// label in front of C1's body, because an object file missing one is not the
// object file clang produces. Which one a construction *calls* was measured by
// reading the call: C1. The Microsoft ABI has one name, ??0, and writes '@' where a
// member function writes its return type.
bool itaniumConstructorName(const std::string &cls, const Type *clsType,
                            const Type *fn,
                            bool complete, std::string *out, std::string *problem);

bool microsoftConstructorName(const std::string &cls, const Type *clsType,
                              const Type *fn, char access,
                              std::string *out, std::string *problem);

// A destructor. The same two-name split as a constructor - D1 complete, D2
// base - and the same reason for emitting both. Microsoft writes ??1. There is
// also a D0, the deleting destructor, which belongs to polymorphic delete and
// so to rung 4; clang emits none here.
bool itaniumDestructorName(const std::string &cls, const Type *clsType,
                           bool complete, std::string *out);

std::string microsoftDestructorName(const std::string &cls, const Type *clsType,
                                    char access);

// The deleting destructor - Itanium's D0 and Microsoft's ??_G - which lives in
// a vtable and is the one `delete p` through a base pointer reaches. No
// program writes it, so nothing else names it.
std::string itaniumDeletingDestructorName(const std::string &cls,
                                          const Type *clsType);

std::string microsoftDeletingDestructorName(const std::string &cls,
                                            const Type *clsType);

// The copy assignment operator, the one operator this compiler names so far.
// Itanium spells `operator=` as the two-letter code `aS` where a member
// function writes its name's length and letters, and writes no return type;
// Microsoft replaces the whole `?name@` with `??4` and - unlike a constructor,
// which writes a bare '@' there - does write the return type. Both measured,
// cl first: ??4Poly@@QEAAAEAU0@AEBU0@@Z and _ZN4PolyaSERKS_.
//
// One operator and not a table of them: the rest arrive with operator
// overloading, and until then `operator` is refused by name.
bool itaniumCopyAssignName(const std::string &cls, const Type *clsType,
                           const Type *fn, std::string *out, std::string *problem);

bool microsoftCopyAssignName(const std::string &cls, const Type *clsType,
                             const Type *fn, char access,
                             std::string *out, std::string *problem);

// A static data member: one object shared by the class rather than one per
// object, and so a global that the class gave its name to. Itanium spells it
// like a member function without the parameters and without any note of the
// access - `_ZN1C3pubE` whether it is public or private. Microsoft writes the
// access as a **digit** where a member function writes a letter, measured with
// cl: 2 public, 1 protected, 0 private, and then the type exactly as a
// namespace-scope variable has it - `?pub@C@@2HA`, `?priv@C@@0HA`,
// `?k@S@@2HB` for a const one.
std::string itaniumStaticMemberName(const std::string &cls, const Type *clsType,
                                    const std::string &name);

bool microsoftStaticMemberName(const std::string &cls, const Type *clsType,
                               const std::string &name,
                               const Type *t, char access,
                               std::string *out, std::string *problem);

// A variable at namespace scope. The Microsoft ABI mangles it whatever its
// linkage; Itanium leaves an external one alone and marks an internal one,
// the same way it marks an internal function.
bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem);

std::string itaniumDataName(const std::string &name, bool internal);
