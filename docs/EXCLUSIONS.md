# What cxx1 does not accept

**The language cxx1 accepts is C++11 minus this list, and there is no C++
standard library at all.** That sentence is why this file exists. `CLAUDE.md`
opens by saying the language cxx1 compiles is C++11, which is the right
headline and was, on its own, a claim the compiler cannot support: a C++11
compiler that refuses `dynamic_cast` is a C++11 *subset*, and a reader who
believed the headline would take a conforming C++98 program, watch it fail,
and have nowhere to look.

The tree's own rule 4 says refuse by name and never accept quietly, and rule 5
says a claim with no oracle is not allowed to be believed. This file is those
two rules turned on the top-line claim. It is not hand-written: the inventory
comes out of the source, and `tools/exclusions --check` says when it has
drifted.

## How to re-derive this list

```sh
tools/exclusions                          # every refusal site, file:line and message
tools/exclusions --count                  # the two numbers below
tools/exclusions --check docs/EXCLUSIONS.md
```

**Measured at `7aa423d`: 104 refusal sites, 96 distinct messages.** Every site
is cited below, which is what `--check` verifies — it reports each refusal the
source raises and this document does not cite, and each citation whose site is
gone. Run it after adding or removing a refusal; a document that has to be
remembered is the thing this one was written to replace.

**One grep does not find them, and that is worth fixing.** Compiler++'s
`KNOWN-GAPS.md` can say `grep -h 'not supported in this version'` and be
complete, because all forty of its sites use that one phrase. Here the
messages are written three ways — *"is not supported yet"*, *"is C++14, and
this compiler is C++11"*, and eleven that carry neither and say only *"yet"*.
`dynamic_cast`'s message is in the third group, so the most consequential
exclusion in the compiler is the one a naive grep misses. **The house rule
worth adopting: a new refusal says "is not supported yet" or names a standard
version.** Until every message does, `tools/exclusions` is the derivation and
a grep is not.

**Ask a one-line program before believing any single line.** A refusal's
*reachability* depends on where it sits, and three of these read more broadly
than they fire. `template <>` at `src/parser/ParserTemplate.cpp:38` is refused
where a template parameter list is expected, while
`template <> struct Box<int> { … };` compiles; the functional-cast temporary at
`src/parser/ParserOverload.cpp:541` is refused during overload ranking, while
`take(P(4))` and `P q = P(3);` compile. Both were checked with a program before
this sentence was written, and the same habit is the reason `pending[]` had
eight keywords in it that were implemented.

---

## The largest exclusion is not a language feature

**There is no C++ standard library, and there never will be** — `lib/` is
sixteen **C** headers: `assert.h`, `ctype.h`, `errno.h`, `float.h`, `limits.h`,
`locale.h`, `math.h`, `memory.h`, `setjmp.h`, `signal.h`, `stdarg.h`,
`stddef.h`, `stdio.h`, `stdlib.h`, `string.h`, `time.h`.

So there is no `<string>`, `<vector>`, `<map>`, `<set>`, `<iostream>`,
`<sstream>`, `<algorithm>`, `<memory>` or `<initializer_list>`, and no `std::`
namespace beyond what a program declares itself. This is a decision rather than
a gap, and it is the one exclusion that stops an ordinary C++ program before
the language is reached at all. It is also why a *language* feature is refused
in one place: `auto` from a braced initialiser deduces an `initializer_list`,
which there is no library for — `src/parser/ParserTemplate.cpp:866`.

A conforming C++ implementation is a compiler **and** a library. cxx1 is a
language translator with three code generators. Read every claim about C++11
in this tree with that in front of it.

## Run-time type information

A class carries a run-time description on all three targets now — `_ZTI` and
`_ZTS` behind the vtable on Itanium, five `??_R` records and a locator in front
of the vftable on Microsoft — and `dynamic_cast` to a pointer works on each.
What is left of it:

- **`dynamic_cast` to a reference** — it has no null to answer with, so a
  failure throws `std::bad_cast`, and there is no C++ standard library here to
  throw it from. `src/parser/ParserExprNew.cpp:242`
- **`dynamic_cast` naming a class with more than one base** — that wants
  `__vmi_class_type_info`, a third shape carrying the bases' offsets and flags.
  Such a class still compiles and its vtable still works; only the cast is
  refused. `src/parser/ParserExprNew.cpp:307`
- **`typeid`** — in the keyword table below. Nothing emits a `type_info` for a
  *fundamental* type either; a class's is what landed.

## Templates

Rung 5 landed function and class templates, deduction, partial specialization,
SFINAE and variadic packs. What is left:

- **a default template argument** — `src/parser/ParserTemplate.cpp:81`
- **a template template parameter** — `src/parser/ParserTemplate.cpp:43`
- **a member template** — `src/parser/ParserType.cpp:258`
- **an unnamed template parameter** — `src/parser/ParserTemplate.cpp:52`,
  `src/parser/ParserTemplate.cpp:75`
- **a non-type parameter pack** — a pack of types is supported.
  `src/parser/ParserTemplate.cpp:63`
- **a non-type template parameter that is not an integer type** —
  `src/parser/ParserTemplate.cpp:630`
- **two templates of one name** — the single feature between cxx1 and the
  `enable_if` overload idiom. `src/parser/ParserTemplate.cpp:420`
- **an explicit specialization of a *function* template** — the class form
  works. `src/parser/ParserTemplate.cpp:435`
- **`template <>` where a parameter list is expected** —
  `src/parser/ParserTemplate.cpp:38`
- **explicit instantiation** — `src/parser/ParserTemplate.cpp:292`
- **a template that is neither a class nor a function** —
  `src/parser/ParserTemplate.cpp:249`
- **a constructor or destructor of a class template written outside the class**
  — `src/parser/ParserTemplate.cpp:325`
- **naming a function template without calling it** —
  `src/parser/ParserTemplate.cpp:1450`, `src/parser/ParserTemplate.cpp:1506`
- **naming a member through a template's argument list**, `A<int>::n` — a
  `typedef` for the instantiation reaches it.
  `src/parser/ParserTemplate.cpp:1540`
- **instantiating a template that was only declared** —
  `src/parser/ParserTemplate.cpp:1544`
- **`sizeof` of a template parameter in a signature** — the linker name would
  have to spell the expression. `src/parser/ParserExpr.cpp:1672`

## Classes, members and friends

- **a virtual base** — `src/parser/ParserType.cpp:143`
- **one name holding both a static and a non-static member**, where overload
  resolution picks the non-static one — the arguments have been read by then,
  and there is no honest way back to the call that takes an object.
  `src/parser/ParserExpr.cpp:965`. A static member function on its own works.
- **a member function of a union** — `src/parser/ParserClass.cpp:1844`
- **`friend class X;`** — one named function can be befriended.
  `src/parser/ParserType.cpp:364`
- **befriending one member function of another class** —
  `src/parser/ParserType.cpp:376`
- **a friend function defined inside the class body** —
  `src/parser/ParserType.cpp:393`
- **a const member named in a mem-initialiser list** —
  `src/parser/ParserTopLevel.cpp:679`
- **a delegating constructor** — `src/parser/ParserTopLevel.cpp:725`

## Conversion functions and operators

- **a conversion function**, `operator int()` — there are none at all, which is
  also why `explicit` on one is refused naming the conversion function rather
  than the keyword. `src/parser/ParserType.cpp:983`,
  `src/parser/ParserType.cpp:1095`, `src/parser/ParserType.cpp:310`
- **`operator@=`**, the compound assignments — a compound assignment on a class
  is that operator alone and is not rewritten.
  `src/parser/ParserOperator.cpp:368`
- **`operator new` / `operator delete`** as user functions —
  `src/parser/ParserType.cpp:1086`
- **`operator->*`** — `src/parser/ParserType.cpp:1091`
- **a user-defined literal** — `src/parser/ParserType.cpp:1093`
- **an operator that can be named but not reached** — refused at the
  declaration, because a function that links and can never be called is the
  half-built thing this project refuses everywhere.
  `src/parser/ParserType.cpp:1146`

## Initialisation, and braces

- **list-initialisation calling a constructor**, `P p{1, 2}` — write the
  arguments in parentheses. The *empty* pair is read: `{}` is
  value-initialisation. `src/parser/ParserStmt.cpp:157`,
  `src/parser/ParserInit.cpp:38`
- **a braced default argument** — `src/parser/ParserClass.cpp:2404`,
  `src/parser/ParserTopLevel.cpp:514`
- **a braced member initialiser** — `src/parser/ParserType.cpp:676`
- **an initialiser for an array of a class** —
  `src/parser/ParserStmt.cpp:101`, `src/parser/ParserTopLevel.cpp:697`
- **an array of a class with a destructor** — the elements would have to be
  destroyed in reverse; an array of a class with only constructors works.
  `src/parser/ParserStmt.cpp:109`
- **a bit-field initialised at file scope** — `src/parser/ParserInit.cpp:605`

## Objects that would run code before `main`

Nothing runs before `main`, so every shape that would need to is refused where
it is written:

- **a static local with a constructor** — `src/parser/ParserStmt.cpp:127`
- **a static local array whose elements have a constructor** —
  `src/parser/ParserStmt.cpp:97`
- **a file-scope object with a constructor** —
  `src/parser/ParserTopLevel.cpp:271`
- **a static data member of a class with a constructor** —
  `src/parser/ParserClass.cpp:1798`
- **a static reference** — `src/parser/ParserStmt.cpp:300`
- **a reference at file scope** — `src/parser/ParserTopLevel.cpp:254`

## Expressions

- **`?:` as an lvalue** — real C++ when both arms are lvalues; bind the
  reference in an `if`/`else`. `src/parser/ParserExpr.cpp:1244`
- **`static_cast` of a reference to a different type** —
  `src/parser/ParserExpr.cpp:232`
- **a name qualified with `::` alone**, the global scope —
  `src/parser/ParserExpr.cpp:777`
- **choosing an overload by the type it is assigned to** —
  `src/parser/ParserExpr.cpp:1091`
- **a pointer to a *virtual* member function** — it holds a vtable index where
  this holds an address. `src/parser/ParserExpr.cpp:1544`
- **a pointer to a *const* member function** — the constness of `this` is not
  part of a function type here. `src/parser/ParserType.cpp:1227`
- **postfix `++` / `--` on a bit-field** — the prefix form works.
  `src/parser/ParserOperator.cpp:477`
- **`va_arg` of an aggregate** — `src/parser/ParserExpr.cpp:614`
- **a functional-cast temporary reached through overload ranking** — a
  converting constructor is not tried at a call.
  `src/parser/ParserOverload.cpp:541`

## `new` and `delete`

- **placement new**, and a parenthesised type-id after `new` —
  `src/parser/ParserExprNew.cpp:515`
- **more than one value in a new-expression** —
  `src/parser/ParserExprNew.cpp:594`
- **`new T[n]` of a class with a constructor** —
  `src/parser/ParserExprNew.cpp:601`
- **`new T[n][m]`** — only the first dimension may be given.
  `src/parser/ParserExprNew.cpp:543`
- **`new T{...}`** — `src/parser/ParserExprNew.cpp:568`
- **`delete[]` of a polymorphic type** — `src/parser/ParserExprNew.cpp:803`
- **`delete[]` of a type with a destructor** — the count `new[]` would have
  recorded is not written. `src/parser/ParserExprNew.cpp:871`

## Statements, exceptions and control

- **a local with a destructor and a `try` in one function** — each is a range
  in the call-site table and one would have to split the other.
  `src/parser/ParserStmt.cpp:600`, `src/parser/ParserStmt.cpp:811`
- **a `try` inside another** — `src/parser/ParserStmt.cpp:849`
- **catching by reference** — catch by value.
  `src/parser/ParserStmt.cpp:900`
- **a rethrow**, `throw;` with nothing after it —
  `src/parser/ParserStmt.cpp:1073`
- **a dynamic exception specification**, `throw(T)` — `throw()` with nothing in
  it is `noexcept` and works. `src/parser/ParserConst.cpp:71`
- **a range-based `for` over anything but an array** — a class would need its
  `begin()` and `end()` found and called. `src/parser/ParserStmt.cpp:451`
- **a reference loop variable in a range-based `for`** —
  `src/parser/ParserStmt.cpp:459`
- **a trailing return type**, `auto f(int) -> int` — C++11, and refused as the
  C++11 feature it is rather than as `auto` deduction.
  `src/parser/ParserTopLevel.cpp:391`

## Namespaces and lookup

- **a namespace alias**, `namespace A = N;` —
  `src/parser/ParserTopLevel.cpp:95`
- **a using-declaration inside a class**, `using B::f;` — it redeclares a base
  member rather than naming one, changing its access and joining the derived
  class's overload set. `src/parser/ParserType.cpp:265`
- **a using-declaration inside a block** — it would declare a name for the rest
  of the block and rank against the locals beside it.
  `src/parser/ParserStmt.cpp:1057`. The one at namespace scope,
  `using N::f;`, works, and so does `using namespace N;` here.

## Lambdas

- **naming a capture after a default one** — `[=]` and `[&]` on their own take
  everything the body reads. `src/parser/ParserExprLambda.cpp:152`

## Lexer and preprocessor

- **GNU's named variadic macro parameter** — write `...` and use `__VA_ARGS__`.
  `src/Preprocessor.cpp:747`

## Refused because of the standard version

C++11 is the target, so a C++14 or C++17 form is refused *naming the version*
rather than as a missing feature. The standing rule is that when a C++11
feature in this table's neighbourhood is built, the version-boundary refusal
beside it goes in the same commit.

| written | version | site |
| --- | --- | --- |
| `1'000`, a digit separator | C++14 | `src/Lexer.cpp:146` |
| `0b101`, a binary literal | C++14 | `src/Lexer.cpp:250` |
| `decltype(auto)` | C++14 | `src/parser/ParserExpr.cpp:1139` |
| `[n = k]`, an init-capture | C++14 | `src/parser/ParserExprLambda.cpp:184` |
| `auto` as a parameter type | C++14 | `src/parser/ParserClass.cpp:2392`, `src/parser/ParserTopLevel.cpp:466` |
| `auto` as a return type | C++14 | `src/parser/ParserTopLevel.cpp:396` |
| a variable template | C++14 | `src/parser/ParserTemplate.cpp:309` |
| `S s = {1, 2}` with an NSDMI — not an aggregate in C++11 | C++14 changed the rule | `src/parser/ParserInit.cpp:642`, `src/parser/ParserStmt.cpp:150`, `src/parser/ParserTopLevel.cpp:265` |
| `static_assert` with no message | C++17 | `src/parser/ParserConst.cpp:31` |
| `namespace N::M { }` | C++17 | `src/parser/ParserTopLevel.cpp:92` |
| an attribute, `[[noreturn]]` | none parse | `src/parser/ParserType.cpp:997` |

## Keywords the parser has no rule for

Twenty, from `pending[]` in `src/parser/Parser.cpp`. Each is refused **by
name** at the three doors a keyword can arrive at — an expression, a member
declaration, and a name — rather than as a parse error further along:
`src/parser/Parser.cpp:99`, `src/parser/ParserExpr.cpp:551`,
`src/parser/ParserType.cpp:1001`.

    alignas   alignof   and       and_eq    asm
    bitand    bitor     char16_t  char32_t  compl
    export    inline    not       not_eq    or
    or_eq     thread_local        typeid    xor       xor_eq

**A second list beside it says the opposite thing**, and the distinction is the
point: `implementedElsewhere[]` holds `catch`, `friend`, `mutable`,
`namespace`, `operator`, `template`, `using` and `virtual` — all implemented,
none of which begins an expression, so the answer is *"it is implemented, but
it does not begin an expression"* and not *"not supported yet"*. Those eight
sat in `pending[]` until 2026-09-02 and the compiler was lying about itself in
eight places. **Measure with a one-line program in a place the keyword belongs
before moving a name between the two lists**; the review that first named them
guessed wrong twice.

## Refused for one target only

- **`return` inside a `catch` on x86_64-windows** — a handler is a funclet
  there, so leaving one early is a return of the address to carry on at.
  `src/parser/ParserStmt.cpp:1082`
- **a virtual function overridden from a base that is not the first, on the
  Microsoft ABI** — cl compiles such an override against a biased `this` where
  Itanium puts a thunk in front, so this is a difference in code generation
  rather than in naming. `src/parser/ParserClass.cpp:738`

A case that cannot be compiled for a target names it in `<case>.notarget` with
the reason on the line, which is printed on every run.

## Three the tool finds that are not exclusions

`tools/exclusions` cannot tell a refusal from an ordinary diagnostic that
happens to say "yet", and filtering them inside the script would hide a
judgement in a program. They are named here instead:

- `src/parser/ParserType.cpp:167` — a base class that is not yet defined. An
  ordinary error: a derived object contains its base, so the base has to be
  complete.
- `src/Mangle.cpp:487`, `src/Mangle.cpp:938` — a type with no Itanium or
  Microsoft linkage name. Internal: reaching either means a type was built that
  the mangler was never taught, which is a bug in this compiler and not a
  statement about the language.

## The rule this file leaves behind

**A refusal and a claim are the same kind of thing, and both need an oracle.**
A refusal that names a feature the compiler now has is worse than none, because
it is a claim the compiler cannot support — that is why `pending[]` was
measured keyword by keyword. A *headline* the compiler cannot support is the
same defect one level up, which is what this document fixes. When a feature
lands, delete its entry here and run `tools/exclusions --check`; when a refusal
is added, cite it here in the same commit.
