# What cxx1 does not accept

**The language cxx1 accepts is C++11 minus this list, and the library it
ships is the one in `include/` rather than a conforming one.** That sentence is
why this file exists. `CLAUDE.md`
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

**Measured at `ffdf677`: 101 refusal sites, 93 distinct messages.** Every site
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
`src/parser/ParserOverload.cpp:632` is refused during overload ranking, while
`take(P(4))` and `P q = P(3);` compile. Both were checked with a program before
this sentence was written, and the same habit is the reason `pending[]` had
eight keywords in it that were implemented.

---

## The library, which is no longer nothing

**`include/` holds the C++ headers this compiler ships**, and `lib/` holds the
sixteen C headers underneath them. What is there: `<cstddef>`, `<cstdlib>`,
`<cstring>`, `<cmath>` and `<cctype>`, each its C header wrapped by
using-declarations into `std`, with every name that header declares; and
`<string>`, which is a class.

`<utility>`, `<vector>`, `<map>`, `<set>` and `<algorithm>` are there too: a
vector is a growing array, a map is a sorted vector of pairs, a set is a sorted
vector, and an iterator in all of them is a pointer.

`<iostream>`, `<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<ios>` and
`<cstdio>` are there now. Two things made them possible and one had to be
fixed. **`std::cout` is an object at file scope with no constructor** - an
aggregate with a constant initialiser, whose `FILE *` is resolved at the point
of use - because a file-scope object with a constructor would have to run one
before `main`, which is refused below. **`if (stream >> v)` is a conversion
function**, which is why these could not have been written before those landed.
And a **reference to a base would not bind to a derived object**, so
`std::getline(istringstream, s)` found no matching function; the pointer form
had always worked and the reference form was never done.

A stringstream reuses its base's operators through one pointer - `ostream::buf_`
and `istream::src_` - rather than a virtual, because a virtual needs a vptr and
a vptr needs the constructor `cout` cannot have.

What the streams do not have: **`rdbuf()` and the `streambuf` layer under it**,
so `ss << in.rdbuf()` - the idiom for slurping a file - has no meaning here;
read with `getline` or `ifstream::readAll`. There are no format flags either,
so no `setw`, no `setprecision`, and no `boolalpha` - a `bool` prints as `1`,
which is what the default is anyway.

A converting constructor is called to make an argument now - [over.ics.user] -
so `m["key"]` and `f(literal)` against a `const std::string &` parameter work,
and the containers are written the way anyone writes them. What that rule does
*not* do is chain: at most one user-defined conversion per sequence, so nothing
makes a `Far` out of an `int` because a `Near` sits between them.

Two limits the headers themselves carry, both compiler limits rather than
choices. **There is no `inline`**, and no weak or linkonce linkage to give it,
so a header's non-member functions are `static`: every translation unit that
includes one gets its own copy. And **`<cmath>` overloads nothing** - `lib/`
declares the `double` form alone, so `std::sqrt(2.0f)` converts to double and
back rather than calling `sqrtf`. The answer is right; the call is not the one a
conforming library makes.

The C headers underneath, unchanged and usable on their own: `assert.h`, `ctype.h`, `errno.h`, `float.h`, `limits.h`,
`locale.h`, `math.h`, `memory.h`, `setjmp.h`, `signal.h`, `stdarg.h`,
`stddef.h`, `stdio.h`, `stdlib.h`, `string.h`, `time.h`.

So what is still missing is `<memory>`, `<initializer_list>`, `<exception>`,
`<stdexcept>`, `<iomanip>`, `<list>`, `<deque>`, `<iterator>`, `<limits>` and
the rest - and inside the headers that do exist, whatever a program reaches for
that was not written. This is a library sized to what has been asked of it
rather than to the standard, and the distance between those two is not small.
It is also why a *language* feature is refused in one place: `auto` from a
braced initialiser deduces an `initializer_list`, which there is no library for
— `src/parser/ParserTemplate.cpp:877`.

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
  throw it from. `src/parser/ParserExprNew.cpp:471`
- **`dynamic_cast` naming a class with more than one base** — that wants
  `__vmi_class_type_info`, a third shape carrying the bases' offsets and flags.
  Such a class still compiles and its vtable still works; only the cast is
  refused. `src/parser/ParserExprNew.cpp:536`
- **`typeid`** — in the keyword table below. Nothing emits a `type_info` for a
  *fundamental* type either; a class's is what landed.

## Templates

Rung 5 landed function and class templates, deduction, partial specialization,
SFINAE and variadic packs. What is left:

- **a default template argument** — `src/parser/ParserTemplate.cpp:81`
- **a template template parameter** — `src/parser/ParserTemplate.cpp:43`
- **a member template** — `src/parser/ParserType.cpp:259`
- **an unnamed template parameter** — `src/parser/ParserTemplate.cpp:52`,
  `src/parser/ParserTemplate.cpp:75`
- **a non-type parameter pack** — a pack of types is supported.
  `src/parser/ParserTemplate.cpp:63`
- **a non-type template parameter that is not an integer type** —
  `src/parser/ParserTemplate.cpp:633`
- **two templates of one name** — the single feature between cxx1 and the
  `enable_if` overload idiom. `src/parser/ParserTemplate.cpp:422`
- **an explicit specialization of a *function* template** — the class form
  works. `src/parser/ParserTemplate.cpp:437`
- **`template <>` where a parameter list is expected** —
  `src/parser/ParserTemplate.cpp:38`
- **explicit instantiation** — `src/parser/ParserTemplate.cpp:292`
- **a template that is neither a class nor a function** —
  `src/parser/ParserTemplate.cpp:249`
- **a constructor or destructor of a class template written outside the class**
  — `src/parser/ParserTemplate.cpp:325`
- **naming a function template without calling it** —
  `src/parser/ParserTemplate.cpp:1502`, `src/parser/ParserTemplate.cpp:1558`
- **naming a member through a template's argument list**, `A<int>::n` — a
  `typedef` for the instantiation reaches it.
  `src/parser/ParserTemplate.cpp:1592`
- **instantiating a template that was only declared** —
  `src/parser/ParserTemplate.cpp:1596`
- **`sizeof` of a template parameter in a signature** — the linker name would
  have to spell the expression. `src/parser/ParserExpr.cpp:1920`

## Classes, members and friends

- **a virtual base** — `src/parser/ParserType.cpp:144`
- **one name holding both a static and a non-static member**, where overload
  resolution picks the non-static one — the arguments have been read by then,
  and there is no honest way back to the call that takes an object.
  `src/parser/ParserExpr.cpp:1158`. A static member function on its own works.
- **a member function of a union** — `src/parser/ParserClass.cpp:1935`
- **`friend class X;`** — one named function can be befriended.
  `src/parser/ParserType.cpp:382`
- **befriending one member function of another class** —
  `src/parser/ParserType.cpp:394`
- **a friend function defined inside the class body** —
  `src/parser/ParserType.cpp:411`
- **a const member named in a mem-initialiser list** —
  `src/parser/ParserTopLevel.cpp:720`
- **a delegating constructor** — `src/parser/ParserTopLevel.cpp:766`

## Conversion functions and operators

- **`explicit` on a conversion function** — C++11's addition, and the only part
  of one that is missing: an explicit conversion has to be refused everywhere
  except a `static_cast` and a condition, and accepting the keyword while
  ignoring that rule is a claim the compiler cannot support, which is the defect
  this file exists for. `src/parser/ParserType.cpp:320`. The conversion function
  itself works, in both directions and on all three targets.
- **`operator new` / `operator delete`** as user functions —
  `src/parser/ParserType.cpp:1246`
- **`operator->*`** — `src/parser/ParserType.cpp:1251`
- **a user-defined literal** — `src/parser/ParserType.cpp:1253`
- **`operator&&`, `operator||`, `operator,` and `operator->*`** — the four that
  still fall into the generic refusal below, **named** rather than left to it.
  The ten compound assignments used to be here too and are reachable now; a
  class that declares `operator+` and `operator=` but not `operator+=` is still
  refused `+=`, because [over.ass] makes each `@=` an operator of its own and
  the rewrite into `+` and an assignment is for built-in operands only.
  A bullet that says only "an operator that can be named but not reached" hid
  `operator[]`, `operator=` and `operator->` for as long as it stood, which
  meant the document could not answer "can a container be written here?" - and
  the answer was no. Those three are reachable now; these four are what is
  left, and none is needed to write one. The first two would want the
  short-circuit to stop short-circuiting, which is the whole of why they are
  rarely overloaded.
- **an operator that can be named but not reached** — the rule the four above
  are refused by: refused at the declaration, because a function that links and
  can never be called is the half-built thing this project refuses everywhere.
  Every other overloadable operator resolves from an expression, asked of a
  one-line program each: `+ - * / % & | ^ << >> == != < <= > >=` binary,
  `+ - * & ! ~ ++ --` unary, and `() [] = ->`.
  `src/parser/ParserType.cpp:1367`

## Initialisation, and braces

- **list-initialisation calling a constructor**, `P p{1, 2}` — write the
  arguments in parentheses. The *empty* pair is read: `{}` is
  value-initialisation. `src/parser/ParserStmt.cpp:170`,
  `src/parser/ParserInit.cpp:38`
- **a braced default argument** — `src/parser/ParserClass.cpp:2513`,
  `src/parser/ParserTopLevel.cpp:530`
- **a braced member initialiser** — `src/parser/ParserType.cpp:747`
- **an initialiser for an array of a class** —
  `src/parser/ParserStmt.cpp:111`, `src/parser/ParserTopLevel.cpp:738`
- **an array of a class with a destructor** — the elements would have to be
  destroyed in reverse; an array of a class with only constructors works.
  `src/parser/ParserStmt.cpp:119`
- **a bit-field initialised at file scope** — `src/parser/ParserInit.cpp:605`

## Objects that would run code before `main`

Nothing runs before `main`, so every shape that would need to is refused where
it is written:

- **a static local with a constructor** — `src/parser/ParserStmt.cpp:140`
- **a static local array whose elements have a constructor** —
  `src/parser/ParserStmt.cpp:107`
- **a file-scope object with a constructor** —
  `src/parser/ParserTopLevel.cpp:277`
- **a static data member of a class with a constructor** —
  `src/parser/ParserClass.cpp:1868`
- **a static reference** — `src/parser/ParserStmt.cpp:326`
- **a reference at file scope** — `src/parser/ParserTopLevel.cpp:260`

## Expressions

- **`static_cast` of a reference to a different type** —
  `src/parser/ParserExpr.cpp:268`
- **a name qualified with `::` alone in an *expression*** — as a *type* it
  works: `::Lexer *p;` names the global scope past a nearer class or
  namespace, which is one look in one table. A name in an expression goes
  through `qualifyForLookup`, where a namespace and a using-directive get
  their say, and restricting that for one name is a flag that has to be put
  down again before the call's arguments are parsed.
  `src/parser/ParserExpr.cpp:846`
- **choosing an overload by the type it is assigned to** —
  `src/parser/ParserExpr.cpp:499`
- **a pointer to a *virtual* member function** — it holds a vtable index where
  this holds an address. `src/parser/ParserExpr.cpp:1792`
- **a pointer to a *const* member function** — the constness of `this` is not
  part of a function type here. `src/parser/ParserType.cpp:1448`
- **postfix `++` / `--` on a bit-field** — the prefix form works.
  `src/parser/ParserOperator.cpp:489`
- **`va_arg` of an aggregate** — `src/parser/ParserExpr.cpp:676`
- **a functional-cast temporary reached through overload ranking** — a
  converting constructor is not tried at a call.
  `src/parser/ParserOverload.cpp:632`

## `new` and `delete`

- **placement new**, and a parenthesised type-id after `new` —
  `src/parser/ParserExprNew.cpp:744`
- **more than one value in a new-expression** —
  `src/parser/ParserExprNew.cpp:824`
- **`new T[n]` of a class with a constructor** —
  `src/parser/ParserExprNew.cpp:831`
- **`new T[n][m]`** — only the first dimension may be given.
  `src/parser/ParserExprNew.cpp:772`
- **`new T{...}`** — `src/parser/ParserExprNew.cpp:798`
- **`delete[]` of a polymorphic type** — `src/parser/ParserExprNew.cpp:1033`
- **`delete[]` of a type with a destructor** — the count `new[]` would have
  recorded is not written. `src/parser/ParserExprNew.cpp:1101`

## Statements, exceptions and control

- **a local with a destructor and a `try` in one function** — each is a range
  in the call-site table and one would have to split the other.
  `src/parser/ParserStmt.cpp:707`, `src/parser/ParserStmt.cpp:941`,
  `src/parser/ParserStmt.cpp:1437`
- **a class declared in the condition of a `while`** — [stmt.iter]/2 builds it
  afresh on every turn and destroys it at the end of each one, and the
  construction would have to be written where the test is. A scalar works, and
  so does a class in the condition of an `if`, where the object is built once.
  `src/parser/ParserStmt.cpp:647`
- **a `try` inside another** — `src/parser/ParserStmt.cpp:981`
- **catching by reference** — catch by value.
  `src/parser/ParserStmt.cpp:1032`
- **a rethrow**, `throw;` with nothing after it —
  `src/parser/ParserStmt.cpp:1205`
- **a dynamic exception specification**, `throw(T)` — `throw()` with nothing in
  it is `noexcept` and works. `src/parser/ParserConst.cpp:71`
- **a range-based `for` over anything but an array** — a class would need its
  `begin()` and `end()` found and called. `src/parser/ParserStmt.cpp:487`
- **a reference loop variable in a range-based `for`** —
  `src/parser/ParserStmt.cpp:495`
- **a trailing return type**, `auto f(int) -> int` — C++11, and refused as the
  C++11 feature it is rather than as `auto` deduction.
  `src/parser/ParserTopLevel.cpp:401`

## Namespaces and lookup

- **a namespace alias**, `namespace A = N;` —
  `src/parser/ParserTopLevel.cpp:95`
- **a using-declaration inside a class**, `using B::f;` — it redeclares a base
  member rather than naming one, changing its access and joining the derived
  class's overload set. `src/parser/ParserType.cpp:266`
- **a using-declaration inside a block** — it would declare a name for the rest
  of the block and rank against the locals beside it.
  `src/parser/ParserStmt.cpp:1189`. The one at namespace scope,
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
| `decltype(auto)` | C++14 | `src/parser/ParserExpr.cpp:1319` |
| `[n = k]`, an init-capture | C++14 | `src/parser/ParserExprLambda.cpp:184` |
| `auto` as a parameter type | C++14 | `src/parser/ParserClass.cpp:2501`, `src/parser/ParserTopLevel.cpp:474` |
| `auto` as a return type | C++14 | `src/parser/ParserTopLevel.cpp:406` |
| a variable template | C++14 | `src/parser/ParserTemplate.cpp:309` |
| `S s = {1, 2}` with an NSDMI — not an aggregate in C++11 | C++14 changed the rule | `src/parser/ParserInit.cpp:642`, `src/parser/ParserStmt.cpp:163`, `src/parser/ParserTopLevel.cpp:271` |
| `static_assert` with no message | C++17 | `src/parser/ParserConst.cpp:31` |
| `namespace N::M { }` | C++17 | `src/parser/ParserTopLevel.cpp:92` |
| an attribute, `[[noreturn]]` | none parse | `src/parser/ParserType.cpp:1157` |

## Keywords the parser has no rule for

Twenty, from `pending[]` in `src/parser/Parser.cpp`. Each is refused **by
name** at the three doors a keyword can arrive at — an expression, a member
declaration, and a name — rather than as a parse error further along:
`src/parser/Parser.cpp:99`, `src/parser/ParserExpr.cpp:613`,
`src/parser/ParserType.cpp:1161`.

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
  `src/parser/ParserStmt.cpp:1214`
- **a virtual function overridden from a base that is not the first, on the
  Microsoft ABI** — cl compiles such an override against a biased `this` where
  Itanium puts a thunk in front, so this is a difference in code generation
  rather than in naming. `src/parser/ParserClass.cpp:798`

A case that cannot be compiled for a target names it in `<case>.notarget` with
the reason on the line, which is printed on every run.

## Three the tool finds that are not exclusions

`tools/exclusions` cannot tell a refusal from an ordinary diagnostic that
happens to say "yet", and filtering them inside the script would hide a
judgement in a program. They are named here instead:

- `src/parser/ParserType.cpp:168` — a base class that is not yet defined. An
  ordinary error: a derived object contains its base, so the base has to be
  complete.
- `src/Mangle.cpp:566`, `src/Mangle.cpp:1052` — a type with no Itanium or
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
