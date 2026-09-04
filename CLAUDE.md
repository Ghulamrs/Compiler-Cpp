# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Two languages, and they are not the same one

**`src/` is ISO C++14. The language `cxx1` compiles is C++11 — minus the list
in `docs/EXCLUSIONS.md`, and with no C++ standard library at all.** Keep the
two languages apart in every sentence you write about this project, because
almost every confusing question here comes from conflating them.

The consequence worth stating up front: **this compiler can never compile
itself**, and that is deliberate rather than a gap. A C++11 compiler cannot
read C++14 source. Self-hosting is not a milestone here and offering it as one
is a mistake — see "How correctness is established" for what replaces it.

**The subset is not a footnote, and "C++11" on its own was a claim this
compiler cannot support.** Some of that has since been answered — `dynamic_cast`
works on all three targets, conversion functions exist, and `include/` holds a
real library: `<string>`, `<vector>`, `<map>`, `<set>`, `<algorithm>`,
`<utility>`, the five C wrappers, and the stream family `<iostream>`,
`<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<ios>`, `<cstdio>`. What
remains is still a subset, and a large one: the library is sized to what has
been asked of it rather than to the standard, `<memory>` and
`<initializer_list>` are not there, and `docs/EXCLUSIONS.md` runs to a hundred
entries. An ordinary conforming C++98 program may still not compile here, and a
reader who took the headline at its word would meet that with nowhere to look.
`docs/EXCLUSIONS.md` is the list — **101 refusal sites**, derived from
the source by `tools/exclusions` rather than written by hand, and
`tools/exclusions --check docs/EXCLUSIONS.md` reports every refusal the
document does not cite. This is the tree's own rule 5 — *a claim with no oracle
is not allowed to be believed* — applied to the first sentence of this file.

`-std=c++14 -Wall -Wextra -Werror -pedantic` in the `Makefile`. A Mac cannot
enforce C++14: Apple's libc++ hands you `std::string_view` in C++14 mode, so a
C++17-ism compiles clean here and is refused only when it reaches real g++.
Build on the Linux box before believing a clean Mac build. Same trap, same
answer as Compiler-C.

Trigraphs are still live in C++14, so write `'?\?'` and not `'??'` in any
diagnostic. A C++ compiler's messages are full of quoted punctuation, so this
will come up more here than it did in Compiler-S, where it cost an afternoon.

## Reading this tree

**There is an order that works, and nothing here said what it was.** This file
is written in the order things happened, which is the right shape for a record
and the wrong one for a first reading - its first sections after this one are
changelog entries. What follows is the path, and every item on it already
existed; what was missing was the list.

1. **`README.md`** - what cxx1 is, what it is not, and how to build it.
2. **Six sections of this file, in this order**, and no others yet: "Two
   languages, and they are not the same one", "Where this came from", "What
   makes C++ different from C to parse", "The ladder", "Decisions already
   taken", "How correctness is established". Then "Understandable means
   checkable", which is how a change to any of it is judged.
3. **The pipeline, by file**: `main.cpp` (five lines) → `Driver.h` and
   `Driver::compile` → `Lexer.h` → `Preprocessor.h` → **`parser/Parser.h`**,
   which at fourteen hundred lines is the front end's design document and says
   so nowhere else → `Ast.h` → `Type.h`, whose `class Target` is the machine →
   `Abi.h` and `backend/Backend.h` → `backend/Walker.h` → one code generator,
   `backend/X86_64Linux.h`.
4. **One case per rung**, whose headers are the best short prose in the tree:
   `class.cpp`, `inherit.cpp`, `multiple.cpp`, `thunk.cpp`, `tuple.cpp`,
   `cleanup.cpp`, `lambda.cpp`, and `refused.cpp` for what is said no to.
5. **The suites**: the header comments of `tests/run.sh`, `tests/emit.sh`,
   `tests/names.sh`, `tests/overload.sh`, then `tools/verify-three`, which is
   the three-box rule with a command behind it.
6. **`docs/`**: `EXCLUSIONS.md` for what this compiler does not accept — read
   it before writing a program for cxx1, since it is the difference between
   "C++11" and what is actually here — then `CONFORMANCE.md` for what compiles
   and should not, the newest `HANDOVER-*.md` for where a round stopped, and
   `DESIGN-REVIEW-2026-09-02.md` for what to change next and what not to.

**What this file is *for*, once that reading is done**, is the measurement
behind a line of code: why an ABI answer is what it is, what a bug looked like
before it was mended, which oracle was asked. Search it by its `## ` headings;
do not read it end to end.

## The parser is twelve files

`Parser.cpp` was 9,639 lines and 450 KB, which is one translation unit: every
build recompiled all of it, and every reader had to find their subject in it.
It is twelve files now, in `src/parser/` — `Parser.cpp` and eleven
`ParserXxx.cpp` beside it — still one class, declared as it always was in
`Parser.h`, which moved in with them. `src/parser/` is laid out the way `src/backend/` already
was: the directory owns its headers, reaches the shared ones as `../Type.h`,
and is included from outside by path, so `Driver.cpp` says
`#include "parser/Parser.h"`. Both builds glob it — `SRCS` in the Makefile and
the source list in `msvc/build.cmd`.

| file (in `src/parser/`) | holds |
| --- | --- |
| `Parser.cpp` | tokens, name lookup, the declaration tests, scopes and symbol tables |
| `ParserTemplate.cpp` | parameters, deduction, partial ordering, instantiation |
| `ParserType.cpp` | class, enum, the specifiers and the declarator |
| `ParserOverload.cpp` | conversions, ranking, overload resolution |
| `ParserClass.cpp` | constructors, destructors, vtables, thunks, implicit specials |
| `ParserExpr.cpp` | primaries, names, the named casts, `decltype`, references, postfix and unary |
| `ParserExprCall.cpp` | arguments, defaults, by-value copies, member calls and their access checks |
| `ParserExprLambda.cpp` | lambdas, closures, captures, the deduced return type |
| `ParserExprNew.cpp` | `new`, `delete`, `throw`, and class temporaries |
| `ParserInit.cpp` | initialisers, and the constant folding they need |
| `ParserOperator.cpp` | one function per precedence level |
| `ParserStmt.cpp` | statements, the top level, `parse()` |

**`ParserExpr.cpp` was split a second time, 2026-09-01**, by the same method
and for the first of the same two reasons: at 3,446 lines it was the largest
file in the tree by nine hundred, and a reader looking for how a lambda is
built had a third of a compiler to walk. Three files came out of it - calls,
lambdas, and the expressions that make or destroy an object - and it keeps
1,674 lines, which is the primary expression, the casts and the two operator
levels below the ladder.

**The seams were free this time**, which is worth writing down because last
time they were not: `ParserExpr.cpp` had exactly one file-scope `static`, and
its only two callers sat thirty lines below it. `binOpSpelling` has external
linkage and is declared in the shared header already. So the cut could follow
the subjects rather than the linkage, and `src/ParserInternal.h` did not gain
an entry.

**Cut by whole function, comment block included.** A range that starts at a
signature leaves the paragraph above it behind, attached to nothing - so each
function's extent was taken from the first line of its leading comment to the
last line before the next one's. Checked afterwards by counting lines: every
line of the original file is present in exactly one of the four, and the only
additions are the three new headers and their includes.

**Nothing was rewritten.** The split was cut by line range out of the old file
and the units were reassembled from those ranges, so the diff is a move and
the three boxes proved it: 97 / 153 / 52 on the Mac, 97 / 153 on Linux, 94 on
Windows and the C corpus at 379/424 — every number identical to the commit
before.

**A basename may not repeat across `src/`, `src/parser/` and `src/backend/`.**
That is why the nine kept their `ParserXxx` names on moving into a directory
that would have let them drop the prefix — `src/parser/Type.cpp` beside
`src/Type.cpp` reads better and does not work. `obj/` mirrors `src/`, so make
is safe; `msvc/build.cmd` gives cl **one flat `/Fo` directory** for every
source in one command, and there the second `Type.cpp` overwrites the first
one's object. It would have been found on the Windows box rather than here.

Where the seams went was decided by the file-scope `static` helpers rather
than by taste. A `static` cannot be seen from another unit, so a cut that
separated one from its callers would have forced it into a shared header; the
cuts were moved instead until only three helpers had callers on both sides.
Those three — `alignTo`, `isLvalue`, `isNullConstant` — lost the keyword and
live in `Parser.cpp`, declared in `src/ParserInternal.h`. That header is the
whole cost of the split, and it is deliberately short: if a later change wants
to add a fourth entry, consider moving the function instead.

## Refusing by name reaches declarations now, not only expressions

`notYetSupported` is the list of keywords the lexer knows and this parser has
no rule for, and it existed so that the word is *named* instead of the error
landing on whatever follows it. It had one caller, in `primary()`, so it only
ever worked for a keyword written in an expression.

Every keyword that begins a **declaration** slipped past it. Asked about
`friend int peek(const Account &a);` in a class body, cxx1 said `expected a
type` and pointed at `friend` - the right token, and nothing about what is
wrong with it. The same for `mutable`, `explicit`, `using` and
`static_assert`; and `int operator+(const Vec &o);`, where the type reads fine
and it is the name that is a keyword, ended in `expectIdent` saying `expected
a name`.

Two more callers fixed all seven, at the two places the generic messages were
raised: the end of `unqualifiedSpecifiers`, which is where a member
declaration that begins with an unknown keyword arrives, and `expectIdent`,
which is where one that begins with a type but is *named* by a keyword
arrives. Both now answer `'friend' is not supported yet` and so on.

**None of this implemented any of them, and that is what changed later.**
`friend` was the example: not on the ladder and never was, wanting a friend
list on the class that `checkAccessible` consults as well as a declaration
path that puts the function outside the class it is written in. Both of those
now exist - see "`friend`, and why it landed beside the operators" - and the
description above is exactly what the implementation turned out to be.

**And then the list went on saying they were missing.** Eight of the keywords in
`pending[]` had been implemented by 2026-09-02 - `catch`, `friend`, `mutable`,
`namespace`, `operator`, `template`, `using`, `virtual` - so `int x = template;`
was answered "'template' is not supported yet", which is a claim about the
compiler where the truth was about the place. There are two lists now, and the
second answers "'template' is implemented, but it does not begin an expression"
at the same three doors.

**Which eight was measured, one keyword at a time, and the guess was wrong
twice.** A review had named seven, including `inline`, which is *not*
implemented; it missed `catch`, `mutable` and `using`, which are. The check is a
one-line program per keyword in a place the keyword belongs, and it is what to
run before moving a name between the two lists - a keyword in the wrong list is
a diagnostic that lies in one direction or the other.

## Rung 5 reopened: a real tuple, 2026-08-29

Asked whether this compiler supports tuples. There is no `std::tuple` and
there never will be here - `lib/` is the C headers and there is no C++
standard library at all - so the question worth answering was whether the
*language* can express one. A fixed-arity `Pair` already worked. The
recursive variadic shape the real one is built from did not, and finding out
why turned up **nine** things rather than the two it looked like.

```cpp
template <class... Ts> struct Tuple;
template <> struct Tuple<> { };
template <class T, class... Rest>
struct Tuple<T, Rest...> : Tuple<Rest...> {
    T head;
    Tuple<Rest...> *tail() { return this; }
};
```

**Three were template gaps.**

*A partial specialization that peels a pack.* `template <class... Ts> struct
L;` has one parameter and `L<T, R...>` gives it two, because a pack stands for
a list rather than a type - so checking written arguments against parameter
count rejected every recursive variadic class. What says where the pattern
stops is the closing angle. Matching is the other half: the arguments arrive
as *one* pack, so `choosePartial` flattens them and matches the fixed
arguments in front, with a trailing `R...` taking the rest - possibly nothing.

*A pack expanded into another template's argument list*, `Tuple<Rest...>`,
which was refused by name. It works where the members are known, because a
pack argument is read last and takes everything left, so an expansion splices
its members in where it stands. **It is still refused in a pattern**, where
the pack stands for itself and what would be spliced is the parameter - that
is deduction, and a different problem with the same syntax.

*A dependent base*, `: Tuple<Rest...>`. The base clause read a bare identifier
and looked it up as a typedef, which a template-id can never be. Reading the
base as a *type* reaches the code that already knows how to instantiate one,
and costs nothing inside a template because a pattern is never parsed.

**Three more were consequences of those, found only by running it.**

A partial specialization may have a **base clause**, so `bodyAt` points at `:`
as well as `{`. A variadic template is very often **declared and never
defined**, every definition being a specialization - so "nothing to
instantiate" has to be asked *after* a partial is chosen. And a partial's pack
has to be recorded in its `Specialization`, because the held member bodies are
replayed later from that record: left out, every level replayed with an empty
pack and `Tuple<char, long>::tail` was declared to return `Tuple<long> &`
while its body said `Tuple<> &`.

**Three were nothing to do with templates, and two of those were silent.**

`int &get() { return x; }` was reported as a *reference data member*. At the
point that check is made the `(` has not been read, so the type in hand is
still the return type - the same shape as the `constexpr` function test in
7.5a, and the same fix.

**The empty base optimisation was missing.** An empty class has `sizeof` 1 -
so two objects of it differ in address - and a *data size* of 0, and the two
are different numbers on purpose. `dataSize()` answered 1, so `struct D : E {
int x; };` was 8 bytes where clang and cl both say 4, and **every class with
an empty base had a layout that agreed with no other compiler**. All three
users of `dataSize()` are asking "how far into an object does this base's data
reach", and 0 is the right answer to that for all of them. A recursive
variadic class feels it hardest: it bottoms out in an empty specialization and
pays at every level.

**A derived member did not hide a base's.** A base's members are copied into
the derived class's list at their own offsets - that flattening *is* the
layout - so the list runs most-base to most-derived and `findMember` has to
read it **backwards**. Forwards, every one of `Tuple`'s three `head` members
resolved to the innermost: wrong values, right types, no diagnostic.

**Still refused, and each says so by name:** binding a *reference* to a base
subobject (`Tuple<Rest...> &tail() { return *this; }` - the pointer form
works), and a template-id as a qualified name in an expression
(`Tuple<Rest...>::depth()`).

**A test-case rule this cost a Windows round to learn: a case that prints
`sizeof` may not contain `long`.** It is 8 bytes on the two Itanium targets
and 4 on Windows, so such a case measures the data model rather than the
layout and no single `.expected` can serve all three. `double` is 8
everywhere. All three failures on the Windows box were this and none was the
layout work.

## Where this came from

Forked from `../Compiler-C` on 2026-08-26, at the whole-tree level rather than
just `src/backend/`. The reason is mechanical: `src/backend/Backend.h` is
written against `src/Ast.h` and `src/Type.h`, so taking the backend means
taking the AST, and once the AST comes across the Lexer, Preprocessor and
Driver are free. What was bought is three working code generators —
`x86_64-linux`, `x86_64-windows` (MASM) and `arm64-darwin` — plus DWARF, the
`Abi`/`Spelling`/target split, and toolchain discovery. **The work goes into
the front end.**

The fork is a copy, not a git remote: the history in Compiler-C is about C.
Fixes do not flow between the trees automatically, and divergence in
`src/backend/` is expected rather than accidental — C++ needs mangling,
vtables, exception tables and `__cxa_guard`, none of which C may ever grow.

`tests/c-corpus/` is Compiler-C's 425 cases, inherited and **untriaged**. Much
of it is valid C++11 and much is not (implicit `void *` conversions, `new` as
an identifier). Do not report a number from it as a pass rate until it has
been triaged case by case.

A different 43 started compiling when `wchar_t` became a keyword type rather
than a typedef. `<stddef.h>` had been declaring `typedef int wchar_t;` since
rung 1 made `wchar_t` a keyword, so **the whole of `lib/` had been unusable
and nothing noticed** - no case in the suite included a header, and the corpus
cases that do were already failing for their own reasons. `wchar_t` is a type
of its own in C++ and is spelled in the parser now.

43 of those cases stopped compiling when `const` entered the type system, and
all 43 are the same C idiom: `int printf(char *fmt, ...);` handed a string
literal, which is an array of *const* char in C++11. clang refuses all 43 as
well. **Ask clang about a corpus case with
`clang++ -x c++ -std=c++11 -pedantic-errors`, and nothing less.** Without
`-x c++` it compiles a `.c` file as C and agrees with everything; without
`-pedantic-errors` the string-literal rule is only a warning, kept that way
for old C++. Both defaults quietly turn the oracle into a rubber stamp.

A case in `tests/cases/` can be handed straight to
`clang++ -x c++ -std=c++11` now, because the cases declare what they take from
the C library inside `extern "C"` - which is what a C++ program has to do, and
what the earlier sed-a-copy stopgap was standing in for. Every `.expected` in
the suite is clang's own output.

## What makes C++ different from C to parse

Two facts shape the whole front end, and a design that ignores either has to be
redone:

**Parsing needs the symbol table.** `T * x;` is a declaration or a
multiplication depending on how `T` was declared. There is no clean
lex → parse → check pipeline; lookup runs during parsing. cc1's parser already
tracks typedefs for exactly this reason, which is the seam the C++ work grows
from.

**Two-phase name lookup was a day-one decision, and it has been taken the
other way.** This file used to say the parser must distinguish dependent from
non-dependent names before templates parse, because getting it wrong is the
classic rewrite. Rungs 1 to 4 shipped without doing so, and rung 5 is planned
around instantiating a template by **replaying its tokens** rather than by
building a dependent AST - which means every name in a template body is looked
up at instantiation. The reasoning, and what it costs, is in the rung-5 section
below; it is a decision rather than a drift, and it is the one to revisit first
if templates ever feel wrong.

## The ladder

Each rung is a language someone could write a real program in, and each one
compiles, assembles, links and runs on all three targets before the next
starts. No half-built pipelines waiting on a later phase.

| | Rung | State |
| --- | --- | --- |
| 0 | The fork: C90 through three backends | **done**, 2026-08-26 |
| 1 | C++ as a better C: keywords, `bool`, tag names, `::` | **done**, 2026-08-26 |
| 2 | References, overloading, **Itanium/MSVC mangling**, `new`/`delete` | **done**, 2026-08-28 |
| 3 | `class`: members, access, ctors/dtors, `this`, RAII | **done**, 2026-08-28 |
| 4 | Inheritance → virtual functions and vtables → multiple inheritance | **done**, 2026-08-29 |
| 5 | Templates: function → class → deduction → partial spec → SFINAE → variadic | **done**, 2026-08-29 |
| 6 | Exceptions: `__cxa_*`, `.gcc_except_table`, unwind data | **done** on all three targets, 2026-08-30 - `return` inside a `catch` is refused on Windows |
| 7 | The C++11 layer: `auto`, `decltype`, move, lambdas, `constexpr`, range-for | **done**, 2026-08-30 - every C++11 capture included |

## Where this stands, 2026-09-01

**The ladder is walked.** All eight rungs compile, assemble, link and run on
x86_64-linux, x86_64-windows and arm64-darwin. Suites at the last commit:

| | run | emit | names vs clang | overload | names vs cl |
| --- | --- | --- | --- | --- | --- |
| Mac | 223 | 348 | 117 | 26 | - |
| Linux | 223 | 348 | - | - | - |
| Windows | 220 | - | - | - | 93 |

**Two of the four suites only ever run on one machine.** `names.sh` and
`overload.sh` both ask clang, and the Linux box has no clang++ - they skip
themselves there and say so. So the oracle half of three-box verification is
the Mac's, with the Windows box asking cl separately. Worth knowing before
reading a green Linux run as agreement with anything.

Since the ladder finished, the work has been **C++11 features that were never
rungs** - each one measured against clang, and against cl for a Microsoft-ABI
question, before anything was written:

| Feature | Landed | What it turned on |
| --- | --- | --- |
| `namespace`, `using namespace` | 2026-08-30 | scopes in a tag, and lookup as a search |
| `nullptr` | 2026-08-30 | a null constant that is not the integer 0 |
| `static_assert` | 2026-08-30 | a declaration that declares nothing |
| `explicit` | 2026-08-30 | copy-initialization, in its three places |
| `const_cast`, `reinterpret_cast` | 2026-08-30 | the const line between the two |
| `noexcept` | 2026-08-30 | the specifier and the operator |

Each has a section further down saying what was measured and what was
deliberately not built.

### What to do next, and why in this order

**The two try/catch limits come first**, and they were promoted from footnotes
to a prerequisite by `noexcept`: a local with a destructor and a `try` in one
function is refused, and on x86_64-windows a handler is a funclet that cannot
`return`. Both are recorded under rung 6 as their own gaps. They now also block
enforcing a `noexcept` specification at run time - which is the one entry in
`docs/CONFORMANCE.md` whose fix is otherwise small - so lifting them buys two
things rather than one.

Then, roughly by what else depends on it:

* **Conversion functions** (`operator int()`, `explicit operator bool()`).
  There are none at all, which is why `explicit` on one is refused naming the
  conversion function rather than the keyword.
* **A mem-initialiser calling a class member's constructor with an argument** -
  `H() : t(3) {}` where `t` is a class. Refused today whether or not anything
  is `explicit`.
* **Parenthesised direct-init of a scalar**, `int n(5);`. Small, and it is what
  makes `bool b(nullptr);` unreachable - the one measured `nullptr` case that
  cannot be written here.
* **`T(3);` as a bare statement**, and **a null pointer to member function**,
  which has no value by any spelling because it is two words on both ABIs.

**`dynamic_cast` is a rung of its own, not a fourth branch beside the other
casts**: it needs a `type_info` beside each vtable, the inheritance graph it
carries, and the `__cxa_` call that walks it. None of that is emitted today.

Rung 1 in full: the C++11 keyword table, the `::`, `.*` and `->*`
punctuators, `__cplusplus`, `bool`/`true`/`false`, class and enum tags as type
names, and character literals typed `char` rather than `int`. Mixed
declarations and `for`-init scope came across from Compiler-C already working
and now have cases holding them there. `auto` as a deduced type is refused by
name and belongs to rung 7.

**Rung 2 is done in dependency order, and `const` comes first.** The type
system inherited from Compiler-C had no qualifier at all: constness was a bool
on the declared object, and a pointee's const was discarded, so `const char *`
and `char *` were the same `Type`. Nothing else in the rung can be built on
that. A mangler cannot spell `PKc` for a type that does not exist, and
matching clang's mangled names is the whole reason for having the oracle;
overload resolution cannot rank `f(char *)` against `f(const char *)` if they
are one function. So the order is: `const` in the type system, then
references, then `extern "C"` and mangling, then overloading, then
`new`/`delete`. All five are done, and the order was the right one twice over:
overload resolution could not have ranked `f(char *)` against
`f(const char *)` before `const` was a property of the type, and the two
overloads could not have reached the linker as different functions before the
mangler existed.

**`new` and `delete` call the platform's four operator functions by name, and
those names were measured rather than read** - `clang++ -target ... -S -O0`
over a file that allocates, for each target. At `-O1` clang elides the
allocation, which it is allowed to do, and the assembly comes back with
nothing in it to read.

    operator new(size_t)      _Znwm    ??2@YAPEAX_K@Z
    operator new[](size_t)    _Znam    ??_U@YAPEAX_K@Z
    operator delete(void *)   _ZdlPv   ??3@YAXPEAX@Z
    operator delete[](void *) _ZdaPv   ??_V@YAXPEAX@Z

Calling the platform's operators rather than shipping an allocator is what
makes a `new` here and a `delete` in an object built by clang the same
allocation. It also decided the link line: **the driver links with `c++`, not
`cc`**, because those four live in libc++ or libstdc++ and the C driver links
neither - `new int` compiled and assembled and then failed with four undefined
symbols the linker had helpfully demangled. On Windows they are already in
`libcmt.lib`, which was measured on the box with `dumpbin /linkermember`
rather than assumed.

Refused by name rather than half-built: placement new and a parenthesised
type-id after `new` (both need rung 3 to be worth having, and they read the
same way to the parser, so one message says how to write the other), more than
one value in a new-expression, `new T[n][m]`, `new T[n](...)`, and `delete` of
a `void *`. `docs/CONFORMANCE.md` records the one thing that compiles and is
not the standard's answer: a failed allocation terminates rather than throwing,
because the platform's operator throws and there is no handler until rung 6.

**The linkage name is computed in the parser and carried beside the source
name.** `Var`, `Call` and `Function` each answer `name()` for people and
`symbol()` for the linker, and the backends emit the second - including in
label prefixes, because two overloads will share a name and must not share a
label. `src/Mangle.cpp` holds both ABIs: Itanium on Linux and Darwin,
Microsoft on Windows, chosen by `Target::microsoftNames()`.

What is *not* mangled: anything inside `extern "C"`, `main`, and a variable
with internal linkage. What is: every other function, and on Windows every
externally-visible variable. Internal linkage says so in an Itanium name -
`_ZL5twicei` - and does not in a Microsoft one.

**Every rule in the mangler was measured, not read.** clang will spell either
ABI on any machine:

```
clang++ -target x86_64-linux-gnu       -S     # Itanium
clang++ -target x86_64-pc-windows-msvc -S     # Microsoft
```

`tools/mangled-names case.cpp` puts cxx1's names beside clang's for all three
targets, and `tests/names.sh` runs it over every case in the suite - the third
suite, and the one that says the platform ABI was conformed to rather than
approximated. It skips itself where there is no clang, saying so.

Two things that surprised: MSVC numbers are the value minus one as a digit up
to ten and the value itself in hexadecimal above that, and a const array
element is written `$$CB` rather than by qualifying the array. Both are in the
comments where they are used.

**A reference is lowered to a pointer, and the parser is where it stops.**
`Kind::LValueRef` exists in the type system, so a function's type can record
that a parameter is `int &` - overload resolution and the mangler will both
need that - but no backend ever sees one. `Parser::useReference` turns every
mention of a reference into a dereference of the slot holding its address, and
from that point on assignment, address-of, member access and subscripting see
an ordinary lvalue. `Parser::bindReference` is the other half: binding is
taking an address, and where there is no address to take - a value, a
bit-field, a `register` object, a converted type - a const reference gets a
temporary in the frame and everything else is refused by name.

The one asymmetry to remember: `sizeof` a reference asks about what it refers
to, while the *frame slot* is a pointer. `Type::size` answers the first and
`allocateFrameSlot` the second, and those are the only two places that differ.

Refused by name rather than half-built: rvalue references (`&&`, rung 7), a
reference member of a class (it needs a constructor, rung 3), a reference at
file scope (it needs binding before `main`), a reference to a reference, an
array of references, a pointer to a reference, and a cv-qualified reference.
Also `?:` as an lvalue, which is real C++ and would let `return c ? a : b;`
bind a reference - it is a Conditional here and Conditional is not an lvalue.

**`const` is a property of the type, and a qualified type is a second `Type`
that forwards.** `TypeTable::withConst` interns a copy carrying `unqual_`, and
everything that depends on state a struct gains *later* - its members, its
size, whether it is complete - is asked of the unqualified one rather than of
the copy, which was taken before the struct was finished. The trap to know
about: every interning loop in `Type.cpp` must skip qualified types. A
`char * const` is a `Kind::Pointer` whose pointee is `char`, so a `pointerTo()`
that did not skip it would hand it back for `char *` and quietly make every
such pointer in the file read-only.

**Not in rung 1, and deliberately**: a declaration in an `if` or `while`
condition. It needs the condition's scope to wrap both branches, which is a
change to how `If` is built rather than an addition to it. **Landed
2026-09-04**, and that description held - see "A declaration in a condition,
and the two rules that are not one rule".

**Rung 3 is done in dependency order too, and `class` with access control
comes first**, for the same reason `const` did: the member table has to carry
who may name a member before anything can decide whether a use is allowed. A
class and a struct build the same type through the same function and differ in
one thing - where access starts, private or public - which is what
[class.access] says they differ in. `protected` is recorded distinctly even
though nothing can yet tell it from `private`, because inheritance is what
tells them apart and revisiting every declaration then would be the more
expensive order.

**There is no "inside" yet**, so from anywhere a non-public member is out of
reach. That is not a half-check: a class with private data and no member
functions is closed, and member functions are the next step. `Type` records
which keyword defined it so a diagnostic says "class Account" about something
written as a class - only a *definition* sets that, since `class X;` followed
by `struct X { }` is one type written two ways and the standard allows the mix.

**Member functions are free functions with one extra leading pointer**, and
that is why the backends needed nothing: `this` is parameter zero and every
backend already knew how to pass a pointer. It is bound before any written
parameter so it takes the first slot, and it is deliberately not in the
signature's `params` - that vector is the declared type, which is what
overload resolution ranks and the mangler spells, and `this` is in neither.

A member is keyed in the one function table as `"Point::get"`, which is what
gave members overload resolution with no second implementation of it: two
members with different parameters are two entries under that key, exactly as
two free functions would be.

**Both ABIs spell the class into the name, and only Microsoft spells in the
access** - Q public, I protected, A private, measured. So a member that changes
from private to public changes its symbol on Windows and keeps it on Linux. A
const member function is `_ZNK...` and `...QEBA...`, and const is part of which
member it is: `get()` and `get() const` are two functions.

**An empty class became legal here with this step**, and that was not a loose
end: a class holding only member functions has no data members, and C's rule
that a struct needs one would have refused the ordinary shape of a class that
carries behaviour and no state. Size is one byte so two objects have different
addresses.

Refused by name rather than half-built: a member function **defined inside**
the class body, because the body would have to see members declared after it -
which means holding it until the class is closed, and that is a change to when
parsing happens rather than an addition to it; a nested class; a member
function of a union; and a member function declared out of line that the class
never declared.

**A constructor is a member function whose name is its class and whose return
type is nothing**, so it is keyed under `"Point::Point"` and every piece of the
overload machinery applies unchanged - `Point()` and `Point(int,int)` are two
entries a construction chooses between. The object exists before the call, as
a frame slot like any other local; the constructor gives its members values.

Two things had to be taught rather than reused. `Point::Point(...)` has **no
type before a name that IS a type**, so `specifiers()` declines it - answers
void and consumes nothing - which leaves the declarator's qualified-name path
to read `Point::Point` exactly as it reads `Point::get`. And `Point p(1)` and a
function declaration look the same until the type is known to be a class with
constructors, so that question is asked before the block-function branch.

**Itanium gives a constructor two names and both are emitted**: C1 for a
complete object, C2 for a base subobject. A construction calls C1 - measured,
by reading which one clang's call names - and nothing calls C2 until a derived
class does, but an object file missing it is not the object file clang
produces. `Function::alias` carries the second name and the two Itanium
backends emit it as a label in front of the body, which is one address with two
names rather than two copies of the code. The Microsoft ABI has one name, ??0,
and writes '@' where a member function writes its return type.

Refused by name: a constructor defined inside the class body, a variadic
constructor, a static local with a constructor (it would have to run before
main), copy-initialisation `Point p = ...` (it needs a copy constructor), and
`Point p();` - which declares a function, and a compiler that quietly built an
object there would compile a program that means something else everywhere else.

**RAII is one list read backwards at the right moments.** `alive_` holds what
has been constructed and not yet destroyed, innermost last; a block destroys
what it added, a `return` destroys everything the function still owes, and
`delete` destroys before it frees - the order measured from clang, which calls
D1 and then `_ZdlPv`.

**A return computes its value before running destructors**, into a slot of its
own. That is not a detail: the expression may read an object that is about to
be destroyed, and without the temporary the function would return a value taken
out of an object after its destructor had been told it was finished.

**A jump out of a scope destroys what the scope built, innermost first**,
before it goes - [stmt.jump]/2, and the same calls the scope's end makes,
through the same `destroyObject`. `return` has always done it for the whole
function. `break` and `continue` do it for everything built since their loop
or switch body was entered - a mark on `alive_` taken after a `for`'s
init-statement, so `for (S s; ...)` keeps its `s` across a break. A `goto`
cannot know at the time what it leaves, since a forward label has not been
read, so it is emitted as a block in front of the jump, left empty, and
`resolveGotos()` fills it with the calls for every object alive at the goto
and not at the label. Measured against clang over ten shapes in
`tests/cases/jump-out-destroys` - two scopes at once, a loop body, a
backward jump, a switch, an inner loop only, several objects in one scope,
an object destroyed only through a member's destructor - with identical
output.

Before this, `break` and `continue` were refused whenever anything at all
was alive, and `goto` was not refused at all: its half of that test sat on a
line the goto branch had already returned from, and `{ S s; goto out; }`
compiled and never ran `~S`. Found 2026-09-02 while mending the rule below;
`tests/cases/goto-out-of-scope` runs it now.

**A jump may not land past an initialisation** - [stmt.dcl]/3, for `goto`
forward, `goto` backward into a block, and a `switch` to its case labels
alike. Each label, goto and switch records which initialised automatic
objects are in scope where it stands - an initialiser, a constructor or a
destructor makes an object count, and an uninitialised scalar, a POD without
an initialiser or a static does not - and a jump whose label holds one its
origin does not is refused at the origin. Before the rule `goto done; S s;
done:` ran `~S` on an object never built, and `goto done; int x = 5; done:
return x;` returned whatever the slot held. Twenty-two shapes measured
against clang with `-x c++ -std=c++11 -pedantic-errors`, all agreeing.

**A braced initialiser does not narrow** - [dcl.init.list]/7, in the four
forms the paragraph lists, on a local, at file scope, and for each member of
an aggregate. A constant source is judged by its value through `fold` and
`foldDouble`, the evaluators every other constant context uses; a
non-constant one by its type alone. `char c = {300}` gave 44 without a word
until it did. Fifty shapes measured against clang, all agreeing - the oracle
needs `-x c++` and `-pedantic-errors`, or it compiles a `.c` as C and keeps
the rule as a warning respectively.

**`tools/mangled-names` asks clang with `-fno-exceptions` now**, and that
change is about the comparison rather than the code: a class with a destructor
makes clang emit a cleanup path - the personality routine, `.gcc_except_table`,
`_Unwind_Resume` - and this compiler has no exceptions until rung 6, so those
symbols read as a disagreement about names when they are a difference in
features. Mangling is unchanged by the flag.

**Multiple inheritance is where "the base is at offset 0" stops being true**,
and every step before it leaned on that. `class C : public A, public B` puts A
at 0 and B at 4 - measured - so `B *pb = &c` is `&c + 4`, and the same four is
what B's member functions expect as `this`. `Type::bases()` carries each base
with its offset; `convert()` moves the pointer, **and keeps a null pointer
null**, because [conv.ptr] says so and `(char *)0 + 4` is not null.

Bases are constructed in the order written and destroyed in reverse. Both
loops walk the list **backwards**, because a constructor's call is prepended
to the body and a destructor's is appended - prepending A last is what leaves
it first. Walking forwards for the constructor put B before A, which the
oracle caught immediately.

**A polymorphic second base needs a secondary vtable and a thunk, and Itanium
has both now.** `_ZTV1C` holds two tables back to back: the primary for A, then
a secondary for B whose first word is **-16**, saying how far back the complete
object is. The object carries two vptrs, at 0 and at B's offset.

A call through a `B *` arrives with `this` pointing at the B subobject, and an
override expects the whole object - so the secondary slot holds a **thunk**
that walks `this` back and calls the real function. clang tail-jumps; cxx1
calls and returns, which costs a frame and behaves identically and needed
nothing new from any backend. The name is `_ZThn16_N1C1gEv`: the prefix, the
offset, then the mangled name with its own `_Z` removed and its `N` kept.

**The Microsoft ABI does not use a thunk at all, and that is a difference in
code generation rather than in naming.** Measured with **cl itself**, from its
own `/FAs` listing:

- Two separate vftable symbols, `??_7C@@6BA@@@` and `??_7C@@6BB@@@`, rather
  than one table in two sections. Each is stored at its own base's offset.
- The second points **straight at `?g@C@@UEAAHXZ`**. There is no adjustor
  symbol anywhere in the object.

The adjustment is inside the override. For `int C::g() { return c*100 + b*10 + a; }`
cl emits

    imul eax, DWORD PTR [rax+16], 100     ; c
    imul ecx, DWORD PTR [rcx+8], 10       ; b
    add  eax, DWORD PTR [rcx-8]           ; a  <- NEGATIVE

so **`this` arrives pointing at the B subobject**, and the first base's members
are reached backwards from it. MSVC compiles the whole function against a
biased `this`; Itanium compiles it against the complete object and puts a thunk
in front.

That is why this is refused on Windows rather than approximated: implementing
it means carrying a `this` bias through member lookup for the whole body of
such an override, not emitting a different symbol. It is a rung-4 remainder
with a known shape, and the shape is written down here so the next attempt
starts from a measurement rather than from a guess.

**A case may now name a target it does not compile for**, one per line in
`<case>.notarget` with the reason on the line, honoured by `tests/emit.sh` and
`tools/mangled-names`. The reason is printed on every run: an exclusion nobody
sees is an exclusion nobody removes.

**Rung 4 opened with single, non-virtual inheritance**, and the base subobject
sits at **offset 0** - measured. So a derived object's address is its base's
address, no pointer adjustment happens anywhere, and passing `this` to a base's
member function is a change of type and nothing else. Multiple inheritance is
what ends that, which is why it is a later step and is refused by name until
then rather than laid out wrongly.

**Data members are copied down and member functions are searched up**, and the
asymmetry is deliberate: a member lives at an offset and can be copied, a
function lives under a name and cannot be without inventing a second symbol for
it. Access travels through the inheritance - a public member of a private base
is private in the derived class.

**A constructor runs the base's first and a destructor runs it last**, calling
the base's **C2 and D2** - the base-object forms. That is the first thing to
call them, and the reason they have been emitted since constructors landed. On
Windows there is one name for each and it is called directly.

Naming a base's constructor needs a mem-initializer list, which does not exist
yet, so a base with no default constructor is refused by name.

**Virtual functions land in two slices: the table, then dispatch.** The first
emits the vtable and sets the vptr and **refuses a call to a virtual function
by name**. A static call would be right whenever the static type happened to
be the dynamic one and silently wrong otherwise, which is the outcome this
project refuses loudest.

A vtable holds **one pointer per virtual function, in the order the base first
declared them**, an override replacing an entry rather than appending - which
is what lets a `Base *` and a `Derived *` agree on where to look. It is emitted
as an ordinary global whose initialiser pieces are symbol addresses, so no
backend was told about vtables at all.

**Finding a base's slot is what makes a function virtual**, not the keyword.
[class.virtual] says an override is virtual whether or not `virtual` is
written again, and a destructor is virtual as soon as a base's is. cxx1 used
to require the keyword, and a class that left it out was dispatched
*statically* - it compiled, ran, and quietly gave the base's answer, which is
the one outcome this project refuses loudest. So the slot search runs *before*
the linkage name is built rather than after: Microsoft spells a virtual member
U and a plain one Q, and getting the dispatch wrong had been getting the name
wrong too. The destructor half is worse than the function half - `delete`
through a `Base *` ran `~Base` alone and the derived class's own cleanup never
happened.

The two ABIs differ in the header, and so in what the vptr holds. Itanium
writes offset-to-top and a typeinfo pointer first and the vptr points at
**table + 16** - measured from the `addq $16` in clang's own constructor. The
typeinfo slot is a plain 0: there is no RTTI here and `typeid` is refused by
name, which is also why `tools/mangled-names` now asks clang with `-fno-rtti`
as well as `-fno-exceptions`. Microsoft has no header, so the vptr is the
table's own address, and it spells a virtual member **U** where a non-virtual
public one is Q.

**A base subobject occupies its data size, not its sizeof**, and that bug was
found here rather than reasoned about: a derived class may put its first
member in the base's tail padding. `Base {vptr, int}` ends at twelve and pads
to sixteen, so `Derived`'s int lands at twelve and the whole is sixteen, where
laying it out after `sizeof` gave twenty-four. `Type::dataSize` is the
distinction; it equals `size()` for anything without tail padding, which is why
single inheritance passed without it.

A polymorphic class with no constructor is refused: nothing else sets the vptr,
and an implicit default constructor is not written yet.

**Dispatch reads the slot rather than naming the function**: the object's first
word is the vptr, the slot is at a fixed index - the same index in every class
in the chain, which is what the table's ordering bought - and from there it is
an ordinary indirect call, the machinery a call through a function pointer
already used. It needs `Derived *` to convert to `Base *`, which costs nothing
at run time because the base is at offset 0, and ranks as a pointer conversion
so `f(Derived *)` still beats `f(Base *)` for a `Derived *`.

**Two traps in building AST by hand, both found by running.** A global `Var` is
an lvalue, so naming the vtable *loaded* its first word instead of taking its
address - the array type and `decay` are what yield an address. And a `Binary`
built by hand is not the parser's pointer arithmetic: `+ 2` added two bytes
rather than two entries, so the vptr pointed two bytes into the table's first
word. Both places compute bytes explicitly now and say why.

**A pure virtual holds a slot it has no function for**, and what goes in it is
the runtime's own trap — `__cxa_pure_virtual` on Itanium, `_purecall` on
Microsoft, both measured. The vtable is still emitted and the constructor still
stores it, because an abstract class is built as a base subobject every time a
derived one is.

**`= 0` is a specifier and not an initialiser**, so it is read where the
exception specification is read rather than anywhere a value would be. The
function may still have a body — C++ allows one and a derived class can call it
explicitly — so the *symbol* is unchanged and only the table entry differs.

**Abstract is a question about the finished table, not about what the class
declared.** A derived class that overrides every pure entry has replaced them
and is concrete; one that leaves any is abstract in its turn, even though it
declared no pure virtual of its own. `VSlot::pure` carries it and an override
clears it, which is the same machinery that already replaced the symbol.

**The refusal is where an object would be made, not where the call would
happen** — a local, a member, a `new` — because by the time the call is reached
there is nothing left to say about it. It names the function that has no
implementation, which is the thing the reader has to write.

Worth knowing about how this was found: pure virtuals were missing and
`docs/EXCLUSIONS.md` did **not** list them, because the refusal was a bare
`expected ';'` rather than a message naming the feature. That is the same
defect the exclusions document exists to catch, one level down, and it is why
the rule there is that a refusal says "is not supported yet" or names a
standard version.

**A virtual destructor puts a function in the vtable that no program writes.**
`delete p` through a base pointer has to reach the right destructor and then
free, so one slot does both and holds a *deleting* destructor. cxx1
synthesizes it - there is no source for it - and emits it as ordinary AST, so
no backend knows it was invented.

The ABIs diverge here more than anywhere else, both measured. Itanium takes
**two adjacent slots**, D1 for the complete object and D0 for the deleting
form, and `delete` calls the second. Microsoft takes **one**, `??_G`, which
takes a flag beside `this`, frees only when its low bit is set - so a non-heap
object can reach the same slot safely - and returns `this`. cl spells it
`??_GBase@@UEAAPEAXI@Z` and so does cxx1.

`delete[]` of a polymorphic type is refused by name: it needs the element count
and the dynamic type, and neither is recorded.

**T union, U struct, V class** in a Microsoft name. Until vtables nothing
declared with `class` had its type reach a name, so U covered both; clang and
cl both write `?viaPointer@@YAHPEAVShape@@@Z` where cxx1 wrote `...PEAUShape...`.
Itanium spells all three by tag and does not care.

**A member function defined inside its class is held, not parsed where it is
met.** Its body may name members declared below it, so nothing in it can be
read until the class is closed. The class body records the token the whole
declaration starts at, steps over the braces, and `replayInlineBodies` re-reads
those tokens once the class is complete - through the ordinary out-of-line
definition path, so constructors, `this`, init lists, destructors and virtuals
needed no second implementation. `inlineOwner_` supplies the `Class::` the
source does not have and is **one-shot**, cleared by the first declarator that
uses it, so declarations inside the body stay ordinary locals.

**The replay begins after `virtual`, not at it.** Out of line the keyword is
not written - C++ puts it on the declaration inside the class and nowhere else
- so the recorded token has to be the one after it, or the ordinary path is
handed a `virtual` that `specifiers()` will not read. It was, and every
virtual function written inside its class was a parse error until this was
fixed: `error: expected a type`, pointing at the keyword.

**The three special members a class does not write, the compiler does** -
default constructor, copy constructor, copy assignment - and the first thing
to know is that **a trivial one is not a function at all**. That is measured,
with cl first: for a class with no virtual function and no member that needs
building, cl emits no symbol for any of the three, and clang emits none on
either Itanium target either. So a plain class is copied by moving its bytes -
the struct assignment this compiler has emitted since it was a C compiler -
and no implicit member is declared for it, which is what keeps the symbol
lists level with the oracles' and leaves the old C path untouched.

An implicit member is declared where it has *work* to do: a virtual function,
whose vptr somebody has to store, or a base or member with a special member of
its own to run. And it is given a body only when something calls it, which is
[class.copy]'s own rule about implicit *definition* and also what cl does -
`??0Poly@@QEAA@XZ` appears because a `Poly` is built, not because `Poly` is
declared. `defineImplicitFunctions` runs at the end of the file and to a fixed
point, since giving one class a body can be what first calls another's.

**A copy constructor stores the vptr and a copy assignment does not**, which
is the only difference between the two bodies and was read out of cl's own
listing:

    ??0Poly@@QEAA@AEBU0@@Z   lea rcx, OFFSET FLAT:??_7Poly@@6B@   <- its own
                             mov QWORD PTR [rax], rcx
                             mov ecx, DWORD PTR [rcx+8]           <- then members
    ??4Poly@@QEAAAEAU0@AEBU0@@Z
                             mov ecx, DWORD PTR [rcx+8]           <- members only

The reason is that a constructor is *making* an object, so the new object is of
this class whatever the source was, while assignment writes into one that is
already of this class. A polymorphic class is non-trivial for *both*, even
though only one of them touches the vptr.

**Members are copied one at a time and not as one block.** A base subobject
occupies its data size rather than its sizeof, so a derived class may have put
a member of its own in this class's tail padding - and a `sizeof`-sized copy
through the C2 form would take that member with it. Where a base has a copy of
its own, its call is emitted and the members it covers are skipped: they are in
this class's member list too, because data members are copied down.

**An array member is copied by a loop**, `int i = 0; while (i < n) { ... }`,
built as ordinary AST. N statements would have been simpler and is wrong: the
count is a property of the type and nothing bounds it. The element addresses
are computed in *bytes*, index times element size - a Binary built by hand gets
none of the parser's pointer scaling, the same trap the vptr store hit.

**`operator=` is the one operator this compiler spells**, and both ABIs were
measured: Itanium writes the two-letter code `aS` where a member function
writes the length and letters of its name, and no return type -
`_ZN4PolyaSERKS_`. Microsoft replaces the whole `?name@` with `??4` and, unlike
`??0`, *does* write the return type - `??4Poly@@QEAAAEAU0@AEBU0@@Z`. Only the
class is pushed as a name there, so it is back-reference 0 where a named member
function would leave it 1. The rest of the operators arrive with operator
overloading; until then `operator` is refused by name.

**Copy-initialisation came with the copy constructor** and is not a separate
feature: `X b = a;` is a constructor called with one argument, chosen by the
ordinary overload rules, so `Conv c = 5;` picks a converting constructor by the
same road. What the standard adds here is that an `explicit` constructor may
not be picked, and `explicit` is refused by name until rung 7.

`X q(p);` for a class with no constructor at all is the lowering trade again:
there is no constructor to call, so it becomes the assignment the backends
already emit. A parameter list begins with a type name and this does not, which
is what tells the two apart - the same question the constructor path asks, from
the other side.

Refused by name rather than half-built: a base or member whose default
constructor takes arguments, where the implicit one has no way to name them -
the message says to write a constructor with an initialiser list; and a base or
member whose special member is private or protected. A class with a `const`
member simply has no implicit copy assignment declared, which is [class.copy]'s
deleted one arriving as "there is no such function" rather than as a wrong one.

**Passing a class by value is a copy constructor call, and that changes how it
is passed.** Measured with cl first and confirmed on both Itanium targets: a
class with a **non-trivial copy constructor** goes **by address whatever its
size**, where a trivially copyable class of the same size goes in a register.
The two halves are one change - making the copy happen means the callee has to
be handed the address of storage the caller owns - and neither is right
without the other.

`Type::nonTrivialCopy()` is where the two sides agree. It is set when the
class is completed, exactly when a copy constructor exists for it - written or
just implicitly declared - and it is read by `returnsIndirectly` in the parser
and by the two code generators, so nothing has to re-derive it.

**Both sides are lowered in the parser, so the backends needed almost
nothing.** The parameter becomes a **reference**: its frame slot holds the
caller's pointer and every mention of it dereferences, which is the machinery
references already had. The argument becomes `(ctor(&tmp, arg), &tmp)` - one
expression, so it works wherever a call does. What the backends see on both
sides is a pointer. The only thing they were told is that such a class is
returned through a hidden pointer whatever its size.

**The caller makes the copy and the caller destroys it** - Itanium's rule,
and clang emits the destructor of the argument temporary at the call site.
**cl puts that on the callee**, measured: `?useE@@YAHUE@@@Z` destroys its own
parameter. cxx1 follows Itanium on all three targets; `docs/CONFORMANCE.md`
records what that costs, which is nothing until a cxx1 object is linked with a
cl object.

**A temporary dies at the end of the full expression, not when the call it was
made for returns**, and that is visible rather than pedantic:
`printf("%d", takeNoisy(d))` destroys the copy *after* the printf. So the
temporaries are collected in `pendingTemps_` and `endFullExpression` empties
the list at the places an expression becomes a statement or a condition. A site
that forgets to call it does not lose the destructor - the temporary stays on
the list and goes at the next full expression - so the failure mode is late
rather than absent.

**Copy elision is permitted rather than required in C++11, and the two oracles
take different options**: clang elides at `-O0`, cl does not at `/O0`. cxx1
elides in the two places worth having it - a declaration initialised by a call
that already returns the class through a hidden pointer, and such a call passed
straight in as an argument. In both, the callee builds its result where the
object had to end up anyway, which is one `setResultSlot` and no copy. It also
means a case that *counts* constructor calls has no single right answer, and
`tests/cases/by-value.cpp` says so and does not count them.

The check that says this is right is not the suite: **an object cxx1 compiled
links with one clang compiled, in both directions, and the program prints what
the all-clang build prints.** A mangled name can be diffed, but a calling
convention has to be run.

**A static data member is one object shared by the class**, and the whole of
what makes it different from a global is where its name comes from. It takes no
room in the layout - `Type::StaticMember` is kept apart from `members()` so
that neither the size computation nor anything walking the members has to learn
to skip it - and it is **searched up through the bases** rather than copied
down, the way a member function is, because it lives under a name and not at an
offset.

Both ABIs spell the class into the name and **only Microsoft spells in the
access**, as a *digit* where a member function writes a letter - `2` public,
`1` protected, `0` private, measured with cl:

    ?pub@C@@2HA   ?prot@C@@1HA   ?priv@C@@0HA        _ZN1C3pubE (all three)

**`static const int k = 5;` written inside the class needs no definition and
gets no symbol** - measured, cl folds the value in - so it is kept as a value
on the `StaticMember` and read back as one. Anything else with an initialiser
inside the class is refused: the definition outside is where the storage comes
from and the value belongs with it.

Three ways to name one and they all reach the same object: `C::n`, `obj.n` and
`p->n`, plus the unqualified `n` inside a member function, which is found by
name rather than through `this` because it needs no object at all. `obj.n`
still *evaluates* obj - [expr.ref] - but where that expression is pure there is
nothing to evaluate, and dropping it is what leaves an ordinary lvalue rather
than a comma, which is what `b.count = 1` and `&b.count` need. A comma is an
lvalue here when its right operand is, which is C++'s rule and not C's.

`Counter::total = 1;` as a *statement* had to be taught to `atDeclarationStart`,
which otherwise saw a name that names a type and tried to read a declaration. A
declaration whose type is written `C::something` needs a nested class and is
refused, so an identifier naming a class followed by `::` always begins an
expression here.

Refused by name: a static member *function* (the data member is what this step
is), a static member of a class with a constructor - it would have to run
before main, the same mechanism a static local with a constructor needs - and
an initialiser inside the class for anything but a `static const` of integer
type.

**Two Microsoft data-symbol bugs came out on the way**, both pre-existing and
both measured with cl, because a static member is spelled with exactly the same
machinery a namespace-scope variable is:

    int arr[3]        ?arr@@3PAHA        an array decays; cxx1 wrote Y02H
    int m2[2][3]      ?m2@@3PAY02HA      to a pointer to its element
    S *self           ?self@@3PEAUS@@EA  the qualifier carries E as well
    const char *ccp   ?ccp@@3PEBDEB      and repeats the POINTEE's const

The last is the surprising one: `ccp` is a mutable pointer to const char and
the qualifier after the type says `B` all the same.

**A class written inside another one** takes no room in the enclosing object -
`struct Inner { ... };` in a class body declares a type and no member - and
what it gains is a name. `Type::tag()` becomes the *qualified* one,
"Outer::Inner", because every table here is keyed by it and a nested class must
not collide with a global of the same name; `localName()` is the single
component, which is what both ABIs spell; and `enclosing()` is the class it was
written in.

**Both ABIs spell the whole scope, and both compress it**, measured with cl and
confirmed with clang:

    _ZN5Outer5Inner3getEv          ?get@Inner@Outer@@QEAAHXZ
    _ZN5Outer5Inner6sharedE        ?shared@Inner@Outer@@2HA
    _ZN5Outer3useENS_5InnerE       ?use@Outer@@QEAAHUInner@1@@Z

The last line is the one to understand. Itanium makes **each enclosing class a
substitution candidate of its own**, so inside a member of Outer the parameter
type Outer::Inner is `N S_ 5Inner E` - Outer found in the table rather than
spelled again. Microsoft lists the scopes innermost first and back-references
them the same way, and `1` there is Outer. `Itanium::prefix` and
`Microsoft::scopeOf` are the two functions that do it, and every name that
names a class goes through one of them.

**The scope is walked at lookup rather than kept as a stack of tables.** The
one type-name table this parser has is flat and keyed by the qualified name;
`findTypedef` falls back through `classStack_` - the classes whose bodies are
being parsed - and then `currentClass_`, which is what makes `Inner` mean
`Outer::Inner` inside the class body and inside any member function of it, and
nowhere else.

Three questions had to be re-asked once a name could have more than two
components. `atUntypedMemberDefinition` walks every level to recognise
`Outer::Inner::Inner(` and `Outer::Inner::~Inner(` - a definition whose name is
its class's own and so has no type in front of it. `specifiers` takes the
**longest prefix that names a type**, so `Outer::Inner x;` declares an x rather
than leaving `::Inner` to the declarator. And `atDeclarationStart` answers yes
only where the name *stops* at a type: `Outer::Inner x;` is a declaration where
`Outer::Inner::shared = 1;` is a statement.

`constructorKey` and `destructorKey` take the last component - `localOf` - so
that "Outer::Inner" keys "Outer::Inner::Inner", which is what a definition
outside the class spells.

A nested class is a member, so `private:` reaches it, and the name is refused
where it is written rather than at the first use of an object.

**The destructor a class does not write** is the fourth special member, and it
decides when the other three's work is undone. It becomes a function exactly
when a base or a member has one of its own to run - measured with cl, which
emits `??1Has@@QEAA@XZ` for a class holding members with destructors and no
destructor symbol at all for a class of plain ones.

**A virtual function does not make it non-trivial**, and that is the one worth
writing down because it would have been guessed the other way: cl emits no
destructor for a class with a virtual `f()` and nothing else. What makes it
*virtual* is a base whose destructor is virtual - then it takes over that slot,
gets a deleting form beside it, and `delete` through a base pointer reaches it.
Microsoft writes `U` there where a non-virtual one is `Q`, the same rule a
member function already had and one this compiler was getting wrong for written
virtual destructors too.

Members are undone after the body, in the reverse of the order they were
declared, then the bases in reverse - and an array backwards, which is the
same `eachElement` loop the constructor uses with the index read as
`count - 1 - i`. A base's own destructor deals with the members it brought, so
they are skipped here: they are in this class's member list too, because data
members are copied down.

**Nothing but the constructor path used to add to `alive_`**, so a class with a
member that needed destroying and no constructor of its own was destroyed by
nobody. Any local whose type has a destructor is registered now, constructor or
not - a class can have one and not the other.

Building an array member element by element arrived with this, since the
destructor needed the loop anyway; it had been refused by name until then.

**A member function has an implicit object parameter, and it is ranked like
any other argument** - [over.match.funcs]. That is the whole of what tells
`get()` from `get() const`, and without it a class declaring both could not be
called at all: the two candidates had identical parameter lists and tied.

Binding the object is a reference binding, so it ranks as one:

    object          get()                 get() const
    P               Identity              Qualification   -> get() wins
    const P         not viable            Identity        -> get() const

The last row is why a non-const member on a const object is *unavailable*
rather than merely worse. When it leaves nothing viable, the message that says
so by name is kept - "no function takes these arguments" would send the reader
looking for a parameter mismatch that is not there.

`resolveOverload` takes the object's type and puts its rank in front of the
written arguments', so the best-and-unambiguous machinery below needed nothing
new. Inside a member function the object is `this`, which is what makes a const
member function reach only the const one.

Nothing here is about names: `_ZN1P3getEv` and `_ZNK1P3getEv`, `QEAA` against
`QEBA`, have been two symbols since const member functions landed. It was the
ranking that was missing.

**A class with only a destructor is where the two ABIs genuinely part
company**, and both halves were measured. Itanium passes one **by address**
whatever its size, and the **caller** destroys the copy it made. Microsoft
passes it by the ordinary size rules - in a register if it fits - and the
**callee** destroys its own parameter: cl's `?useSmall@@YAHUSmall@@@Z` calls
`??1Small@@QEAA@XZ` on its parameter before returning, and its caller emits no
destructor for it at all, for the by-address case as much as the register one.
Both return such a class through a hidden pointer.

`Type::hasDestructor()` is the flag, `Parser::passedByAddress` is the rule, and
it is the first thing here that reads `microsoftNames()` to answer a question
about *semantics* rather than about spelling. On Windows a by-value class
parameter with a destructor goes into `alive_` - `Alive::byAddress` says
whether the slot holds the object or a pointer to it - so a `return` unwinds it
with everything else, and the path that falls off the end gets it appended.

**The copy is destroyed at a different moment on the two platforms**, and no
recorded output can cover both: at the end of the full expression on Itanium,
inside the callee on Windows. `tests/cases/by-value-destructor.cpp` takes each
result into a variable before printing it, so the full expression ends before
anything observable happens either way.

**A returned local is not destroyed on the way out.** Its bytes go straight to
the caller's storage and the caller destroys it there - one construction, one
destruction. Destroying it here as well would destroy the same object twice,
which for a class that owns anything is a double free, and it was the shape the
code had: bytes copied *as if* eliding and the local destroyed *as if* not.
Taking the elision is what makes the two consistent, and it is what clang does
at -O0 where cl does not.

**Two exclusion files, and the difference matters.** `<case>.notarget` says
cxx1 cannot compile that case for that target at all - `emit.sh` and
`mangled-names` both read it. `<case>.nonames` says it compiles fine and only
the symbol lists differ for a recorded reason, so emission stays covered and
just the comparison steps aside. Using the first for the second quietly stops
compiling a case that compiles, which is how a suite loses coverage without
anyone noticing.

`docs/CONFORMANCE.md` records what compiles here and should not — currently
that an enumeration is still `int`, and that a class name cannot be hidden by
an object of the same name.

**A parser that loops on bad input is worse than one that says no.** The
specifier loop spun forever on `typedef long T;` where `T` was already a
typedef: `atTypeName()` stayed true and nothing in the loop consumed an
identifier. **Compiler-C has this too** and shipped it through 425 cases,
because no case there redeclares a typedef. Both suites now run every compiler
invocation under `ulimit -t`, so the next one fails rather than wedging the
run.

**Conversion to bool is lowered to a comparison, not taught to the backends.**
`(bool)256` is `true` where `(char)256` is `0`, so the two cannot share a cast
path. `Parser::convert` builds `x != 0` and gives it type `bool`, which every
backend already knows how to emit. This is the pattern to reach for again:
where C++ adds a *conversion*, look for an existing operation to lower it to
before adding a case to three code generators.

## The audit of 2026-09-01, and what it is for

**`docs/audit-2026-09-01.html` is a frozen record and is not to be edited as
things are fixed.** It says what five reviewers found in the tree at 29f8dfb,
with the program and the two outputs for each finding, and its value is that
it is dated: a later reader can tell what was true then from what is true now
only if nobody quietly brings it up to date. Fixes are recorded *here*, in the
table below, and the report stays as written. A second audit gets a second
file beside it rather than an edit to this one.

**What it was.** Five reviewers, one each on C++11 conformance, the C++14
source, the backends and ABI, the written record, and test coverage by
mutation. Every reviewer worked from its own copy of the tree with clang as
the oracle - `clang++ -std=c++11 -pedantic-errors` for the language,
`-target x86_64-pc-windows-msvc` for Microsoft names. Twenty-three defects
were confirmed under a suite that was green on all three boxes, seventeen of
them reproduced a second time before being written down.

**The one sentence worth carrying forward:** the suites were green and the
compiler was not, and the mutation pass says why with evidence - nine
deliberate bugs planted in a copy of the tree, and **four survived every
suite**, including unsigned division emitted as signed *on the host target*
and a Windows prologue that writes a garbage frame pointer into every
function.

| | Finding | State |
| --- | --- | --- |
| S-01 | Returning a by-value parameter elides a copy [class.copy]/31 forbids; one object destroyed twice | **fixed** 2026-09-01 |
| S-02 | A move-only class is copied bytewise and its deleted copy accepted | **fixed** 2026-09-01 |
| S-03 | An override of a non-first base written without `virtual` dispatches statically | **fixed** 2026-09-01 |
| S-04 | By-value aggregates lose their tail bytes on arm64-darwin and x86_64-linux | **fixed** 2026-09-01 |
| S-05 | Tail padding of a POD base is reused; `sizeof` wrong on all three | **fixed** 2026-09-01 |
| S-06 | `delete` of a null pointer runs the destructor | **fixed** 2026-09-01 |
| S-07 | A virtual call in a base destructor reaches the derived override | **fixed** 2026-09-01 |
| S-08 | Comparisons yield `int`; hex literals skip `unsigned int` | **fixed** 2026-09-01 |
| S-09 | Value-initialisation does not zero | **fixed** 2026-09-01 |
| S-10 | A default argument leaks onto the next function declared | **fixed** 2026-09-01 |
| A-01 | `this` and the sret pointer are swapped against cl on x86_64-windows | **fixed** 2026-09-01 |
| A-02 | A vtable references an implicit destructor that is never emitted | **fixed** 2026-09-01 |
| A-03 | Bitfield layout is Itanium's on Windows; zero-width fields match no ABI | **fixed** 2026-09-01 |
| A-04 | `double` to `unsigned long long` is a bare signed convert on x86 | **fixed** 2026-09-01 |
| A-05 | An empty class argument consumes a register on arm64-darwin | **fixed** 2026-09-01 |
| C-01 | A `Signature &` into `functions_` dangles across a default-argument re-parse | **fixed** 2026-09-01 |
| C-02 | An array's size overflows a signed `int`; the assembler is handed a negative length | **fixed** 2026-09-01 |
| C-03 | The host's `long double` decides the target's x87 constants | **fixed** 2026-09-01 |
| C-04 | `long` narrowings make the front end behave differently on the Windows build | **fixed** 2026-09-01 |

**The nineteen in the table are closed** - each has a fix and a case, and each
was verified on the boxes available when it was committed.

**Four more were confirmed by a reviewer and never fixed**, and they are listed
below the table rather than in it, which made "all nineteen are closed" read
as more than it says. They are open, they are real, and two of them are silent
wrong answers rather than refusals: a `goto` past an initialisation runs a
destructor on an object that was never constructed, and narrowing in a braced
initialiser turns 300 into 44 without a word. The other two are `const S s;`
for a POD with no user-provided constructor, which C++11 refuses and this
accepts, and an ambiguity [over.ics.rank] requires between `f(int)` and
`f(const int &)` on an lvalue, which is silently resolved. Counting them, the
audit found twenty-three and this compiler has fixed nineteen.

**And the two the report marks as suspected are fixed too, still unreproduced.**
`Trial` restores `angleSplit_` now, alongside the three things it already put
back; and the assignment that wrote a member's position into the *first*
overload's entry is gone - a no-op when the member being defined was that one,
and a corruption of somebody else's recorded position when it was not.

**The `>>` window is reachable, and the claim here that it might not be was
wrong.** It was checked afterwards and a program was built that reaches it.
The premise of the failed attempts was that a substitution failure needs
`typename T::type`; inside a `Trial` *every* `src_.fail` becomes one, so an
ill-formed array length in a nested template argument raises one between the
halves just as well. One subtlety made it hard to find: any intervening `>>`
launders the single-slot flag, so the poisoning call has to come first and the
type that replays those tokens has to be named before it. With the two `split`
lines reverted, the second call meets the stale index, swallows both halves at
the inner list, and a program this compiler accepts fails to parse. So the fix
is not hygiene - it repairs something a program can reach.

The second one had nowhere to show, and that part holds: the only readers of a
`Signature`'s `pos` are the three implicit-special synthesisers, and the old
write's target is always a user declaration. It is deleted rather than
corrected, so nothing remains to misfire.

### The verification round, and the neighbours it found standing

**A verdict of "fixed" was checked by walking one door over from each fix, and
three of the doors were open.** Each of these is the audited fault's own
disease in a shape the fix did not cover, found by the reviewer who re-judged
the audit and mended on `fable/src-mend`.

**A base mem-initialiser never applied default arguments, and read past its
vector.** `: Base(2)` against `Base(int, int = 6)` walked one argument per
*declared* parameter and the shipped compiler died on three lines of ordinary
C++11 - the same default-argument door C-01 came through, in the call that is
built by hand in `topLevel`. `applyDefaults` is applied there now, and
default-building a base goes through overload resolution instead of a search
for an empty parameter list, so a constructor whose every parameter has a
default serves as the default constructor [class.ctor]/5. The case is
`default-arg-base-init.cpp`, and it watches both doors.

**A class summed its members past the count one member is held to.** The
array declarator refuses what C-02 refused, but `alignTo` takes an `int`, and
two 2000000000-byte arrays in one struct were truncated in that call:
`.zerofill` of -294967296, and `sizeof` the same negative number. Refused at
the member list now, in `long long`, padding included - and worth remembering
that **no sanitizer sees this one**, because the truncation is a defined
conversion. `class-too-large.cpp`.

**And C-03's rule - the build machine must not decide a constant - now holds
for every floating constant, not only a suffixed literal.** Three lanes still
diverged by host: a `double` literal was read through the host's `long
double` and rounded twice on one box and once on another (a 72-digit literal
measured `.quad ...409` from the Mac build, `...408` from the Linux build);
a `float` literal the same, one rounding further down; and the constant
folder computed x87 `long double` arithmetic in the host's own type, so one
`+` of two individually exact literals walked past the literal gate. The
lexer reads every literal once with `strtod` - `strtof` under an `f` suffix -
which is single rounding to the value's own type on every host; that is the
one thing taken on the hosts' word, since C requires those correctly rounded,
and doing without them is the software decimal-to-binary rung the literal
gate already named. The folder computes in `double` and refuses, by name, a
folded x87 constant whose exact result does not fit one - measured by
error-free transforms, TwoSum for the additions and an `fma` residue for the
multiplies - and an integer past 53 bits is the same refusal when it reaches
an x87 lane. `float-literal-exact.cpp` pins two literals built to sit
astride the double-rounding boundary.

**The digit test also stopped calling trailing zeros precision.**
`2.5000000000000000000L` overflowed the accumulator and was refused while
`2.5L` was exact, for one value; zeros are deferred to the exponent now and
only multiplied in when a later significant digit needs them. What remains
conservative, deliberately: an exact value whose *significant* digits pass
nineteen - `1180591620717411303424.0L` is 2^70 exactly - still overflows the
64-bit accumulator and is refused. Lifting that means arithmetic on the digit
string itself; it is cheap to want and not cheap to write, and the refusal is
the safe side of the trade the gate already made.

**The `>>` window has a case now, `template-angle-split.cpp`.** The shape
that reaches it: any diagnostic raised inside a `Trial` is a substitution
failure, so instantiating a nested template argument whose *body* is
ill-formed throws between the halves of a `>>` - and an intervening `>>`
anywhere overwrites the one stale mark, which is why the case's typedef sits
above `main` and why the window resisted reproduction the first time.

**Re-judged on `fable/src-mend`, 2026-09-02, one door over from each of the
three.** The tip that recorded the paragraphs above had never been built; it
built, and two of its three fixes stood while the third was missing entirely -
the base mem-initialiser fix had been lost to the `git checkout` its author
reported, and the compiler still died on the audit's three lines. What was
done, and what each was measured against:

- **Default arguments, everywhere a constructor call is built by hand.**
  `defaultConstructorOf` now answers [class.ctor]/5 - a constructor whose
  every parameter has a default *is* the default constructor - instead of
  searching for an empty parameter list, and the five hand-built calls apply
  the defaults: the base a mem-initialiser named with fewer arguments (the
  crash), the base it did not name, the bases and members of the implicit
  default constructor, a local array, and `new M` / `new P(1)` - which was
  refused as "takes 2 argument(s), given 1" after resolution had accepted
  it. `default-arg-member-init.cpp` proves each against clang, with a
  counter in the default to show it is evaluated once per call, three
  times for an array of three. Two constructors that both take nothing are
  refused as before, with the old wording where clang says "ambiguous".
- **The base list, before the member list.** Two 2000000000-byte bases with
  no members of their own passed the member-list guard as a `.zerofill` of
  -294967295, because a base's offset is an `int` and the cursor the guard
  reads is derived from it; a third base wrapped the offset negative before
  the guard saw anything. Refused per base now, where the sum is still a
  sum. `class-too-large-bases.cpp`.
- **Fives, not tens.** The digit test multiplied its accumulator by ten for
  a positive exponent and overflowed at 1e20, so `100000000000000000000.0L`
  - exactly 2^20 * 5^20 - was refused as inexact. It multiplies by five now,
  the twos being free, and an overflow there is the true answer: the odd
  part is past 2^64. 1e22L is accepted and 1e23L refused for x86_64-linux,
  which is the real boundary of a double. `float-literal-tens.cpp`.
- **What is verified about the three floating lanes, and on which box.**
  The strtod/strtof reads and the double-only fold are checked here on the
  Mac, where the host's `long double` is a double and so cannot tell a
  once-rounded read from a twice-rounded one. The Linux build is the one
  that can, and `float-literal-exact.cpp` fails there by printing `0 1 5`
  if a read is still going through `strtold`. Three-box verification has
  not run on this branch yet. The x87 refusals - `0.5L + 2^53`, `1.0L /
  3.0L`, an integer past 53 bits, `1e23L` - were measured by hand for all
  three targets and fire for x86_64-linux alone, but **no case pins a
  refusal that fires for one target only**: `.error` is judged on the host
  target and emit.sh knows no expected refusal. That is a suite change, and
  it was left for the session that owns the three boxes.
- **Found beside the case, not in the audit: a cloned operand lost its
  linker name.** `clonePure` rebuilt a global as `Var::global(name)` and
  dropped the symbol, so on the Windows target `++n` and `n += 1` took the
  address of `n` - `EXTERN n:PROC` - where the definition is `?n@@3HA`. A
  link error on the one target that mangles a variable; the Itanium targets
  cannot show it. Mended in the clone, and the Windows lane of
  `default-arg-member-init.cpp` is what would have failed.

**A user-written constructor builds the class members it does not name.**
[class.base.init]/8: a member the mem-initialiser list leaves out is
default-initialised, and for a class type that is its default constructor.
The implicit constructor did this from the start; a written one did not -
`struct S { M m; S() {} };` with `M() : v(3) {}` left `m` holding the stack,
printed 1 where clang prints 3, and then ran `~M` on it from the destructor
the compiler wrote. A silent wrong answer with a destructor for an object
nothing built. Mended by pulling the member's constructor call out of
`synthesizeDefaultCtor` into `constructMember`, and walking the members
*once*, in declaration order, in both constructors by the first of three
rules that applies: named in the list, own initialiser, class with
constructors. Two things followed from the one walk:

- `: m(args)` on a class-typed member with constructors now *constructs*
  it, through overload resolution and `applyDefaults`, where it was refused
  as an assignment ("'n' is 'struct N' and this is 'int'") - so `: n(9)` and
  a written copy constructor's `: m(o.m)` both reach the right constructor.
  An array member named with arguments is refused by name, as clang refuses
  it.
- The implicit constructor used to apply a member's own initialiser and then
  default-construct the same member over it; the walk applies one rule per
  member, which is what /8's "otherwise" says.

Measured against clang, constructors and destructors printing, in
`member-default-init.cpp`: a user-provided default constructor, an
all-defaulted one, the implicit non-trivial one nested two deep, an array
member, a scalar named beside a class left unnamed, a base beside a member
with either named, a non-default constructor, and two written copy
constructors - every line identical, order included.
`member-default-init-refused.cpp` is the member with no default constructor,
refused where clang refuses it. A union's members are still not built.

**Where that walk meets `: m()`, and which rule owns an empty pair.** The
construction above and the value-initialisation `: m()` were written apart,
against the same function, and the seam between them is one question:
**parentheses with nothing between them are [dcl.init]/8 and not
[class.base.init]/7.** So an entry the list left empty is recorded when the
list is read, and the declaration-order walk asks *that* before it asks what
the member is - construction is never handed an empty argument vector from a
list. It cannot usefully take one: for `: p()` on a plain struct, `: k()` on
an `int`, `: d()` on a `double` or `: arr()` there is no constructor to
reach, and the answer is zeroing, which construction has nothing to say
about. Handing it the empty vector anyway is what refused `: p()` with "'p'
takes one value here, given 0", a form ordinary C++ writes daily.

**And the arity check moved with the construction.** One value per entry used
to be settled in the loop that reads the list, where every entry looks alike;
a class-typed member now takes as many values as one of its constructors
does, so the question is asked further down, after the member's own type has
had its say, and only of the entries that are neither constructed nor
value-initialised. `mem-init-two-rules.cpp` is the seam itself - one list
carrying `: m(1, 2)`, `: p()`, `: k()`, `: d()`, `: m2()`, `: q()`, `: v()`
and `: arr()`, measured against clang with every constructor and destructor
printing - and `mem-init-arity-refused.cpp` is `: k(1, 2)` on an `int`, which
is what proves the check moved rather than went.

**The door beside it, and it is mended now: a class-typed member with its own
initialiser is built from it.** `struct E { M m = M(2); E() {} };` printed
`M2 E ~M | 2 ~M`: the temporary constructed, its bytes *assigned* into storage
nothing had constructed, and then destroyed - one constructor and two
destructors, and for a class that owns anything, the member left holding what
the temporary's destructor had given back.

`memberInitialiser` builds the member through `constructMember` -
[dcl.init]/17 copy-initialisation, the same overload resolution `: m(x)` does,
reaching the copy, the move or a converting constructor - and **the
initialiser's temporaries are destroyed at the end of it**, which they were not.
They sat on the pending list until something else flushed them, so the temporary
died late; once the member was built by a copy rather than handed the bytes, it
would not have died at all. Both halves were needed and the case would have
caught either one alone.

What is left is the elision, recorded rather than open: clang builds `M(2)`
straight into the member and cxx1 makes the copy C++11 also permits. Live-object
counts agree and constructor counts do not, which is why
`member-init-class.cpp` counts the first and why it carries a `.nonames` - the
copy constructor cxx1 emits is one clang never needed.

**What the audit was worth, in the end.** Nineteen defects under a suite that
was green on all three machines, and the fixes added 62 cases - the suite went
from 201 run cases to 223, and from 292 emissions to 348. Four of the fixes
were wrong the first time and the tests said so: a scratch register that held
the base address, a proxy that was true of every synthesised type, a
Microsoft unit rule that forgot a unit is charged for whole, and three
existing cases that turned out to be pinning an ABI's answer as though it were
a fact. Two of those four were caught only by running on the box that owns the
target.

Four more silent wrong answers are recorded in the report and not in this
table because they were confirmed by a reviewer and not re-run here: `goto`
past an initialisation, narrowing in list-initialisation, an ambiguity
[over.ics.rank] requires that is silently resolved, and `const S s;` for a POD.

**Two of those four are fixed, 2026-09-02**, on the Mac and awaiting the
other two boxes: the jump past an initialisation
(`tests/cases/goto-past-initialisation`, with its scalar, backward-into-a-block
and `switch` neighbours), and narrowing in a braced initialiser
(`tests/cases/narrowing-in-braces`, with its aggregate-member and file-scope
neighbours). Mending the first found a third: a `goto` *out* of a block
holding a live object skipped the destructor, because the refusal written for
it was unreachable - a jump destroys what it leaves now, `break` and
`continue` included. The other two of the four stay open.

### C-04: the widest integer there is, and which box decides how wide that is

**[cpp.cond] evaluates a condition in the widest signed integer there is**, and
five levels of this evaluator worked in `long`. A `long` is 64 bits where gcc
and clang build this compiler and 32 where cl does, so a condition holding a
value wider than 32 bits answered differently depending on which of the three
boxes had built the preprocessor reading it - `#if 0x300000002 & 0xFFFFFFFF`
among them. The same source, two answers, and no test could see it from one
machine. The bit-field widths went the same way as the array length before
them, for the same reason.

**And the arithmetic wraps now rather than overflowing.** This evaluator runs
inside a compiler, so "undefined behaviour on overflow" would mean the
compiler itself - negating the most negative value, the one division that
overflows, and a shift count outside 0 to 63 were each reachable from a
`#if` line. The first two are defined through the unsigned type, which is what
every preprocessor a program is likely to have met already does; the third is
refused by name, because a count of 100 is a mistake rather than a large
shift.

### A-03 and C-03: two ABIs' bitfields, and a constant the host was choosing

**One bitfield walk served both ABIs, and it was Itanium's.** Itanium packs
bitfields end to end and lets one allocation unit hold fields of different
declared types. The Microsoft ABI gives each declared type its own unit,
starts a new one whenever the type changes or the current one is full, and
**charges for the whole unit however little of it is used** - that last part
is what decides the size, and it is what the first attempt here missed:
`{int a:3; char b:2;}` puts the char at offset 4, not offset 1. Eight shapes
are pinned in `tests/cases/bitfield-layout.cpp`, each measured with clang for
the ABI it sits under.

**A zero-width bitfield matched neither ABI.** `{char a; int :0; char b;}` is
5 bytes aligned 1 on Itanium and 2 aligned 1 on Windows; cxx1 made it 8
aligned 4, because the `int` was allowed to widen the class the way a real
member would. It does not: it moves the next field and contributes nothing
else.

**And the host was choosing the target's constants.** A floating literal is
read with the host's `strtold`, and the host is not the target: a `long double`
carries 64 bits of significand where gcc built this compiler and 53 where
clang did, so `long double x = 0.1L;` compiled for x86_64-linux was
`0xCCCCCCCCCCCCD000` from a Mac-built cxx1 and the correct
`0xCCCCCCCCCCCCCCCD` from a Linux-built one. **Three-box verification cannot
see this**, and that is the interesting part: each box builds its own compiler
and each agrees with itself.

The question asked is now one the *digits* answer rather than the host: a
literal that is exactly a double is parsed identically by every host and
emitted identically by all three targets, and one that is not is refused where
the target's `long double` is wider than a double - which today is
x86_64-linux alone. `10^-k` divides out only through its fives, so the test is
integer arithmetic on the digit string and nothing else.

**This trades a capability for consistency, and says so.** A Linux-built cxx1
could spell `0.1L` exactly and no longer will. Approximating it differently
per build machine was the alternative, and a silent difference that only
appears when someone rebuilds elsewhere is worth less than a refusal that says
what is missing. Lifting it means converting decimal to binary in software,
which is its own step and is written down as one.

**Why there is no `.error` case for that refusal:** it fires for one target
and not the other two, and `run.sh` compiles for the host. A case that is
refused on the Linux box and accepted on the Mac would make the suite say
different things on different machines, which is the fault this fix exists to
remove. The positive half - the literals that are exact, and their emitted
bytes - is a case.

### A-05 and C-02, and a proxy that was true of every class the parser builds

**Apple's arm64 ABI ignores an empty class in the parameter list**, and
`sizeof` being 1 is not the question. cxx1 gave one a register and shifted
every argument after it along - consistent with itself, so only a mixed link
showed it. Measured with clang: `take(Empty, int x, int y)` puts x in w0, and
`take(Wrap, int x, int y)`, where Wrap's only member is an empty class, puts x
in w1. An empty *base* is ignored; a member is not.

**The first attempt asked `dataSize() == 0`**, which is true of every empty
class the parser lays out - and also of every type the compiler *synthesises*,
because nothing sets dataSize on those. A pointer to a member function is a
struct of one or two words built in `TypeTable`, so it answered 0 and was
passed in no register at all: `member-function-pointer` died with a bus error.
The member list is what actually says whether a class carries anything,
whoever built it, and that is what the test asks now. Worth remembering as a
shape: a field the parser fills in is not a property of the type system, and
the backends see both kinds.

**And every size here is a signed 32-bit count.** `static int a[600000000]`
overflowed one and was laid out anyway - `.zero -1894967296`, written into the
assembly by the shipped `-O2` binary without a word. The multiply is in
`long long` now, and an array this compiler cannot measure is refused where it
is written, by name, with the arithmetic done by division so the check itself
cannot overflow. The array length is read as `long long` rather than `long`
while the line is open, which is half of C-04: on the box where cl builds this
compiler a `long` is 32 bits, and `char a[0x100000001]` silently became
`char a[1]` there and kept its length on the other two.

**The sanitizer build is clean on the whole suite now**, where before it
reported this overflow - which is the point of having kept the control: the
same build still fails to report anything only because there is nothing left
to report.

### A-01 and A-04: the register `this` travels in, and the one x86 lacks

**cl passes `this` first and the hidden return pointer second; cxx1 passed
them the other way round.** For a free function returning a large struct the
pointer does come first, on every ABI - the Microsoft rule is about *member*
functions, and cxx1 reserved integer slot 0 for the pointer ahead of every
parameter including `this`. It did so on both sides of every call it
generated, so every case agreed with itself and disagreed with the platform.

Measured with clang for this ABI before anything was written:
`movq 56(%rsp), %rcx; leaq 32(%rsp), %rdx; call ?get@W@@QEAA?AUBig@@H@Z`, and
the callee reads its member through `%rcx` and writes its result through
`%rdx`. That is what cxx1 emits now, on both sides.

**The parser had to learn to say so.** A member call is lowered to an ordinary
one with the object's address in front, which is what makes everything below
it simple - and it is exactly the distinction the Microsoft ABI needs back.
`Call::hasThis` and `Function::hasThis` carry the one bit; `Signature::owner`
already knew the answer at every site that resolves an overload.

**`tools/windows/sret-check.cmd` is how it is proved**, and it is the shape
this class of bug needs: cxx1 compiles the member function, **cl compiles the
caller**, and they are linked and run. A same-compiler test cannot ask this
question - cxx1 was wrong in a way that was perfectly consistent with itself.
It sits beside `throw-check` and `catch-check` in the Windows leg of
`tools/verify-three`, and is the first thing in the fleet to check a calling
convention against the platform's own compiler rather than against a second
implementation of it.

**And `cvttsd2si` is a signed conversion, which is the only one x86 has.**
Below 2^63 that costs nothing; at or above it there is no signed answer and
the instruction returns the integer indefinite value, so
`(unsigned long long)12000000000000000000.0` came out 9223372036854775808 on
both x86 targets. arm64 has `fcvtzu` and was right all along - the house bug
class again, one target correct and two not. Subtract 2^63, convert what is
left, put the bit back; it is the mirror of the halving trick the other
direction already had, and it branches on `jae` rather than using `cmov`
because the Microsoft speller knows the one and not the other.

### A-02: a vtable slot is a use, and the Microsoft table names one fewer

**Emitting a table odr-uses everything in it.** The `used` flag came only
from calls, and a slot holding a function's address is not a call - so
`struct D : B { };` where B's destructor is virtual got a table pointing at
`~D` and no `~D` emitted anywhere. Nothing in the program named it and
nothing had to: `delete p` through a `B *` reaches it through the slot. The
link failed with a symbol not found, on a program rung 4 says works.

The slots are marked when the table is emitted, which is during the class's
own completion and well before `defineImplicitFunctions` walks the list.

**And then the same fault one symbol over, on one ABI.** Itanium has two
destructor slots - the complete-object form and the deleting one - so marking
the slots covers both. MSVC has a single slot holding the deleting destructor,
whose body calls an ordinary destructor that nothing else points at, so `??1E`
was still unemitted there and nowhere else. The class's own destructor is
marked alongside the slots now, and the Windows name comparison agrees on all
seventeen.

Worth the note because of how it was found: the fix looked complete, the Mac
and Linux suites were green, and it was the clang-as-MSVC name comparison that
said one ABI was still missing a symbol. That comparison runs on the Mac and
is the only thing in the fleet that would have caught it before the Windows
box did.

### S-09 and S-10: an object nobody set, and a default nobody asked for

**`P()` handed back the frame slot as it stood.** [dcl.init]/8 makes `T()`
value-initialisation, and for a class with no user-provided constructor that
is zero-initialisation. The comment in `classTemporary` read "an object with
nothing to set" - which is the wrong reading of a class that has no
constructor to *run*, and exactly why the zeroing is the compiler's job.
`f(P())` returned whatever the frame held, reproducibly and differently per
call site.

`initZero` already says this in statements, for a declaration; `zeroLeaves`
says it as an expression, which is the only form a `T()` in an expression can
take. The leaf walk is `initZero`'s and the access is `targetFor`'s, rooted at
a frame slot rather than at a declared name. The case dirties the frame first,
because on a clean stack the fault is invisible.

**Worth knowing about the case:** clang zeroes a nested temporary with a call
to `memset` where cxx1 writes the fields out. For a compiler that ships no
runtime that is the answer it has to give, and it is recorded rather than
matched.

**A default argument was dropped where it belonged and read where it did
not.** [dcl.fct.default]/4 lets a later declaration add a default the earlier
one did not give. The redeclaration path cleared the `noexcept` it had just
checked and left `pendingDefaults_` sitting in the parser, so `g`'s default
was lost - and then attached to the *next* function declared, which made `h()`
a legal call that evaluated g's token stream to fill it in. It returned 50.

The defaults are merged now rather than replaced, which is what the rule
actually says: what one declaration gave and the other did not is added, a
second default for the same parameter is refused by name, and the union has to
be a suffix even where neither half was on its own. Both halves are cases -
`g()` works, which it did not before either, and `h()` is refused.

### S-07 and S-08: the object during teardown, and two types

**A destructor never stored the vptr.** [class.cdtor]/4 says a virtual call
from a constructor or a destructor reaches the final overrider in *that*
function's class - the object is what the level currently running built, and
no more. Constructors stored the pointer as each level ran and destructors
did not, so on the way down the object still claimed to be the most derived
thing it had been: during `~A` a virtual call ran C's override, against
subobjects C had already finished destroying.

The fix is one condition. The store already existed and was already in the
right place - in front of the body, with the base's call wrapped around it -
and the test that reached it named only the constructor. A destructor gets it
now, and a three-level trace matches clang both for an automatic object and
through a base pointer.

**A comparison yielded `int`, which is C's answer.** [expr.rel]/1 and its
neighbours make it `bool`, and the difference is visible three ways: `sizeof`
was 4 where it is 1, overloading on `bool` against `int` chose the wrong
function, and `auto b = (x == y)` produced something that would hold 3. Four
sites, one word each. Nothing that used the old answer as a number had to
change, because a bool promotes to int wherever one is wanted.

**And [lex.icon] table 6 holds two ladders, not one.** A decimal literal with
no suffix climbs int, long, long long and never reaches an unsigned type; a
hexadecimal or octal one has an unsigned rung above each signed one. One
ladder served both, so `0x80000000` came out `long` where it is
`unsigned int` - which changes `sizeof`, and changes the signedness of any
comparison written against a mask: `0xFFFFFFFF == -1` was false where C++
makes it true. Which base a literal was written in now survives the lexer,
which is what the two ladders need to be told apart.

### S-06 and S-04, and a sixth site an audit of five missed

**Deleting a null pointer ran the destructor.** [expr.delete]/2 says it has no
effect; `operator delete` took null itself and always did, and what needed the
guard is the destructor call in front of it - and, for a virtual destructor,
the whole call, which loads the vtable through the pointer before it frees.
`delete p;` on a pointer that may be null is the reason `delete` gets written,
and it segfaulted. The guard is written `p != 0 ? (call, 1) : 0` rather than a
conditional with void arms: the value is thrown away either way, and an int on
both sides is a shape every backend already emits.

**A partial lane was written with the largest single instruction that fit.**
An aggregate whose size is not a multiple of eight ends in a lane of 3, 5, 6
or 7 live bytes, and every place one moved wrote two bytes for a three-byte
tail and four for a six - leaving the rest of the object as whatever the
destination held. `struct { char c[3]; }` passed by value arrived with its
last byte missing.

The tails are composed now, on arm64 by shifting the value register down (it
is dead after its own store at both sites) and on x86_64 by three helpers -
memory to memory, register to memory, memory to register. **The last of those
had to be written with no scratch register at all.** Building a value from the
bottom up needs one, because a 32-bit write zeroes the upper half of its
destination on this machine and an 8-byte read would read past the object; so
it goes from the top down, shifting the accumulator up a byte and OR-ing the
next one into its low eight bits, where an 8-bit write leaves the rest alone.

**Two things this cost, and both are worth writing down.** The first attempt
borrowed `%rcx` as a scratch and then kept reading the base address out of it,
which segfaulted on a three-byte struct - on the Linux box, and only there,
because the Mac's suite and both emit suites were green. `run.sh` runs the host
target only, so x86_64 code is executed on exactly one machine in the fleet and
`tools/verify-three` is the only thing that runs it.

And the audit named five sites; there are six. The one it missed is the
caller's load of an aggregate small enough to travel in registers - the most
ordinary case there is, `take(v)` with a `struct { char c[3]; }` - which was
found by bisecting sizes 1 to 15 on the box rather than by reading. Every size
in that range is a case now.

### S-03 and S-05, the two that made the object model quietly wrong

**A class may override a virtual of any base, and the slot search knew about
one.** `vtables_[cls]` is seeded from base zero alone, so a member overriding
a second base's virtual was found nowhere, was declared non-virtual, and a
call through a `B *` reached B's own function. Writing `virtual` set the flag
by hand and everything downstream worked - which is why the whole suite
passed, since every case in it writes the keyword, and why the two spellings
of one declaration meant different things.

[class.virtual]/2 does not care which base declared the function, so neither
does the search now: it looks through the bases after the first as well, and
what follows is unchanged from the path the keyword already took. The emitted
table is clang's, entry for entry - 56 bytes, `_ZThn8_N1D2fbEv` in the
secondary section - and the two spellings produce identical objects.

**Tail padding was reused for every base, and the ABI reuses it for some.**
Itanium sets dsize == sizeof for a POD and only lets a derived class into the
padding of a base that is *not* one; the Microsoft ABI never lets it in.
`struct TD : TP` with `TP { int; char; }` came out 8 bytes where all three
oracles say 12, and the cost is not the number: the base and the derived
member overlapped, so assigning through a `TP *` wrote over `TD::c`.

`podForLayout` is asked while the class is being completed, which is *before*
its implicit special members are declared - so what it sees is what the
program wrote, which is exactly the question the ABI asks. A vptr, a base, a
constructor or a destructor each answer no; the rest of the standard's list
cannot be written in this language yet, `operator=` being refused by name.

**Three existing cases printed a size that is an ABI's answer and not a
fact.** `vtable`, `tuple` and `template-dependent-base` each printed a
`sizeof` into one `.expected` shared by three machines, and the numbers were
Itanium's - so the Windows box failed all three the moment its layout became
correct. Each is a `static_assert` under `#ifdef _WIN32` now, with both
answers measured against clang for both ABIs: `Derived` 16 against 24,
`Tuple<int,char,double>` 16 against 24, `Deeper<double>` 24 against 32. That
is stronger than the print it replaced, because `emit.sh` checks it for all
three targets from whichever box is running, where a printed number is only
ever checked on the host.

**The empty base is the other half of the same rule and had to stay put.** An
empty class contributes nothing on every ABI - that is the empty base
optimisation, not tail padding - so the 0 is kept and only the non-empty case
consults the POD question. Seven layouts are pinned in
`tests/cases/base-tail-padding.cpp`, measured against clang for all three
ABIs, and the two that differ by ABI are written out under `#ifdef _WIN32`
rather than skipped: cl never reuses, so its `ND` is 12 where Itanium's is 8
and its `VD` is 24 where Itanium's is 16.

### C-01, and why the fix is at the two doors rather than at twelve

**`resolveOverload` handed back a reference into `functions_`.** That vector
grows whenever a function is declared, and the caller's next move is often to
parse something - a default argument, a conversion - that can declare one. The
reference was then reading freed memory, and so was `applyDefaults`, which
re-reads `f.params` and `f.name` on every turn of a loop whose body is a
parse.

Twelve call sites held that reference and a dozen more hold one straight out
of `functions_`. Editing twelve sites leaves the thirteenth, so **both entry
points hand back a copy now**: `resolveOverload` returns a `Signature` by
value, and `applyDefaults` takes one by value. Every existing
`const Signature &sig = resolveOverload(...)` binds to a temporary and is safe
without being touched, and the compiler found the one site that had taken the
*address* of the result - a base's constructor in a mem-initialiser list,
which now holds its own copy.

**The same bug was found once before**, in `localOwnerOf`'s caller, and fixed
there by taking a copy with a comment saying why. This is that comment applied
at the source instead of at one of its readers.

**It never crashed; it read whatever the reallocation left behind.** The
symptom was a diagnostic naming a function with no name and a parameter that
does not exist - `'' has no default for parameter 3`, for a function of two -
which is a freed `Signature` being printed. An ASan build reports
heap-use-after-free on the same input; the same build is clean on it now, and
on the whole 205-case suite. The negative control matters as much: that build
still reports C-02's signed overflow, so the instrumentation is live and the
clean result means something.

**And a second fault the case's linkage names exposed.** [dcl.fct.default]/5
reads a default argument in the scope of its *declaration*, and
`applyDefaults` already hides the caller's locals to honour that - but not the
enclosing function, so a lambda written in a default argument was given the
closure type of whichever function happened to call. clang names one written
at namespace scope `_ZNK3$_0clEi`; cxx1 wrote `_ZZ4mainENK3$_0clEi`. The
function is hidden and restored now alongside the locals, and both Itanium
targets agree with clang name for name. Windows keeps a recorded difference -
clang-msvc spells a namespace-scope closure `<lambda_0>` and encodes its
deduced return as `@` where cxx1 writes `$_0` and the deduced type - noted on
the case.

### What S-01 and S-02 were, and why they are one fix in two places

**The return path never called a copy constructor at all.** It moved the bytes
into the caller's storage and elided the *destructor* of the source to keep
the tally right - which is correct for exactly one case, an automatic object
of the function, where [class.copy]/31 also lets the copy itself go. For
anything else it left the caller holding a byte copy no constructor had made
and that the source would also destroy: `T pass(T t) { return t; }` built two
objects and destroyed three, and `return *p;` did the same. The standard
excludes a parameter from that elision *by name*, and the reason is exactly
this: the caller made the argument and the caller destroys it.

So `Local` knows whether it is a parameter now, and a return of a glvalue this
function does not own builds the copy into a slot of its own and elides
*that*. One constructor runs, its bytes become the caller's object, nothing
destroys it here - the copy the standard asks for plus the elision it allows,
which is what clang emits.

**The move-only half is the same shape one table over.** `copyConstructorOf`
answering null means two different things - a class that is trivially
copyable, and a class whose copy [class.copy]/7 *deleted* because it declared
a move constructor - and the byte path could not tell them apart. It asks
`moveConstructorOf` first now: with a move constructor in the way the
initialisation goes through overload resolution, which picks the move for an
xvalue; and an lvalue is refused by name, because the deletion is what the
reader needs to be told rather than a resolution failure that would name
something else.

**The cases pin the invariant, not the count.** CLAUDE.md rules out counting
constructor calls because clang elides at -O0 where cl does not, and that
rule is right - so `return-copy-balance.cpp` and `move-only.cpp` count
constructions and destructions and print whether the two agree. Elision moves
both numbers together and cannot change the answer; a double free is exactly
what makes them differ. That is the shape any future case about copies wants,
and it is why nothing in a 201-case suite noticed either bug.

## The line at C++14, and the refusals that hold it

**`src/` is C++14 and the language is C++11, and the risk runs the other way
round from the one that gets watched.** The build enforces the first: `-std=c++14
-pedantic` on three toolchains. Nothing enforced the second - the compiler
accepts what its rules accept, and a C++14 rule that slipped into one of them
would be found by a program that compiled here and nowhere else.

**Measured on 2026-09-01, with `clang++ -std=c++11 -pedantic-errors` as the
oracle for "this is not C++11": fifteen C++14 forms, one of them accepted.**
The one is worth the whole exercise:

```cpp
struct S { int i = 1; int j = 2; };
S s = {5, 6};                        // file scope
```

[dcl.init.aggr]/1 in C++11 makes a class that writes an initialiser on a
member *not an aggregate*, so this is ill-formed; C++14 removed that clause
and it means 5 and 6. cxx1 printed 5 and 6. **A local went to the constructor
path and was refused; a file-scope object is laid out by `flattenInit`, which
knows nothing about constructors** - so the one declaration that never asks
about a constructor was the one that needed to.

**Beside it, and found the same afternoon: a class with a constructor at file
scope never ran it.** `S s;` where `S::S` writes 7 read 0, and it compiled,
linked and ran. The local path refuses a `static` one by name for want of the
mechanism that runs it before main; the file-scope path had no test at all.
That is not a C++14 question - it is C++11 silently not happening - but it
lived in the same three lines and is refused by name now.

**Everything else held**, and one of them holds deliberately: a `constexpr`
non-static member function is implicitly `const` in C++11 and is not from
C++14, and cxx1 answers C++11 on both halves - it accepts the call on a const
object that C++14's clang refuses, and refuses the mutation that C++14's clang
accepts. `ParserType.cpp` says so where it sets the flag.

### A C++14 form has to be refused by name before its C++11 neighbour lands

The two places the line matters most were already named with the version
number in the message - `auto` as a parameter and `auto` as a return type.
The rest were refused only by a parse error, because the *C++11* feature next
to them does not parse either. **That is an accidental barrier, and it
disappears the moment the neighbour is built.** So each now says what it is
and which standard it belongs to:

| Written | Refused with |
| --- | --- |
| `0b101` | a binary literal is C++14 |
| `1'000` | a digit separator is C++14 |
| `decltype(auto)` | is C++14; `decltype` of an expression works |
| `[n = k]` | an init-capture is C++14 |
| `S s = {1, 2}` with an NSDMI | not an aggregate in C++11; C++14 changed that rule |
| `template <class T> T v = ...` | a variable template is C++14 |
| `[[noreturn]]`, `[[deprecated]]` | no attribute parses; C++11 has two, `[[deprecated]]` is C++14 |

The variable template is told from the two C++11 declarations by a token scan
rather than a parse: a class or a function reaches a `(` or a class key before
any `=`, and an out-of-line member writes a `::` before its own.

**The rule this leaves behind:** when a C++11 feature in that table's left-hand
column is built, the C++14 form beside it gets its named refusal in the same
commit. `S s{1, 2}` calling a constructor is the next one to meet it - it is
C++11, it is refused by name today as a missing feature, and the aggregate
rule beside it is a different answer to a nearly identical program.

## Three diagnostics that pointed the wrong way, and one name the assembler refused

**A `::` in an Itanium symbol, which never linked.** A class written inside a
function and handed to a function template came out `_Z1fI7main::LEiT_`: the
tag is `main::L` and the mangler spelled it whole, where [mangle] has a
`<local-name>` for exactly this - `Z <function> E <name>`, and clang writes
`_Z1fIZ4mainE1LEiT_`. The Microsoft ABI wraps the same thing in `?1?`, and
clang for that ABI writes `??$f@UL@?1??main@@9@@@YAHUL@?1??main@@9@@Z`. Both
are what cxx1 writes now, measured against clang for all three ABIs.

**The machinery was already there for a *member* of such a class** -
`itaniumLocalMemberName` and the Microsoft `scopeOf`'s `localOwner` - and what
was missing was the type being able to answer which function it was written
in. `Type::localOwner()` is that, set where the parser already computed it.

**Why no suite saw it.** `emit.sh` stops at assembly and the compiler exited
0, so a name the assembler refuses passes there; `run.sh` had no case with a
local class as a template argument; `names.sh` compares the cases that exist.
`tests/cases/local-class-template-arg.cpp` is that case now. Assembling every
emitted case would catch the class of fault - all 98 do assemble today - but
`emit.sh` is deliberately the suite that needs no assembler and runs anywhere,
so the case is the guard rather than a new step in it.

**Three messages that sent the reader somewhere false**, all found by writing
the program each one describes:

* `static_cast<int &>` of a const lvalue said `const_cast` "is not supported
  yet". It landed on 2026-08-30. The message now says what it says for
  `reinterpret_cast` - that the two are written separately on purpose.
* `A<int>::n` said "'A' is a class template, and instantiating one is not
  supported yet", while `A<int> a;` compiles. The instantiation is not the
  problem: reading a template-id as the qualifier of a name is, and
  `typedef A<int> AI;` then `AI::n` works today. The message says that.
* `auto f(int) -> int` said `auto` there "is C++14, and this compiler is
  C++11". **A trailing return type is C++11**, and it is not deduction at all -
  the reader wrote the type down. It is refused as the missing C++11 feature
  it is. The C++14 forms next to it, `auto f() { }` and `decltype(auto)`, keep
  their own answer. The arrow is found by stepping over the parameter list,
  which is still ahead at that point - it was recorded to be read again, not
  consumed.

And `README.md` documented `-target`, which the driver has never accepted; the
flag is `-arch`, as `tests/emit.sh` has always passed it.

## Rung 5: templates, done

**All of 5.1 to 5.7 landed 2026-08-29, each with its own section at the end of
this one.** What follows is the order the work was meant to happen in and the
reasons for that order, written before any of it: what follows is the order the work
is meant to happen in and the reasons for that order, written before any of
it, the way rungs 2 and 3 were.

Rung 5 is larger than rungs 2, 3 and 4 together. Rung 6 is where the three
targets stop being symmetric: Windows EH is SEH-based and needs unwind data,
`ml64` cannot emit CodeView, and arm64-darwin objects here carry no unwind
info. Expect Windows exceptions to lag, and do not promise otherwise.

### The decision everything else follows from: replay, not a dependent AST

A template is **instantiated by replaying its tokens with the parameters
bound**. All tokens are already in one vector and `replayInlineBodies` already
re-parses a body by moving `at_` - that is the same machinery, and it needs no
second AST and no second lookup pass.

**What it costs is two-phase lookup.** Every name in a template body is looked
up at instantiation, which is MSVC's historical model rather than the
standard's. The consequence is that **cxx1 accepts more than C++11 does**: a
non-dependent name declared *after* the template will bind here and be refused
by clang. That is over-acceptance, so it belongs in `docs/CONFORMANCE.md` when
it lands, not in a refusal.

The alternative - parsing each template body once into a dependent AST - is a
different compiler, and it would stall the ladder for a long time. The rewrite
risk that remains is confined to the template front end rather than the parser,
which is what makes the trade worth taking. **If templates ever feel wrong,
this is the decision to revisit first.**

### The order, and why it is that order

**5.1 - parameter lists, the `<` ambiguity, `>>`, and a template table. No
instantiation at all.** This is the rung's `const`: it changes how every
expression is parsed and everything else stands on it. `f<int>(x)` and
`a<b>(c)` are told apart only by knowing that `f` names a template, so the
table has to exist before anything reads a `<`. **Treat `<` as opening a
template-id only when the name is in that table** - never on shape alone, which
is the one mistake here that silently mis-parses code that used to work. `>>`
arrives from the lexer as a single token and has to split inside an argument
list without disturbing the shift operator.

**5.2 - function templates, explicit arguments only.** `twice<int>(x)` is a
complete and useful feature without deduction, and taking it first isolates the
mangler on the smallest case. All the Itanium template work lands here.

**5.3 - deduction.** `twice(1)`. Overload resolution then has to rank a
specialization against ordinary functions - a non-template wins a tie,
[over.match.best]. Kept separate from 5.2 so that when the matching algorithm
is being debugged it is the only new thing in the room.

**5.4 - class templates, explicit arguments.** One `Type` per argument list,
tagged `Box<int,3>`. Nested classes made this cheap rather than hard: `tag()`
is already an arbitrary qualified string with `localName()` and `enclosing()`
beside it, and both manglers already walk a scope.

**5.5 - out-of-line member definitions.** `template <class T, int N> int
Box<T,N>::size()`. The declarator already reads a multi-`::` qualifier for
nested classes; this is that path with a template-id in it.

**5.6 - explicit specialization**, `template <> struct Box<int,3>`, which is
simpler than partial specialization and which partial specialization needs.

**5.7 - partial specialization, then SFINAE, then variadic.** Each is its own
step and each is large. **Partial specialization has landed** - see its
section below. The other two are still unwritten and refused by name, and what
each will want is now clearer:

**SFINAE has landed** - see its section below. What it needed was a
substitution that can *fail* rather than refuse, and the line held exactly
where the plan said: a failure inside a signature, not inside a body.

**Variadic templates have landed too** - see the section below. The
prediction was right about what they would need and wrong about the size:
expansion turned out to be a lookup rather than a substitution, which made it
smaller than SFINAE.

**5.1 to 5.3 is the first shippable milestone**: function templates that
deduce, mangle correctly on all three targets, and link against clang's
objects. Re-plan from there. **Reached 2026-08-29** - see the three sections
at the end of this chapter for what each step turned out to mean.

### What both ABIs do, measured before any of it was written

    Itanium   _Z5twiceIdET_S0_          I...E, and the return type IS encoded
              _ZN3BoxIdLi2EE4sizeEv     Li2E for a non-type argument
    Microsoft ??$twice@N@@YANN@Z        ?? $ name @ args @@
              ?size@?$Box@N$01@@QEAAHXZ ?$Box@...@ is one scope component

Two things to take from that. **Itanium encodes a function template's return
type**, where an ordinary function's is absent - and it spells it `T_`, the
template *parameter*, not the argument it was given. So **a specialization is
mangled from the template's pattern plus its argument list, never from the
substituted signature**: the substituted one cannot say where a type came from.
And Microsoft's non-type argument `$01` is the `number()` helper already in
`src/Mangle.cpp` - value minus one as a digit - so that half is reuse.

### Refused by name until its own step

Default template arguments, template template parameters, member templates,
`typename` as a disambiguator, alias templates, explicit instantiation, and
parameter packs. Each with its own message. That discipline is what kept rungs
2 and 3 honest and there is more to refuse here than in either.

### Two things that will go wrong if they are not planned for

**An error inside a template body is reported at the instantiation**, which is
a line the reader did not write. Every diagnostic from a replayed body needs to
name both places, or the messages will be worse than useless.

**The Itanium substitution table and `I...E` interact**, and the interaction is
not guessable - the `S0_` in `_Z5twiceIdET_S0_` is a substitution of a template
*parameter reference*. Measure every case; do not reason about this one.

### 5.1 as it actually landed

**The table, the `<` ambiguity, `>>`, and nothing instantiated.** A template
declaration is read for its parameter list and its *name*, recorded in
`templates_`, and its definition is then stepped over unparsed. Every use of
the name is refused - by name, and after the argument list has been stepped
over, so the reader is told about the template rather than about a stray `<`.

**`>>` is split by a marker, not by inserting a token.** Every held body and
every template records an *absolute* index into the one token vector, so
inserting a second `>` would move all of them. `takeClosingAngle` takes the
first `>` by leaving `angleSplit_ = at_` behind without advancing, and the
second by advancing past the token. Tying it to the index rather than to a
flag is what lets a replay reaching the same `>>` again start over - which
matters from 5.2, where a body is replayed once per instantiation.

**`Box<Box<int>>` is the test, and the failure it guards against is not a
syntax error but the wrong message.** Without the split the inner list eats
both `>` and the outer one runs to the end of the file: the case's `.error`
holds the class-template refusal precisely because "this template argument
list is never closed" is what it says when the split is broken.

**A function template's name is read with the parameters bound to `int`.**
The name sits behind a return type and a declarator that mention them, so `T`
has to denote *something* before `T twice(T x)` can be read at all. The
stand-in cannot escape: the type built is discarded and the body is skipped.
A class template's name needs none of this - it is the identifier after
`struct`, read straight off, because parsing the body would register a class
that has no business existing until an argument list is given for it.

**Refused by name, each with its own message**: a template template
parameter, a default template argument, a parameter pack, `template <>`,
explicit instantiation, a member template, and both kinds of use. `typename`
is still refused everywhere except where 5.1 now reads it, in a parameter
list.

Suites 59 / 92 / 31; the C corpus is unchanged at 379/424.

### 5.2 as it actually landed

**A specialization is made by replaying the template's tokens with the
parameters bound**, and the binding is the whole trick: a type parameter is
entered as a *type name* and a non-type one as an *enumerator*. Those are the
two things this parser already knows how to look up, so `T x` and `int a[N]`
are read by the ordinary path with nothing taught about templates. The tables
are shadowed and put back, not cleared, because a template parameter may share
a name with something at file scope.

**A body cannot be written where the call is** - the call is in the middle of
another function - so a request is recorded and the definitions are replayed
after the file is read, to a fixed point. A body may ask for a specialization
of its own, which `quad` in `tests/cases/template-function.cpp` does. Same
shape as the implicit special members.

**The two ABIs are handed two different signatures, and that is not
cosmetic.** Itanium gets the template's *pattern* - `T twice(T)`, with
`Kind::TemplateParam` still in it - because its name spells `T_` where a type
came from a parameter, and the substituted signature has lost that. Microsoft
gets the *substituted* signature, which is what it writes. So the declaration
is read twice, once each way, and `Kind::TemplateParam` exists for no other
purpose: it has no size, no alignment and reaches no backend.

**The measured facts, all confirmed against cl as well as clang:**

    Itanium   _Z5twiceIiET_S0_    the return type IS encoded, and as T_
              _Z2f4IiEvT_S0_      void f4(T,T): the second T_ is S0_, not S_
              _Z4takeIiEvRKT_     the qualifier is asked about before the kind
              _Z3numILin11EEiv    a negative non-type argument
    Microsoft ??$twice@H@@YAHH@Z  the substituted return type, H not T_
              ??$negative@H$0?4@  -5 is $0 then '?' then number(5)
              ??$same@US@@@@YA?AUS@@U0@   the template-id's own tables

**The template name is Itanium substitution candidate zero.** That is what
makes the second `T_` in `void f4(T, T)` come out `S0_` rather than `S_`, and
nothing else written by that point could occupy the slot. Measured, because
reasoning about the substitution table is what CLAUDE.md already said not to
do.

**A Microsoft template-id carries back-reference tables of its own.** In
`??$same@US@@@@YA?AUS@@U0@` the parameter's name back-reference 0 is the `S`
the *return type* pushed - the `S` inside the argument list is invisible to
the signature. Both tables are put aside, used fresh, and put back.

**`>` stops being an operator inside an argument list**, and parentheses are
where it starts again - which is the whole reason C++ makes `f<(a > b)>` need
them. One flag, cleared in `primary`'s parenthesis branch.

**`tools/mangled-names` had been excluding every name beginning `??`** - so
every constructor, destructor, `operator=` and deleting destructor the
Microsoft ABI spells had never been compared by it. It was meant to exclude
only `??_C@`, the string literals. Narrowed when a file full of template
specializations came back as "0 names"; all 31 cases still passed afterwards,
so what it had not been checking was right anyway.

**Two pre-existing gaps turned up while probing and neither is about
templates**: `int()` as a value-initialised expression is not parsed at all,
and a definition may not leave a parameter unnamed. Both refuse by name.

Suites 63 / 98 / 33; the C corpus is unchanged.

### 5.3 as it actually landed

**Deduction reads the pattern, which 5.2 already had to build.** The
template's signature with `Kind::TemplateParam` still in it is walked beside
the argument types: a parameter reached in that walk binds, and anything else
has to be matched structurally. There is no table of "which positions are
dependent" because the type says so.

**A reference parameter looks through itself; everything else decays.**
`const T &` given an `int` deduces T as int, the const belonging to the
parameter and not to T. A non-reference parameter sees the argument decayed -
array to pointer, function to pointer, top-level qualifier gone - which is not
a rule deduction invented but what passing something already does.

**A parameter written out in full is not checked here.** Deduction only binds;
whether the argument can actually get there is overload resolution's question,
asked afterwards. Answering it twice would refuse conversions that are legal.

**Deduction failing is not an error.** A name may be both a template and an
ordinary function, and then a template that cannot be deduced is simply one
fewer candidate. It becomes the whole answer only where there is no ordinary
function of that name, and there the reason is worth printing - `'T' cannot be
worked out from this call: it is 'int' in one argument and 'double' in
another`, where clang needs two lines to say the same.

**A specialization is a candidate like any other and loses a tie.**
[over.match.best]. `resolveOverload` gained `betterCandidate`, which is the
comparison it already made plus one line: all conversions being equal, the
function that is not a specialization wins. Without it every call to a name
that is both is ambiguous, and deduction is what makes that the ordinary case
rather than a corner.

**So a specialization is registered under two keys** - `twice<int>`, which is
what the replayed definition declares, and `twice`, which is what resolution
has to see. And `declareFunction` skips a `fromTemplate` entry when matching,
since a specialization is never a redeclaration of an ordinary function.

**A candidate that loses gets no body.** It had to be instantiated before it
could be ranked, but only a specialization something *chose* is defined -
`Signature::used`, the same gate the implicit special members use. That is
what keeps the symbol list level with clang's, where a losing candidate was
never instantiated at all. Measured: a file where the ordinary function wins
has exactly clang's names, and the explicit-argument path had to be taught to
set `used` itself, since no ranking runs there.

**The milestone's own criterion was checked as a link, not a diff.** Two
files including one header of templates, one compiled by cxx1 and one by
clang, linked both ways round, print what the all-clang build prints. A
mangled name can be diffed; that a specialization is *called* correctly has to
be run. Note what makes it link at all: cxx1 emits a specialization as a
strong symbol where clang's is a comdat, so the linker keeps cxx1's and there
is no duplicate - the same divergence `docs/CONFORMANCE.md` records for inline
members, working in this direction by luck rather than by design.

Suites 66 / 104 / 35; the C corpus is unchanged.

### 5.4 as it actually landed

**The plan said nested classes would make this cheap and they did.** A
specialization is an ordinary class whose tag is `Box<int,3>` - and `tag()`
was already an arbitrary qualified string with `localName()` and
`enclosing()` beside it, so every table keyed by the tag needed nothing. The
class path was told one thing: what tag to take. `structOrUnionSpecifier` runs
unchanged over the template's own tokens with the arguments bound, exactly as
a function specialization replays its definition.

**A specialization carries its name and arguments on the `Type`**, because
neither ABI spells the tag: Itanium wants `3BoxIiLi3EE` and Microsoft
`?$Box@H$02@`. `Kind::TemplateParam` and `TemplateArg` are what those two are
built from, and `TemplateArg` moved into `Type.h` for it.

**The measured mangling, cl and clang agreeing:**

    Itanium   _ZN3BoxIiLi3EE4sizeEv        the template-id is a prefix component
              _Z3two6HolderIiES_IdE        the template NAME is a candidate of
                                           its own - the S_ there is the word
              _Z4same6HolderIiES0_         and the whole type is the next one
              _Z6nested6HolderIS_IiEE      both, in one name
    Microsoft ?size@?$Box@H$02@@QEAAHXZ    one scope component
              ?copyFrom@?$Holder@H@@QEAAXAEBU1@@Z   pushed as one name
              ?withClass@@YAXU?$Holder@US@@@@US@@@Z the id's own tables again

**So the Itanium table holds two kinds of candidate**, a type or a template's
name, and `Sub` says which. Getting this wrong is invisible until two
specializations of one template meet in a signature.

**The injected class name is what made the bodies work.** Inside `Holder`'s
own body the word `Holder` means this specialization - `const Holder &` and a
`Holder *` return type both need it. Registered as a member type name,
`Holder<int>::Holder`, so the walk a nested class already needs finds it. Two
places had to be taught separately: from inside the body through
`classStack_`, and **from a replayed body's return type through
`inlineOwner_`** - a held body is replayed at file scope and its return type
is read before the declarator says which class it belongs to, so
`currentClass_` is not set yet. `Holder *self()` failed while
`copyFrom(const Holder &)` worked, which is what pointed at it.

**A specialization's constructor has two spellings and both are needed.** The
source writes `Holder(`; the table keys it `Holder<int>::Holder<int>`, because
that is what `constructorKey` makes of the tag. `PendingBody` carries the
source name beside the tag, and only the name that *is* the class moves.

**Member bodies are not replayed where the class is made.** Instantiating a
class happens in the middle of whatever asked for it - which may be a
declaration inside a function - and a replay goes through `topLevel`, which
clears that function's locals. They are handed back and replayed by the same
fixed-point pass that defines function specializations.

**And only the members something calls.** clang and cl both instantiate a
member function of a class template on use, so emitting the rest puts symbols
in the object that neither oracle has - measured, one unused member was one
extra name on all three targets. `PendingBody` carries the function table's
key and the gate is `Signature::used` again. A body skipped on one pass may be
wanted after another is replayed, which is what the outer loop is for.

**Reading a pattern must not instantiate a class.** `Holder<T>` has a member
of type T, which has no size. `patternOnly_` answers a shallow type instead,
carrying the name and the arguments and nothing else - which is all either
caller reads, since the mangler spells those and deduction compares them.

Suites 68 / 110 / 37; the C corpus is unchanged.

### 5.5 as it actually landed

**The plan called this "the qualified-name path with a template-id in it" and
that is exactly what it was.** One place in the declarator had to learn that
the name it just read may be a class template, in which case what follows is
an argument list and the class it makes is the qualifier. Everything after the
`::` is read by the loop nested classes already needed. That one insertion is
the whole feature at the parse end.

**Finding it took a wrong turn worth recording.** The read that learns what a
`template <...>` declares used to bind the parameters to `int`, which was
enough to find a name - but `Box<T>::size` was then read as a declaration
*named* `Box`, because the declarator stopped at the `<` it could not use. The
symptom was silent: everything compiled and the definitions simply never
appeared, and the link failed with six undefined members. The read binds the
parameters to themselves now, which also stops a name scan instantiating
`Box<int>` for a declaration that asks for no class at all.

**An out-of-line definition belongs to the class template, not to a template
of its own.** The member was declared in the class body; this is only where
its definition happens to be written. So it is kept on the `TemplateDecl` and
replayed when a specialization needs it - **and the list is re-read on every
pass**, because a definition may be written further down the file than the use
that asked for the class. `tests/cases/template-out-of-line.cpp` puts one
after `main` on purpose.

**Replayed through `topLevel`, not `replayInlineBodies`.** The tokens already
say `Box<T>::get`, so with the parameters bound the ordinary member-definition
path reads the qualifier itself. `inlineOwner_` exists to supply a qualifier
the source does not have, and here the source has one.

**Refused by name: a constructor or destructor written outside the class.** A
member function has a return type, which is what gets the declarator started;
a constructor has none, and the ordinary path for that - `atUntypedMemberDefinition`
- looks for `Name ::` and not for a template-id. Its own step.

Suites 70 / 113 / 38; the C corpus is unchanged.

### 5.6 as it actually landed

**Nothing about the class path changed, which is the point.** The tag is
`Name<int>` exactly as it would be if the template had produced it, so every
use finds this one through the same lookup, both manglers spell it the same
way, and its members are keyed the same. What differs is only where the body
came from - which is why a specialization may have members the template does
not.

**The argument list is read against the primary's parameters.** That is what
decides whether an argument is a type or a value, the same rule every other
use follows, and the reason the primary has to be declared first.

**Too late is an error about placement, not a redefinition.** [temp.expl.spec]
wants the specialization before the first use that would instantiate, and if
one already did then two different classes have been given one name. The
existing tag lookup is what catches it, and the message says where to move the
specialization rather than that something is defined twice.

**A written class emits its inline members; an instantiated one emits the
members something calls.** Two rules, and they are not an inconsistency: 5.4
gates an *implicit* instantiation because clang and cl instantiate a member
only on use, while a class written out - ordinary or explicitly specialized -
goes down the path every class has always taken. An explicit specialization
behaves like the class it is. Measured: an unused member of one leaves the
same extra symbol an unused member of a plain class does, which is the
divergence `docs/CONFORMANCE.md` already records for inline members.

**Refused by name: an explicit specialization of a function template.** A
class one is a class definition, which the class path already reads. A
function one has to be mangled from the *primary's* pattern - `_Z5twiceIiET_S0_`
is built from `T twice(T)` plus the arguments, and this declaration is not
that - so it cannot be read as an ordinary definition. Its own step.

Suites 73 / 116 / 39; the C corpus is unchanged.

### 5.7a as it actually landed: partial specialization

**The pieces were already here, which is why this was the smallest step of
the rung.** The pattern read gives a template's arguments with
`Kind::TemplateParam` still in them, and matching a pattern against a real
type is a walk `deduceOne` already did. What is new is that the walk here is
**[temp.deduct.type] rather than [temp.deduct.call]**: `matchPattern` decays
nothing and forgives nothing - a pattern that is a pointer matches a pointer
and nothing else, because there is no conversion here for a mismatch to be
excused by. Deduction from a call is deliberately looser and stays that way.

**The qualifier is asked about before anything else and both sides must
agree.** `What<const T>` matches `What<const int>` with T as int and does not
match `What<int>`; `What<T>` matches both, binding T to the qualified type
where there is one. That ordering is the whole rule.

**The tag never changes.** `What<int *>` is that whether the body came from
the template or from a pattern that matched it, so the mangling and every
lookup are what rung 5.4 left them.

**[temp.class.order] asked the standard's own way**, by matching each pattern
against the other: A is at least as specialized as B when B's pattern matches
A's, with A's parameters standing as opaque types - which is what
`Kind::TemplateParam` already is.

**"Was not beaten" is not the same as "beats", and the difference is a silent
wrong answer.** `P<A, int>` and `P<int, B>` given `P<int, int>` match neither
each other, so neither is more specialized and the program is ambiguous.
Checking only whether the winner had been beaten let that through and picked
whichever was written first. The winner must beat every other candidate.

**Refused by name**: a partial specialization with a parameter its arguments
never mention (nothing could ever work it out), one with no body, and one of
something that is not a class template.

### Two bugs this step found, and neither was about templates

**A class with member functions and no data members lost all of them.** The
empty-class rule - size 1, so two objects have different addresses - returned
from the middle of the class body, before the held member bodies were
replayed, before the implicit special members were declared, and before a
vtable would have been emitted. Calling a member of such a class compiled and
linked to nothing. It had shipped since member functions arrived, because no
case had a class carrying behaviour and no state. Found from the far end: a
class template with two type parameters would not link, and an empty class is
what it happened to be. The rule changes the numbers now and nothing else.

**A const template argument was dropped from the Microsoft name.** Measured
with cl: `W<const int>` is `?f@?$W@$$CBH@@QEAAHXZ`, and `$$CB` is written only
where the thing under the const is not a pointer - `const int *` is `PEBH` and
`int *const` is `QEAH`, the P becoming a Q, which type() already wrote. cxx1
dropped it, so `W<const int>` and `W<int>` shared one symbol: two different
classes, one name, and whichever body was emitted answered for both. Silent,
and only on Windows. Itanium spells it with a K and always had.

Suites 78 / 125 / 42; the C corpus is unchanged.

### 5.7b as it actually landed: SFINAE

**[temp.deduct]/8 is the one place in this compiler where a failure
recovers.** A trial is a stretch of parsing whose failure is an answer:
`Source::fail` throws inside one instead of printing and exiting, and the
candidate that was being formed is dropped. Everywhere else the rule stands -
errors at the point of interception, and nothing recovers. Making that the
*only* exception is what keeps the two disciplines from bleeding into each
other.

**A trial has to put back everything a half-formed signature touched**: the
token position, the class stack, the pattern flag, and above all the bound
parameter names, or the next candidate reads a table that still says T means
int. That last one is an RAII guard inside `readTemplateDeclaration` rather
than a `catch`, because it must also run on the ordinary path.

**Three things had to exist first, and none of them is SFINAE:**

- **A typedef inside a class.** Keyed "S::value", which is the qualified key a
  nested class already uses, so every lookup that finds a nested class finds
  this too.
- **`Value<T>::type` as a type**, which is the `Outer::Inner` walk with a
  class that was made rather than named in front of it. And `T::type`, which
  the old walk could not spell at all: it built the key by joining strings,
  where the first component here is a typedef *for* a class rather than its
  tag.
- **`typename` read and dropped.** It tells a C++ parser that a dependent
  qualified name is a type, which matters only where a template body is parsed
  before its arguments are known - and this one replays a body at
  instantiation. Accepted rather than refused so that a file written for clang
  compiles here too, which is what the whole oracle method rests on.

**`Kind::DependentMember` exists because Itanium spells the pattern.**
`_Z4takeIiEN5ValueIT_E4typeES1_` says `Value<T>::type` where the substituted
signature says int and has forgotten where it came from. `NT_4typeE` is the
other shape. Microsoft needs none of it - it writes the substituted signature
and always did. Measured on both.

**Where it stops is a mangling wall, not a parsing one.** A signature that
depends on its parameters through an *expression* - `enable_if<sizeof(T) == 4,
int>::type` - is spelled by Itanium as the expression itself,
`N9enable_ifIXeqstT_Li4EEiE4typeE`. Nothing here can write that, so it is
refused where it is written. Mangling from the substituted signature instead
would compile and run correctly and produce a name no other compiler agrees
with, and a name that links with nothing is worse than a refusal that says
why.

**Two more silent bugs closed on the way, and neither is about SFINAE.**

`templates_` holds one entry per name, so a second template of that name
replaced nothing and simply disappeared - a missing overload with no
diagnostic. Refused by name now. Overloading function templates is its own
step, and it is what would let SFINAE choose between two templates rather than
between a template and an ordinary function.

**And an empty class passed by value took a register it should not have, and
crashed on one target only.** The SysV eightbyte lanes started out SSE and
only a non-floating member cleared one, which says nothing about a lane no
member covers - an empty class kept them all, went in xmm0 through a `movss`
reading four bytes of a one-byte object, and segfaulted on the Linux box.
arm64 and Windows classify differently and were unaffected: the house bug
class, found by a case that only reached it because a class with a typedef and
no data is exactly what SFINAE is written with. clang settles it -
`take2(E{}, 7)` puts the 7 in `%edi`, so the class consumed nothing - and the
fix is an empty lane list, which every loop that walks lanes already treats as
nothing to move. **Checked by a link**: objects cxx1 compiled link with g++'s
in both directions and print what the all-g++ build prints, including the case
with the empty class sitting between other arguments.

Suites 83 / 134 / 45; the C corpus is unchanged.

### 5.7c as it actually landed: variadic templates

**A pack is bound to a list of types, not to a type**, which is the one thing
`Kind::TemplateParam` could not say. Nothing may write `Ts` on its own; what
reads a pack is `Ts...`, `rest...` and `sizeof...`, and all three want the
list. It is shadowed and restored like every other binding, in a table of its
own.

**`Ts... rest` is one thing written and several parameters made, and which
one depends on who is reading.** In a *pattern* it is one parameter of type
`Ts...` - Itanium spells that `DpT0_` and says the same thing at every size,
which is exactly what lets one pattern serve every specialization. In a real
instantiation it is as many parameters as the pack has members, named
`rest$0`, `rest$1`.

**So expansion is a lookup, not a substitution.** `rest...` at a call is the
names those parameters were given, looked up like any other identifier -
whatever `rest$0` holds now is what goes in. That is why this step was
smaller than SFINAE rather than larger, which the plan had it the other way
round.

**The measured mangling, and the two ABIs disagree about shape as usual:**

    Itanium   _Z7nothingIJicEEiv        a pack argument is J...E
              _Z5totalIiJEEiT_DpT0_     an empty one is JE, and the parameter
                                        is still Dp whatever the size
    Microsoft ??$total@HH@@YAHHH@Z      members listed inline, nothing around
              ??$total@H$$V@@YAHH@Z     and $$V for an empty pack

**Refused by name, each its own step**: a non-type pack (binding one is
binding a list of *types*, and a list of values needs a second list beside it
and a second expansion), expanding a pack into another template's argument
list, and a parameter written after a pack.

Suites 87 / 137 / 46; the C corpus is unchanged.

## Rung 6: exceptions - the plan it was built to, kept as written

**6.1 has landed and has its own section below.** What follows is the order
the rest is meant to happen in, written before any of it.

**The asymmetry the rung-5 plan warned about is real, and the first surprise
is that it runs the other way.** The MASM backend has emitted Microsoft's
unwind data since it was written - `PROC FRAME`, `.PUSHREG`, `.SETFRAME`,
`.ALLOCSTACK`, `.ENDPROLOG`, which ml64 turns into `.pdata` and `.xdata` - so
**Windows frames were already unwindable and the two Itanium targets were
not**. Windows will still lag on the *tables*, which are MSVC's own format,
but not on the frames.

**6.1 - unwind data on the Itanium targets.** Nothing else in the rung can be
tested without it, and it is worth having on its own: a debugger could not
walk out of a cxx1 frame either.

**6.2 has landed** - see its section below. The prediction held exactly: the
type_info pointer was the whole of the work.

**6.3 has landed** - see its section below. It was the largest step, and all
three of its bugs were silent.

**6.4 has landed** - see its section below. The prediction was right: `alive_`
was the whole of the bookkeeping, and what was new was where the code runs
from.

**6.5 - Windows.** `__CxxFrameHandler4` and MSVC's own tables, which are a
different design from the Itanium one rather than a different spelling of it.
**Its first half - `throw` - has landed; see the section below.** What is left
is `try`/`catch`, planned in detail here from cl's own output and not started.

### 6.5b: what cl emits for one try/catch, measured

    main            PROC                  - no FRAME. cl writes its own
                                            unwind data rather than letting
                                            the assembler do it
    $pdata$main     DD imagerel $LN9      - start, end, and the unwind info
    $unwind$main    DD 010419H, 06204H    - the prologue codes, then
                    DD imagerel __CxxFrameHandler4
                    DD imagerel $cppxdata$main
    $cppxdata$main  DB 018H + three imagerel pointers
    $stateUnwindMap$main, $tryMap$main, $handlerMap$main, $ip2state$main
    main$catch$0    PROC in segment text$x - the handler, a function of its own

**The first finding decides the shape of the work: `PROC FRAME` cannot be
used for a function with a handler.** MASM's `PROC FRAME:handler` emits the
handler's address, and the *handler data* - the pointer to the FuncInfo -
goes immediately after it in the same UNWIND_INFO, where no MASM directive
can reach. cl does not use `PROC FRAME` at all here; it writes `$pdata$` and
`$unwind$` by hand. **So cxx1 has to do the same for any function with a
`try`**, which means teaching the Windows backend to emit unwind codes itself
rather than through `.PUSHREG` and `.ALLOCSTACK`. That is a real change to a
part that has worked since it was written, and it is the first step.

**A handler is a funclet - a separate function.** `main$catch$0` lives in
`text$x`, takes the parent's frame pointer in `rdx`, saves it, addresses the
caught object relative to it, and *returns the address to continue at* in
`rax`. Nothing in cxx1's code generator makes a second function out of a
block today, and the funclet must see the parent's frame slots at the offsets
the parent gave them.

**Use `__CxxFrameHandler3`, not 4.** cl defaults to 4, whose tables are a
*compressed* encoding - the `DB 04H, 08H, 010H` above is a variable-length
format rather than a struct. FH3 is still supported by the runtime and its
FuncInfo is the classic documented layout of fixed-width fields. Emitting FH3
is the difference between writing a struct and writing a compressor.

**In order, then:**

1. **Done.** Hand-written `.pdata`/`.xdata`, and for *every* function rather
   than only the ones with a handler - one path, exercised by the whole suite,
   rather than a second one that only the new feature walks. See below.
2. One funclet per handler, with the parent frame arriving in `rdx`.
3. The FH3 tables - UnwindMapEntry, TryBlockMapEntry, HandlerType,
   IPtoStateMap - and the state variable in the parent's frame that the
   runtime reads through `dispUnwindHelp`.
4. Cleanups, which are funclets too, and are what let 6.4's destructors run
   on this target.

Expect this to lag and do not promise otherwise.

### 6.5b steps 2 and 3: the FH3 tables, decoded from cl

Measured with `cl /EHsc /d2FH4-`, which is how to ask for the older handler -
cl defaults to `__CxxFrameHandler4` and its compressed encoding. What follows
is the whole of what one `try { risky(1); } catch (int e) { ... }` needs.

    $cppxdata$main  DD 019930522H            the magic number
                    DD 02H                   maxState
                    DD imagerel $stateUnwindMap$main
                    DD 01H                   nTryBlocks
                    DD imagerel $tryMap$main
                    DD 06H                   nIPMapEntries
                    DD imagerel $ip2state$main
                    DD 028H                  dispUnwindHelp
                    DD 00H                   pESTypeList
                    DD 01H                   EHFlags

    $stateUnwindMap DD -1, 0   twice         toState and action, per state
    $tryMap         DD 0, 0, 1, 1            tryLow, tryHigh, catchHigh,
                    DD imagerel $handlerMap$0$main        nCatches
    $handlerMap     DD 00H                   adjectives
                    DD imagerel ??_R0H@8     the type caught
                    DD 020H                  dispCatchObj - where in the frame
                    DD imagerel main$catch$0 the funclet
                    DD 038H                  dispFrame
    $ip2state       DD imagerel main,   -1   six pairs: an address and the
                    DD imagerel main+13, 0   state in force from it, covering
                    DD imagerel main+24, -1  the funclet as well as the body
                    DD imagerel main$catch$0,    0
                    DD imagerel main$catch$0+13, 1
                    DD imagerel main$catch$0+29, 0

**Three things in that are code, not tables.** The parent writes `-2` into the
unwind-help slot at `dispUnwindHelp` on entry - cl emits
`mov QWORD PTR $T2[rsp], -2` as its first instruction. The `ip2state` map
needs a label at every point where the state changes, which the code
generator has to place. And the funclet's own unwind info names
`__CxxFrameHandler3` and *the parent's* `$cppxdata$` - the two share one
FuncInfo.

**The offsets are frame-relative, and cl's example is rsp-relative because
that function has no frame pointer.** cxx1 always establishes rbp, so
`dispUnwindHelp`, `dispCatchObj` and `dispFrame` have to be measured from
whatever the unwind info calls the frame base - which for cxx1 is rbp with
`UWOP_SET_FPREG`. That is the part most likely to be wrong first.

**The funclet itself** (measured in step 2's listing): a `PROC` in `text$x`
that saves `rdx` - the parent's frame pointer - into its own shadow space,
sets `rbp` from it, runs the handler, and returns the address to continue at
in `rax`. Because cxx1 addresses every local as `[rbp-N]`, a handler body
compiles inside a funclet exactly as it would inline, which is the one thing
about this that is easier here than it looks.

### 6.5b: the Windows frame now has its base at the bottom

**Done, and it stands on its own.** The prologue on this target is
`push rbp; sub rsp,N; mov rbp,rsp` rather than Itanium's
`push rbp; mov rbp,rsp; sub rsp,N`, so **rbp is the establisher frame** the
Microsoft runtime hands a handler, and every local is a *positive*
displacement from it - which is the only thing an FH3 table can express. The
epilogue gives the allocation back by hand, since there is no longer a saved
rsp in rbp to restore from.

**The whole frame moved by one constant, so the translation is one line, in
one place.** Every frame operand in the code generator is written against rbp
as Itanium establishes it, and this target's rbp is exactly `frameSize` lower
- so `[rbp + d]` becomes `[rbp + d + frameSize]` when an operand is
*rendered*, positive displacements included: an incoming stack argument is
above the old rbp and above the new one by the same amount plus the frame.

**That is not where it was tried first.** Rewriting the call sites looked
tidier and was wrong: `mem(-(slot), "%rbp")` is one of at least half a dozen
shapes the generator builds a frame operand in - `mem(off - slot, ...)`,
`mem(-n.resultSlot(), ...)`, `mem(to, ...)` with `to` precomputed - and
catching fifteen of them left the rest addressing a frame that had moved. The
suite said so immediately: **eight Windows cases failed with wrong values and
no crash**, which is what a stack slot read one frame away looks like. One
choke point at the renderer catches every shape by construction.

### 6.5b step 4: cleanups, and rung 6.5 with it

**`cleanup.cpp` runs on all three targets now** - its `.notarget` is gone.
Destructors run as an exception passes through a Windows frame, which is what
makes RAII mean anything there.

**A cleanup is a *state*, not a try block.** Nothing is caught: the runtime
runs some destructors and carries on. It is told so with one state per region
whose *action* is a funclet and whose `toState` is the region before it, so
unwinding walks the chain backwards one object at a time.

**Each funclet destroys only what its own region built**, and that is the
difference from the Itanium pad rather than a detail. The Itanium pad
*resumes* - it does not chain - so it destroys everything alive at that point.
Copying that shape here would destroy the earlier objects once per region.

**The `.pdata` has to be sorted, and funclets break the order.** A `.pdata`
contribution is sorted by the address it describes; every funclet lives in
`.text$x`, which the linker lays after `.text`, so all of them come after all
of the ordinary functions. Emitting each funclet's entry beside its parent
interleaves the two orders and the linker says **LNK1223: invalid .pdata
contributions** - which is what it said as soon as more than one function had
a funclet, and cleanups made that the common case. They go to
`MasmSpelling::trailer_` and are written after everything else.

**A cleanup funclet returns nothing.** A handler hands back the address to
carry on at; a cleanup has nowhere to carry on *to*, the runtime going on
unwinding once it returns.

**`tools/mangled-names` asks clang with `-fexceptions` on Windows now**, which
it could not while cxx1 had no tables for that target. What made the
comparison possible was dropping the **funclet names from both sides**: this
compiler writes `main$catch$0` and cl writes `?catch$1@?0?main@4HA` and
`?dtor$2@...`, and those are *file-local* symbols nothing links against - a
difference between the two spellings is a naming style rather than an ABI
fault, and whether the handlers work is what `catch-check` and the run suite
answer. Every name that crosses an object boundary is still compared, and on
this target that is now done for cases with exceptions in them for the first
time.

### 6.5b steps 2 and 3: done - cxx1 catches on Windows

**`catch-check.cmd` prints `before / caught 7 / after`**, and it runs inside
`verify-three` beside the throw check. That one has cl catch what cxx1 threw
and says nothing about this frame; this compiles both halves with cxx1, so
what it tests is the funclet, the FH3 tables and the unwind data together.

**Two faults stood between the tables being right and this working, and
neither was in the tables.**

**`text$x` is a segment called text.** The funclet's segment was written the
way cl's listing writes it, undotted, and the linker gave it a section of its
own with data attributes - so the runtime found the handler, called it
correctly, and faulted on the first instruction because the page was not
code. **It reads exactly like a dispatch fault and is not one.** `.text$x`
with `ALIGN(16) 'CODE'` is what assembles. This is the *third* time cl's
listing has recorded what cl means rather than something that assembles -
`.pdata` and `.xdata` were the first two, in step 1.

**rsp is restored from rbp, never by adding to itself.** Once the handler ran,
`after` printed and the program then jumped to `0xFFFFFFFFFFFFFFFE`. That is
-2, the unwind-help sentinel: `ret` had taken it as a return address. Resuming
after a catch is the case that decides this - the runtime unwinds the frame
and jumps back into the middle of the function, and **rsp is whatever it left
there** rather than what the body had, so `add rsp, frameSize` lands
somewhere arbitrary. `lea rsp, [rbp + frameSize]` is right, and it is written
as an offset of zero because this target's renderer adds the frame size to
every rbp displacement.

**How they were found, with no debugger on the box.** A vectored exception
handler in a cl-compiled translation unit, installed before the throw, printing
the exception code, the faulting address and the RVA. It showed the C++ throw
going out (`E06D7363`) and then where the access violation landed - and an RVA
matched against `dumpbin /headers` named the section, which was the whole
answer. `AddVectoredExceptionHandler` is worth reaching for before installing
debugging tools.

**Several `try` statements in one function work**, which the first version did
not: it wrote tables for `msTries()[0]` and nothing else, so the second try in
a function had no tables and the program died with no output at all. Try *k*
owns states 2k and 2k+1 - the body and its handlers - and `maxState` is twice
the count. They are numbered rather than nested because the parser refuses a
`try` inside another; a nested one would want `tryLow` and `tryHigh` to span
its child's states, which is what those two fields are for.

**What is still refused, and why it is not a small thing.** `return` inside a
`catch`: a handler is a function of its own there, so leaving one early is not
a jump but a *return* of the address to carry on at - in the register a return
value would travel in. `try-catch.cpp` ends every handler with one and so
still names this target in its `.notarget`; `try-catch-value.cpp` is the same
feature with handlers that fall off their end and runs on all three.

**`tools/mangled-names` still asks clang with `-fno-exceptions` for Windows,
and step 4 is what changes it.** `-fexceptions` was tried now that cxx1 can
catch: fourteen cases differ, and every difference is a `?dtor$N@...` funclet -
clang runs a destructor during unwinding by compiling it into a cleanup
funclet, and cxx1 emits no cleanups on this target. That is a missing feature
rather than a wrong name, and asking for it would report the same absence
fourteen times.

### 6.5b steps 2 and 3, as they stood while stuck

**Superseded in part** - the frame fault described below is fixed; what
follows is what has been eliminated since, and what has not.

**Still failing:** a throw dies at **0xC0000005** rather than reaching the
handler. It faults with an *empty* handler too, so it is dispatch and not the
handler's body. What has been ruled out: the frame shape (above); the funclet
prologue, which is now byte-for-byte cl's shape - `mov [rsp+16],rdx; push rbp;
sub rsp,32; mov rbp,rdx`, cl included; the funclet's own unwind codes, where
`032H` for a 32-byte allocation had been written `042H`; and `dispFrame`,
tried at both `frameSize` and 0 with no change.

**What has not been examined** is the ip2state map, which is where the next
attempt should start: cl writes **six** entries for one try where this writes
four, and three of cl's six describe the funclet at states 0, 1, 0 rather than
the single row this writes. A debugger on the box is the way in - reasoning
from listings has now returned four wrong answers in a row.



**What works on the Windows box.** Handler funclets are emitted in `text$x`
with unwind data of their own naming `__CxxFrameHandler3` and the parent's
FuncInfo; the parent's UNWIND_INFO carries the handler flags and the FuncInfo
pointer; and the four FH3 tables are written. It assembles, links, and **a
`try` that does not throw runs correctly** - `before / no throw / after`,
exit 0. So the shape is right and nothing about it disturbs the ordinary path.

**A throw dies at 0xC0000409**, and the cause is measured rather than
suspected. It is the thing the plan said would be wrong first.

**Where the establisher frame is, when there is a frame register.** Windows
computes it as `rbp - FrameOffset*16`, and every `disp` in the FuncInfo -
`dispUnwindHelp`, `dispCatchObj` - is a *positive* offset up from there.
Measured twice with cl:

| | cl, no frame pointer | cl, `_alloca` forces one | cxx1 |
| --- | --- | --- | --- |
| prologue | `sub rsp,56` | `push rbp; sub rsp,64; lea rbp,[rsp+32]` | `push rbp; mov rbp,rsp; sub rsp,N` |
| FrameOffset | none | 2, so `rbp = rsp_after + 32` | 0 |
| establisher | `rsp_after` | `rbp - 32` = `rsp_after` | **`rbp - 0` = `rbp`** |
| a local | `$T2` at `[rsp+40]` | `$T2` at `[rbp+16]` | at `[rbp-k]` |
| its disp | 40 | 48 = 16 + 32 | **-k** |

**cl's locals are above the establisher and cxx1's are below it.** cxx1 takes
`rbp` *before* allocating the frame, so every local is at a negative offset
from the establisher and a FuncInfo cannot say so - the fields are unsigned
displacements. `disp = frameSize - slot`, which is what is emitted now,
describes a frame this compiler does not build.

**The fix is a change to the Windows frame, not to the tables.** The prologue
has to become `push rbp; sub rsp,N; mov rbp,rsp`, so that `rbp` *is* the
establisher and FrameOffset stays 0 honestly; and every local on that target
then has to be addressed `[rbp + (frameSize - slot)]` rather than `[rbp -
slot]`. That is one change in the prologue and one in how the Windows backend
spells a frame slot, and it touches every function on the target rather than
only the ones with a `try` - so it wants the whole suite behind it, which is
the same argument step 1 made for writing unwind data for every function.

**`try` is refused on this target until then**, deliberately: what is there
now compiles a program that ends itself the first time an exception is
thrown, which is worse than saying no.

**One bug found and fixed on the way, worth not repeating.** The funclet's own
`sub rsp, 32` was written as unwind code `042H`. `UWOP_ALLOC_SMALL` is 2 with
`(size/8 - 1)` in the high nibble, so 32 bytes is `032H`; `042H` claims 40 and
leaves anything unwinding *through* a handler landing in the wrong place.

**`tools/windows/catch-check.cmd`** is the check this rung is for, and the
mirror of `throw-check.cmd`: that one has cl catch what cxx1 threw and says
nothing about this frame, while this compiles both halves with cxx1 and so
tests the funclet, the tables and the unwind data together. It either prints
what it should or the program ends.

### 6.5b step 1 as it actually landed: unwind data by hand

**Four assembler rules, each found by being stopped by it**, and none of them
in cl's listing - which is where the temptation is to look:

- **A label inside a `PROC` is local to it.** MASM scopes them by default, so
  the unwind data - which lives outside the procedure and measures into it -
  could not name the labels the prologue defines. `OPTION NOSCOPED`.
- **A segment cannot be called `.pdata` by default.** MASM will not take an
  identifier beginning with a dot. `OPTION DOTNAME`.
- **And it has to be called `.pdata`.** A segment called `pdata` is a segment
  called pdata: the linker builds the image's exception directory from
  `.pdata`, and without the dot the runtime finds no unwind record for the
  frame at all. This is the one that cost the most, because everything
  *assembled and linked and ran* - it only showed when an exception tried to
  leave a cxx1 frame. `dumpbin /summary` is what said it: `pdata` and `xdata`
  sitting beside `.text$mn`.
- **The attributes have to match** the ones the real sections carry, or the
  linker warns about two `.pdata` sections that disagree: `READONLY ALIGN(4)`.

**Every offset in the unwind codes is a label difference**, not a counted
byte. An unwind code records where in the prologue its instruction *ends*, and
`sub rsp, 40` is four bytes where `sub rsp, 400` is seven - the assembler
chooses, so the assembler is asked. Counting them here would be a second
encoder that has to agree with ml64 forever.

cxx1's prologue has one shape, so the codes do too: `UWOP_PUSH_NONVOL` for
rbp, `UWOP_SET_FPREG`, and an allocation that is `UWOP_ALLOC_SMALL` up to 128
bytes and `UWOP_ALLOC_LARGE` past it.

### 6.5a as it actually landed: `throw` on Windows

**The Microsoft ABI throws from the stack and carries its identity in four
objects**, where Itanium asks the runtime for memory and names one pointer:

    ??_R0H@8       the RTTI type descriptor - the type_info vftable, a spare
                   word, and the decorated name, `.H` for int
    _CT??_R0H@84   one catchable type: the descriptor, the object's size, the
                   virtual-base fields a scalar does not use
    _CTA1H         the array of those, with its count
    _TI1H          the ThrowInfo the runtime is handed

All measured from cl's listing, and the type's own letter runs through every
name, which is what makes them agree without anything being passed between
them. `Program::thrown` carries the list from the parser to the one backend
that needs it.

**Two things cl's listing shows but does not mean literally.** It writes
`DQ FLAT:??_7type_info@@6B@`, and `FLAT:` is 32-bit MASM's way of naming a
flat-model address - ml64 has no such keyword and rejects it. And it marks
each object `; COMDAT`, which is cl's object writer rather than assemblable
syntax: MASM cannot say COMDAT at all.

**So the four objects are file-local rather than PUBLIC**, and that is the
decision worth knowing. A public copy collides with cl's at the link -
measured, `LNK2005` on `??_R0H@8`. Keeping them private works because the
runtime matches a type descriptor by its **name string** rather than by its
address, which is the same rule that lets a throw cross a DLL boundary.

**Checked by a run**: `tools/windows/throw-check.cmd` has cxx1 compile the
thrower and cl the catcher, and `tools/verify-three` runs it with the cases.
An int and a double, because the object's size and the type that names it are
what that tests.

### 6.4 as it actually landed

**The objects are the ones a `return` already unwinds.** `alive_` holds them
and nothing new had to track them; the difference is only where the code runs
from - a landing pad rather than the return path, ending in `_Unwind_Resume`
rather than in a return. A cleanup region is a `Try` with an empty type list,
which is what makes the call-site table write action 0.

**One region per stretch, and the stretches do not overlap.** Two objects give
two ranges - after the first, after the second - each with a pad that destroys
exactly what exists by then. An exception thrown before the second is built
must not destroy it, and a call-site table holds sorted disjoint ranges, so
they are *split* rather than nested. The statements that do the constructing
are outside every region on purpose: an exception from a constructor leaves
that object unbuilt.

**A `Try`'s body became a list rather than a block**, because a cleanup region
covers a slice of an enclosing block's statements and must not open a scope of
its own - the objects it destroys belong to the block outside it.

**The bug worth keeping: resizing a vector down and back up is not undo.**
The first pad bounded its destructor list by shrinking `alive_` and restoring
the size afterwards, which default-constructs what it threw away - so the
*second* pad destroyed an object with no class and silently ran one destructor
short. `emitDestructors` takes an upper bound now and mutates nothing.

**Refused by name: a local with a destructor and a `try` in one function.**
Each is a range in the same call-site table and one would have to split the
other, which is the same reason a nested `try` is refused.

**Windows makes no cleanup regions and needs none**: `throw` is refused for
that target, so nothing can unwind through one of its frames, and the
destructors on the normal path are unchanged. Making them anyway would have
put a landing pad in a backend whose tables cannot describe one - which is
exactly what happened for one build, and `.Lfunc.begin.main` reaching ml64 is
what said so.

**`tools/mangled-names` asks with `-fexceptions` now** for the two Itanium
targets, where it used to ask with them off. The flag was hiding a feature
clang had and cxx1 did not; now it would hide cxx1's own work. Windows is
still asked with them off, for the same reason turned round. Two names are
excluded rather than argued with on every case that has a destructor:
`__clang_call_terminate` and `_ZSt9terminatev`, the guard clang emits for a
destructor that throws while an exception is already unwinding - cxx1 emits no
such guard, which is a real gap and not a spelling.

Suites 90 / 141 / 48.

### 6.3 as it actually landed

**Almost all of a `try` is ordinary statements**, which is what kept this
step from being much larger. A backend is told three things: a label before
the body and one after it, a label at the pad where the runtime arrives with
the exception pointer and the selector in two registers, and the type_info
symbols in order. Everything else - the selector comparison, the calls to
`__cxa_begin_catch` and `__cxa_end_catch`, the copy into the caught variable,
the handler bodies, and the `_Unwind_Resume` that nothing matched - is built by
the parser out of nodes that already existed, as a chain of if/else.

**Three bugs on the way, and every one of them was silent:**

**Every call in a function has to be in the call-site table**, not only the
ones inside a try. libc++abi takes a return address the table does not mention
as a program that should stop, so the ranges between and around the try blocks
are written out with no landing pad. Without them, an exception passing
through a function that has a table at all ended in terminate.

**The table needs a label that is not an `L` temporary.** Mach-O's
`.subsections_via_symbols` lets the linker move and drop the pieces of a
section and it cuts them at *symbols*; a temporary label is not one. With only
`L` labels every table in a file was a single atom, so the **first** try in a
file worked and the second did not - the exception simply was not caught, with
no diagnostic anywhere. clang writes `GCC_except_table0` for this reason and
not for readability.

**The call site's action field is a byte offset plus one, not a type index.**
With one handler the two are the same number and the difference is invisible;
with three, the second try's field has to skip six bytes rather than three
entries. The selector is an index into the *function's* type table too, not
into one try's handler list, which the parser has to know because it is what
its comparisons are written against.

**`tools/mangled-names` chooses `-fexceptions` from the source now.** A file
that writes `throw` or `try` cannot be compiled with exceptions off, and one
that does write them is asking for exactly the symbols the flag was hiding.
Recorded as a divergence: clang emits `__clang_call_terminate` for the case
where a destructor throws while an exception is already unwinding, and cxx1
emits no such guard. **This sentence used to say cxx1 ran no destructors
during unwinding at all**, which was true when it was written and stopped
being true when 6.4 landed a few hours later - the divergence is the missing
guard, not the missing destructors.

**Windows was unreachable when this landed** and the commit went in on two
boxes with that said out loud - `da6bf71` is the one commit here whose message
says so. **The box came back and it passes**: cxx1 builds with cl and all 45
of its cases run, with `try-catch` skipped for that target as its `.notarget`
says. So the gap was in the checking rather than in the code, which is the
outcome to hope for and not one to assume.

### 6.2 as it actually landed

**`throw x;` is three calls and a store, and it needed no new machinery.**

    void *e = __cxa_allocate_exception(sizeof x);
    *(T *)e = x;
    __cxa_throw(e, &_ZTI<T>, 0);

Written as one comma expression, so it is a statement wherever an expression
is one. The Itanium ABI puts the object in memory the runtime owns, hands it
over with the type that identifies it, and never returns.

**The type_info pointer was the whole of the work, as the plan said.** cxx1
has no RTTI: the vtable's typeinfo slot is a plain zero and `typeid` is
refused. For a *fundamental* type the object is already in the standard
library and `_ZTI` plus the type as a signature spells it - `_ZTIi`, `_ZTId` -
so the existing Itanium type encoding names it and nothing has to be emitted.
For a class or a pointer the compiler has to emit the object, and that is
refused by name rather than thrown with a type nobody can catch.

**Windows lags here and the reason is a shape rather than an omission.** The
Microsoft ABI hands `_CxxThrowException` a ThrowInfo, which points at a
catchable-type array, which points at a copy record, which points at the RTTI
descriptor - four objects and an image-relative relocation, where Itanium
wants one pointer. Measured; refused by name for that target.

**Checked by a run, in the other direction from 6.1.** `tools/unwind-check`
grew a second half: cxx1 throws and the system compiler catches, an int and a
double, because the size of the object and the type that names it are what
that really tests - a wrong type_info is caught by nobody.

### 6.1 as it actually landed

**It is the same three directives in every function, because a cxx1 frame has
one shape.** The frame pointer is pushed and then established and never moves,
so the CFA is `rbp + 16` (or `x29 + 16`) for the whole body and the saved
registers sit just below it. Emitted where the frame is established rather
than beside each instruction, which is what clang does too: the unwinder looks
CFI up by *return address*, and a return address can only be inside a call -
there are none in a prologue.

**Checked by a run, because unwind data is either right or the program
aborts.** `tools/unwind-check` compiles three files - one that throws, one in
the middle, one that catches - with only the middle one compiled by cxx1. It
passes now and the same program aborted with "terminating due to uncaught
exception" before, which is what an opaque frame does to a personality
routine. It is in the repository rather than on a box, for the reason
`tools/cl-measure` is.

**cxx1 can throw nothing yet and that is the point of the check**: what is
being tested is the *frame*, and a frame is all cxx1 has to contribute for an
exception to cross it.

Suites 88 / 137 / 46 after 6.2, which adds only a refusal - what `throw`
actually does needs a handler, and the tool is where that lives.

## Rung 7: the C++11 layer - the plan it was built to, kept as written

**The last rung, and the widest.** Six features that mostly do not depend on
each other, so this splits where templates and exceptions had to be pushed
through in one piece. The order below is by what each one needs from the others.

**All of it landed** - see "Rung 7.6: lambdas, and the ladder is walked" and the
sections after it. "Nothing after 7.1 is written" was true the day this was
written and is kept because the plan is worth reading against what it became.

**7.1 has landed.** [dcl.spec.auto] says a variable's `auto` is deduced *as
if by template argument deduction from a function call*, which is not a
similarity to reuse but the rule itself - so `deduceOne` did the work
unchanged, decay and all, and `Kind::Deduced` stands where a type will be the
way `Kind::TemplateParam` stands in a pattern. Everything followed from that:
an array decays, a top-level const goes, `auto &` looks through itself and
keeps what it found, `auto *` deduces through a pointer.

**The initialiser is read twice** - once to learn its type, once to build it -
with the tokens put back in between. Reading it once would mean threading the
expression through every branch of the declaration path, the
class-with-constructors one included, to save a parse that costs nothing.

**Refused by name where C++14 begins**: a parameter's type and a function's
return type. Both said so with the standard named, because the answer to "why
not" is a version number rather than a gap.

**7.2 has landed**, and the prediction was right about where the work is: the
parser already types every expression, so all of it was in the rules.

**The rule is about tokens, not about the tree they build.**
[dcl.type.simple]: an unparenthesised name answers what that entity was
*declared* as; anything else answers the expression's type with a `&` added
when it is an lvalue. `decltype(n)` is int and `decltype((n))` is `int &` -
the same characters but for one pair of parentheses - so the shape of the
tokens has to be read before the expression is parsed.

**A reference variable is what forces the lookup rather than the tree.** Every
mention of one is lowered here to a dereference, so the *expression* `ref` has
type int where the declaration said `int &`. Asking the symbol table is the
only way to answer what was written.

`isLvalue` was already here, written for reference binding, and answered the
second half unchanged - the third time in this rung that the feature was a
rule the compiler already had.

**7.3 has landed for arrays.** [stmt.ranged] says what `for (T x : a)` means
by writing another loop, and every node that loop needs was already here - so
this performs the rewrite rather than adding a construct. The range is
evaluated once, which is what binding it to a name buys in the standard's
version and what assigning it to `__b` buys here.

**Telling it from an ordinary `for` takes a scan, and a `?` claims the next
`:`.** `for (int i = 0, n = c ? 2 : 5; ...)` has a colon and is not a
range-for, so the question marks are counted.

**Build through `arithmetic`, never a bare `Binary`.** `p + 1` on an `int *`
advances four bytes and that scaling lives in the helper the ordinary
expression path uses; a node built by hand with a type stamped on it read the
array one byte at a time - first element right, every one after it garbage,
which is what a missing scale looks like.

Refused by name: a class range, which needs `begin` and `end` found on the
class and called where an array's bounds are in its type; and a reference
loop variable, which needs the binding machinery a reference declaration has.

**7.4a has landed: `&&` as a type, and the binding rule.**

**7.4b has landed too: `static_cast`, and with it the third value category.**
`static_cast<T &&>` *is* `std::move` - the standard library's move is a cast
and nothing else - so until this existed no lvalue could reach a move
constructor and every one of them was unreachable.

What the cast makes is an **xvalue**, and it is carried as a one-bit mark on
`Expr` rather than as a type. The object and its address are unchanged; the
only thing said is that whoever takes it may take it apart. That splits the
old `isLvalue` in two, and the split is where all the behaviour is:

* `isGlvalue` - names an object. **Reference binding asks this**, so an
  xvalue binds to the object itself. Asking `isLvalue` there would have sent
  it to the temporary path, and a move constructor would have run on a copy -
  silently.
* `isLvalue` - names an object *and is not an xvalue*. **The rule that
  `T &&` refuses an lvalue asks this**, and so does argument ranking.
* `decltype` now has three answers rather than two, which is
  [dcl.type.simple]/4 in full: `T &&` for an xvalue, `T &` for an lvalue, `T`
  for a prvalue.

**A by-value parameter had to be taught separately.** `materialiseCopy`
predates rvalue references and reached for the copy constructor by name, so
`take(static_cast<S &&>(e))` copied and left `e` untouched where C++ says it
has been emptied. It asks for the move constructor first when the argument is
an xvalue. This is the same class of bug as the binding one - a path written
before a value category existed, which keeps working and gives the wrong
answer.

**`copyConstructorOf` was answering with move constructors.** It matched any
single parameter that `isReference()`, which was exact until `&&` became a
type in 7.4a. Two faults: the answer depended on which of the two was
declared first, and a copy could be routed to a constructor whose entire
contract is that it is never handed an lvalue. It skips rvalue references
now, with `moveConstructorOf` beside it; and because not finding a copy
constructor is how the implicit one gets declared, a user-declared move
constructor has to suppress that explicitly - which is [class.copy]/7, where
the implicit copy is *deleted*.

**7.4c has landed, and 7.4 with it: the move constructor the compiler
writes.** The divergence it was opened for is closed - a class with a member
whose move differs from its copy now prints `inner move` where it printed
`inner copy`.

[class.copy]/9 gives a class an implicit move constructor only if it declared
none of a copy constructor, a move constructor, a copy assignment, a move
assignment or a destructor - each being evidence the class manages something
by hand, and a memberwise move of such a class is how a double free happens.
**Two of the five cannot be written in this language yet**, so three are
checked; and they have to be read in `declareImplicitSpecials` *before* any
implicit member is declared, because a moment later every one of them answers
yes for a class that wrote nothing.

[class.copy]/15 makes the body a move of each base and each member, which is
`static_cast<T &&>(other.m)` for every one - so the synthesised body marks
each source as an xvalue and 7.4b's binding does the rest. **A subobject with
no move constructor is copied, and that is the language's answer rather than a
shortcut**: `T &&` binds to `const T &`, so moving something that has only a
copy is exactly what its copy constructor does.

**Nothing that calls the synthesiser had to learn about moves.** Which of the
three members is being written is read off the signature - an implicit
constructor whose parameter is `X &&` is the move constructor and there is
nothing else it could be - so `defineImplicitFunctions` is unchanged and the
one function writes all three bodies.

Implicit move *assignment* is provably unobservable here and is not written:
`operator=` cannot be declared at all, so every assignment in this language
bottoms out in memberwise scalar copies, and a move of those is a copy of
those.

**A general difference in the names, found here and not about moves.** On
x86_64-linux clang emits only the C2 variant of any *inline* constructor, and
a constructor the compiler writes is inline by nature with no out-of-line form
to switch to - so every implicit constructor meets it. Measured with an
implicit *copy* constructor, which shows the same C2-only set with no move in
sight. cxx1 emits C1 and C2 everywhere; the two are identical for a class with
no virtual bases and both are mergeable definitions, so the extra C1 costs
object size and nothing else. Darwin emits both and agrees on all 39 names,
Windows on all 20. In `implicit-move.nonames`, and it is the second case to
record it - `move-constructor.nonames` is the same fact reached by defining
constructors inside a class body.

**One thing the case had to be told to do.** `Base` was only ever built as
part of a `Derived`, and clang then has no use for its complete-object
constructor and emits none - which showed up as two cxx1-only C1 names on
Darwin. Building a `Base` directly is what the case was missing rather than
what cxx1 was doing wrong, and all 39 agree once it does.

**What `static_cast` does not do is refused out loud.** Its non-reference case
is handed to `convert()`, the same road the C-style cast takes, which is a
subset - a base-to-derived downcast is a `static_cast` and is not here.
`static_cast<T &&>` of a prvalue is refused too: legal C++, an identity, but
it needs a temporary materialised and there is no path here that does that.

**One measured difference in the names, and it is not about moves.** On
x86_64-linux clang emits only the C2 constructors for `move-constructor.cpp`
and calls them directly, where cxx1 emits C1 and C2 as it does everywhere.
The deciding thing is `inline`: every constructor in that case is defined
inside the class body, and writing the same three out of line makes clang emit
both on that target as well - which is why no earlier case met it. Darwin
emits both either way and agrees with cxx1 on all 8 names, so the mangling is
not in question. Recorded in `move-constructor.nonames`.

**An rvalue reference is the same machine as an lvalue one.** Both are a slot
holding an address, both are read by dereferencing it, so `isReference()`
answers for either and almost nothing outside binding and mangling had to
learn it exists. What differs is what may bind: `T &&` takes a value with
nowhere to live, `T &` an object that has somewhere.

**Which reference takes an argument is a question about the argument, not
about a conversion.** An rvalue reference is not viable for an object with an
address; where both are viable it is the better match. Those two lines in
`rankArgument` are the whole of how a move gets chosen over a copy.

Measured: Itanium writes `O` where an lvalue reference is `R`, and the
Microsoft ABI `$$Q` where it writes `A`.

**7.5 - `constexpr`.** The constant evaluator has been here since rung 1 -
`fold` is what array bounds and enumerators use - so `constexpr` on a
*variable* is close to `const` with a required constant initialiser. A
`constexpr` *function* is the real work: evaluating a call at compile time
means an interpreter over the AST, which is a second execution model beside
the backends.

**7.5a has landed, and most of it was not about `constexpr` at all.** The
plan above says a constexpr variable is `const` with a required constant
initialiser, and that was true - but it assumed the `const` half already
worked, and it did not. `fold` had no case for a *name*: it folded literals
and operators and stopped at the first identifier. So

```
const int n = 4;
int a[n];          // "expected an array length, and this is not an integer
                   //  constant expression"
```

and the same refusal for a case label, an enumerator value, a non-type
template argument and a nested `const int m = n * 2;`. Five doors into the
same evaluator, all shut. **[expr.const]/3 - a const object of integral type
initialised with a constant expression is a constant expression - is a rule
this compiler simply did not have**, and it is the rule that lets C++ write
`const int n = 4; int a[n];` where C reaches for a macro or an enum.

So the work was to give `Local` and `GlobalSym` a value to remember and teach
`fold` to read a `Var` back out of them. Locals before globals, the shadowing
order every other lookup uses. **The object is not folded away**: it still
exists, still has an address, and `&n` still works - what is answered is what
it is worth when read. Both suite cases take the address of a constant to say
so, and there is a second reason to: clang folds every use and emits no
symbol at all, so without an address taken the two disagree about what a
translation unit contains.

On top of that, `constexpr` itself is small. It sets `isConst` and one flag,
and the flag carries a *demand* rather than a meaning: an initialiser that is
not constant is an error where a plain `const` would have taken it and simply
not been usable as a constant afterwards.

**Two things needed saying that the plan did not mention.**
`atDeclarationStart` had to learn the keyword - it is a decl-specifier that
names no type, so without it `constexpr int n = 4;` inside a function went to
expression parsing and the reader was told an expression was expected, at a
keyword beginning a perfectly good declaration. And a `constexpr` *function*
is refused by name until 7.5b: accepted quietly it would compile and run
correctly, since a constexpr function may always be called at run time, and
would then fail to be constant in the one place the keyword was written for.
Finding it is a question about the token after the declarator, not about the
type - at that point the type is still the *return* type and the `(` has not
been read.

**A names-suite trap, and it is the assembler's.** `width` is a MASM reserved
word, so the MASM backend emits `$width` - deliberately, invisible to a
program, and stopping at the import boundary. `tests/names.sh` filters
`$`-prefixed symbols, so the case reported a difference that was about the
assembler and not about `constexpr`. The case renames the variable and says
why.

**7.5b has landed, and rung 7.5 with it. The plan called it "the real work:
an interpreter over the AST", and it is not one** - because of a restriction
this compiler's target version happens to impose. **C++11 lets a `constexpr`
function body be one return statement and nothing else**: no local, no loop,
no second statement. So running the function is *folding one expression* with
the parameters standing for the arguments. There is no program counter, no
statement dispatch and no state to carry, and recursion falls out of `fold`
already being recursive. C++14 relaxed the rule, and had this compiler
targeted C++14 the plan would have been right.

What it took was three things. `fold` gained a `Call` case: look the callee up
by its **mangled symbol**, fold the arguments, push a frame, fold the stored
expression, pop. A frame stack, because inside a call a local `Var` names a
parameter of a function whose frame does not exist - what it is worth is what
the call was given. And the expression itself, kept as a `const Expr *` into
the `Function` the `Program` owns, recorded before the body is moved into it.

**The arguments are folded in the caller's frame, before the push.** `fact(n -
1)` reads the caller's `n`; folded after, it would read the callee's parameter
sitting in the same slot. And there is a depth limit of 256, said out loud
where it is reached - [expr.const] lets an implementation have one, and a
compiler that recurses forever instead is worse than one that says no.

**Two mangling consequences, both measured against clang and neither
guessable.**

`constexpr` sets `isConst` because on an *object* that is what it means, and
on a **function** the const has to come back off: cl and clang both spell
`constexpr int sq(int)` as `?sq@@YAHH@Z`, which is `H` for `int` and not `?BH`
for `const int`.

And **a `constexpr` non-static member function is implicitly `const` in
C++11** - clang spells `Board::twice` as `_ZNK5Board5twiceEi` where cxx1 spelt
it `_ZN5Board5twiceEi`, and two object files with those names would not have
found each other. C++14 removed the rule, which is why clang *warns* about it
under `-std=c++11` rather than being silent, and why a compiler pinned to
C++11 has to keep it. It has to be applied in **two** places - the class body
and the out-of-line path - because a member defined inside its class is
declared by the first and defined by the second when its held body is
replayed, and one without the other makes a class refuse its own member.

**7.6 - lambdas**, last because they need the most from the rest: a closure is
a class with a call operator, generated where the lambda is written, holding
its captures as members. Both of the things it was blocked on now exist -
`operator()` is reachable, and a class can be defined inside a function; see
"Operator overloading" and "Local classes" below. **What is left is the lambda
itself**: the closure type, its captures as members, and the body as the call
operator's. Captures by reference then need 7.4's story about lifetime, and a
generic lambda would want `auto` in a parameter, which is C++14 and out of
scope.

## Operator overloading, and why the names came first

**Off the ladder, and the reason it is next.** The ladder was never a map of
C++, and what is off it is now larger than what is on it. Operator overloading
blocks more than its size suggests: user-written copy *and* move assignment,
functors and therefore **lambdas (7.6), which cannot be attempted before it**,
and iterators and therefore range-for over a class. So it comes before the
last rung rather than after it.

**The table is in `src/Operator.h` and `src/Operator.cpp`, and every code in
it was measured.** One class declaring every overloadable operator, compiled
for both ABIs with clang, twice - and the second time was not optional.
Measured with the operators all taking the same argument, **half the Microsoft
codes are unreadable**: `??8`/`??9`, `??M` through `??P`, `??A`/`??R` and
`??Y`/`??Z` each come out as two identical signatures differing in one letter,
with nothing in the listing to say which letter belongs to which operator. The
second measurement gave every operator a *different* parameter type - char,
short, int, long, float, double, and the unsigned four - so each name
identifies exactly one row. The parameter type each row was measured with is
in the comment beside it, which is what makes a row checkable later.

**The one asymmetry between the ABIs is arity.** Itanium gives the unary and
binary forms of a token different codes - `ml` for `a * b` and `de` for `*p`,
`an` and `ad`, `mi` and `ng`, `pl` and `ps` - and Microsoft writes one code for
both and lets the parameter list separate them: `??D` is multiplication *and*
dereference. So an Itanium name cannot be built without knowing how many
operands were written and a Microsoft one can, which is why
`itaniumOperatorCode` takes a flag and `operatorPrefix` does not.

**The code goes exactly where a name's length and letters go**, and nothing on
either side of it changes: `_ZNK1VplERKS_` against `_ZNK1V3addERKS_`. That is
what made this small. `operator=` had been mangled by hand since rung 3 -
`aS` and `??4`, the one operator the compiler could name - and it turns out to
have been the general rule all along.

**One Microsoft rule that cannot be guessed and was measured**: the operator
code replaces the whole `?name@` and is **not pushed as a back-reference**. In
`??HV@@QEBA?AU0@D@Z` the class is back-reference *0*, where a named member
function would have left it 1. The `??4` of `operator=` already followed that
rule; it is written once now.

**Verified end to end, not just built**: `tools/mangled-names` over the
measurement files puts cxx1's names beside clang's for all three targets - 31
names for the binary set, 7 for the unary forms, 30 for a mixed file with
`operator=`, `->`, and pre- and post-increment. All agreeing, on
x86_64-linux, x86_64-windows and arm64-darwin.

### The dispatch is one place, because the expression parser already had one

Every binary operator in the parser funnels through `arithmetic`, `comparison`
or `shiftOf`, so `overloadedBinary` is asked once in each of those three and
nowhere else. It answers null when neither operand is a class - which is every
use in a C program and most in a C++ one - so the built-in paths reach their
work having asked one question about a type and nothing about operators.

**A member operator is looked for on the left operand only.** The left operand
is what [over.match.oper] hands the implicit object parameter, so `2 * v` can
never reach `V::operator*` however the class is written. That is not a
limitation; it is why the non-member form exists, and
`tests/cases/nonmember-operator` holds both halves of it.

**`memberCall` had to be split in two.** It read its own arguments off the
token stream, which is right for `a.f(b)` and impossible for `a + b`, where
the right operand was parsed long before anything knew there was a call here.
`memberCallWith` is the same function with the arguments handed to it.

### Two bugs this found, and only one of them was new

**`a += b` is not `a = a + b` when `a` is a class**, and for about ten minutes
it was. A compound assignment is built by reading the target back, combining
and storing, which is the correct rewrite for a built-in operand. The moment
`+` learned to find a class's `operator+`, the rewrite found it too - so
`a += b` compiled into a call the standard does not sanction, for a class that
had never declared `operator+=`. clang refuses the same program.
`tests/cases/operator-compound-refused` is that case. **The lesson is the
shape of it**: teaching an existing path a new trick teaches it to every
caller of that path, including the ones that wanted the old behaviour.

**Unary `+` accepted a class, and had since before any of this.**
`if (consume("+")) return decay(castExpr());` - a no-op with no check on what
it was applied to, which made `+v` the one operator that took a class in
silence. Found by running every operator over a class operand and putting
cxx1's accept-or-reject beside clang's, which is a sweep worth repeating
whenever a new operand type arrives.

### What is refused, and where

**An operator this compiler can name but cannot reach is refused at the
declaration**, not at the use - because a function that links, carries the
name clang gives it, and can never be called is exactly the half-built thing
this project refuses everywhere else. Which dispatch is missing depends on
arity, and arity is a question about the parameter list, so the check sits
where the parameters are known and not back where the name was read:
`operator-` with a parameter is subtraction and works, with none it is
negation and there is no path to it.

Reachable now: the sixteen binary operators `+ - * / % & | ^ << >>` and
`== != < <= > >=`; every **unary** form - `+ - * & ! ~` and `++ --` in both
their prefix and postfix spellings; and `operator()`.

Refused by name: `= [] , && ||` and the compound assignments; `operator new`
and `operator delete`; `operator->*`; a user-defined literal; and a conversion
function.

### The unary operators, and the two things that are not arithmetic

**`&` is an overload only where the class declared one.** A class that did not
still has an address, and `&obj` is the address-of it has always been - so
`overloadedUnary` answers *null* when it finds no candidate and each built-in
path is reached unchanged, rather than refusing because an operand was a class.
The same shape covers `*` on a pointer to a class. Only where a candidate
exists does the operator become a call.

**The postfix increment's dummy `int` is a real parameter, not a marker.**
[over.inc] gives the postfix form an extra int and passes 0 in it, which is the
whole of how `v++` is told from `++v` - and it is why `operator++(int)` is
written with a parameter nobody names. So the argument is *built* at the call
site, and postfix is then the ordinary two-operand resolution while prefix is
the one-operand one; one function answers both. It is also the one operator
whose arity lies to `checkOperatorDeclarable`, which counts two operands for a
member `operator++(int)` and has to let it through by name.

### `operator()` is the simplest of them, and the one 7.6 was waiting for

**[over.call] makes it a non-static member function and gives it no non-member
form** - alone among the overloadable operators - so the whole candidate set is
the class's own and `memberCall`'s ordinary resolution *is* the resolution,
arity and all. It is also the only one with no fixed number of operands, which
is why it is exempt from the arity check rather than listed in it.

**An operator function can be called by its name**, which is [over.oper]/1 and
was refused for as long as operators have worked here. The name was read in
declarators and nowhere else, so in an *expression* the keyword fell through to
the table of words this parser has no rule for and said `'operator' is not
supported yet` about a feature it had. Two places had to learn it: the
member-access paths, which read a name after `.` and `->`, and `primary` -
where the branch has to sit **before** the catch-all keyword refusal, or that
refusal claims the token first. `tests/cases/operator-by-name` holds all five
forms.

**The harder half was not the dispatch.** `v(1)` never reached the postfix
parser: a name followed by `(` was read as a call to a *function* of that name
before anything looked at what the name held, so an object was reported
undeclared. `callsThroughObject` already existed for exactly this - it keeps a
function-pointer variable out of the free-function branch - and a class-typed
name now takes the same route. One line, and it is the line that made the
feature work.

A closure is a class with a call operator, so this is the piece 7.6 was blocked
on. **What remains for lambdas is a local class to put one in**, cxx1 having
none. **The conversion function
is caught in the specifier path and not the declarator**, because
`operator int() const` is a declaration with no type in front of it - reaching
the end of `unqualifiedSpecifiers` having found `operator` is precisely what a
conversion function looks like from there, and saying so beats the generic
"'operator' is not supported yet" that the keyword table would have given.

**The two halves are ranked together, and at first they were not.** Asking
"does the class declare this operator" and only then looking at the
non-members takes the member whenever one exists - which accepts an ambiguity
clang refuses, and refuses a call a non-member could have taken. Both
directions were wrong and both are in `tests/overload/`.

`resolveOperator` builds the one candidate set [over.match.oper] asks for. The
shape that makes it possible: for `a @ b` a member takes `a` as its implicit
object parameter and `b` as its written one, a non-member takes both as
written parameters - two operands either way, so the rank vectors are the same
length and `betterCandidate` needs to know nothing about which half a
candidate came from. It answers *which half won* rather than which function,
and the caller then goes down the member or free path it already had; a member
that beat every non-member also beat every other member, so resolving again
within one set reaches the same candidate.

## `friend`, and why it landed beside the operators

**A friend is not a member, and nearly every mistake here comes from
forgetting that.** [class.friend]: the declaration is written inside the class
and the function it declares belongs to the *enclosing namespace*. It has no
`this`, it is not in the class's function table, it is not mangled into the
class, and `main` calls it exactly as it calls anything else. All the class
gives it is access - so the implementation is an ordinary function declaration
handed to the same `declareFunction` a file-scope one goes to, plus one entry
in a table.

**Two rules fall out rather than needing to be written.** The access specifier
a friend declaration sits under is ignored - [class.friend]/9 - which is true
here because `access` is simply never read on that path; and a friend reaches
private member *functions* as well as private data, because every access check
asks the same new question.

**The table holds linkage names, not source names.** Friendship is granted to
a *function*, and recording `peek` would befriend every overload of it -
including ones declared afterwards that the class never saw.
`tests/cases/friend-overload` is that case: `peek(const Account &)` is a
friend and `peek(const Account &, int)` is refused, which is what clang does.
So the grant is recorded as the symbol, looked up through `lookupSignature`
once the parameter list has been read.

**What asks the question is `currentFunction_`**, the linkage name of the
function whose body is being read, set beside the `declareFunction` that
registers a definition and cleared with `currentClass_`. It is left empty for
a *member's* body on purpose, and that is consistent rather than a gap: the
qualified form that would make a member somebody's friend is refused where it
is written.

**Five access checks, and all five had to learn it** - `checkAccessible`, the
member-call check in `memberCallWith`, the two constructor checks, and the
static-member one. They were already the complete list of ways past a private
member, so `isFriendOf` is asked in the same breath as "are we inside the
class" in each of them. A sixth check added later that forgets this is the
failure mode to watch for; there is no single funnel that would prevent it.

**It landed beside operator overloading because that is what makes each of
them useful.** A symmetric operator wants to be a non-member - `2 * v` cannot
be a member of V, the left operand being an int - and a non-member cannot see
what the class keeps private. `tests/cases/friend-operator` is both halves at
once, and neither feature alone would have carried it.

Refused by name: `friend class X;`, a friend function *defined* inside the
class body (the held-body replay members use would have to put this one back
outside the class it was written in), befriending one member function of
another class (`Other::look` would have to be found before `Box` is
complete), and a friend declaration that declares no function.

## Overload resolution is checked against clang, not against a recorded answer

`tests/overload.sh` over `tests/overload/`, and it is a suite of its own rather
than more cases in `tests/cases/` because the question is different. A case
there carries a `.expected`, which is a decision written down once; here the
interesting question is not "what does this print" but **"does cxx1 choose the
function clang chooses"**, and those are the same question only while somebody
keeps the recorded answer honest. The oracle is asked on every run, so the
corpus cannot drift away from it.

**It compares the verdict before it compares the output, and that half is what
catches bugs.** Overload resolution is as much about refusing an ambiguity as
about picking a winner, so a harness that only diffed the output of programs
that compiled would call "cxx1 accepted what clang calls ambiguous" a pass.
Every file is run both ways: clang refuses it and cxx1 must refuse it, or clang
accepts it and cxx1 must accept it *and* print the same thing. Each overload
returns a number of its own, so identical output means identical choices;
`0 * argument` keeps every parameter used without letting its value reach the
result.

Twenty files, covering the four forms the overloading has to work for -
functions, constructors, operators member and non-member, and friend functions
and friend operators - plus reference binding and value categories from 7.4,
inheritance in both the member lookup and the ranking, an ambiguity for each
form, and one file where all three meet: an overloaded *private* constructor
set reached only from a friend, which is the only place the access check has to
be got past before the ranking can be asked at all. The unary operators, the
two increments and `operator()` joined them as they landed, ambiguities
included.

**It found a real bug the moment it was written**, which is the argument for
it. See the operator-overloading section: the member and non-member halves were
being asked for in order rather than ranked together, and that is wrong in both
directions - it accepted an ambiguity clang refuses, and refused a call a
non-member could have taken. `resolveOperator` replaced it and both directions
are now files in the corpus.

clang is required, so this skips where there is none - the Linux box says so
rather than reporting a pass, exactly as `names.sh` does. It is a Mac suite in
practice, and that is where the mangling oracle already lives.

## Local classes, the last thing standing between here and lambdas

**[class.local]: a class defined in a function body belongs to that function.**
Two functions may each define `struct L` and they are two types, and neither
name is visible outside the function that wrote it. Before this the tag went
into the one table every class shares, so the second function was told its own
class was "defined twice" - and a global class of the same name could not be
shadowed at all.

**The tag is qualified the way a nested class's is**, with the enclosing
function's *source* name, so a diagnostic reads `struct f::L` rather than
spelling a mangled symbol at the reader. Where two overloads of one name each
define the same tag the source name is not enough, so the owner is checked and
a counter added - two classes silently interned as one is exactly the bug being
fixed. The written name is resolved through `localTypes_`, a scope emptied when
the function ends and asked *before* the file's types, which is what makes the
shadowing work.

**A specialization is not a local class even when a function asked for it.**
`Holder<int>` is instantiated on demand, in the middle of whatever named it -
often a function body - and the class it makes belongs to the file. Without
that exception the instantiation was renamed `f::Holder<int>` and the class
stopped being able to find its own constructor.

**Both ABIs wrap the enclosing function's whole name round the member's**,
which is what keeps two functions' `L::get` apart in one object file. Measured:

    _ZZ1fvEN1L3getEv              Itanium: _ZZ <function> E <ordinary entity>
    ?get@L@?1??f@@YAHXZ@QEAAHXZ   Microsoft: ?1? and the whole name, as a scope

Itanium wraps the ordinary name rather than building a different one, so the
member is spelled exactly as it would be outside a function and both names give
up their `_Z` to sit inside the wrapper. The Microsoft form is one more scope
component, and the embedded name is **not** pushed as a back-reference.

**Two owners are shaped differently and both are ordinary in real code.** A
function with no decorated name - `main`, or anything `extern "C"` - is written
by Itanium as a plain length-and-letters component (`4main`, there being no
`_Z` to take off) and by Microsoft as `?main@@9`, the `9` saying the name
carries no type information. A `static` function needs nothing special: its
name is `_ZL4stati` and the L comes along inside the wrapper.
`tests/cases/local-class-main` holds both.

### The replay is a nested parse, and it was eating the enclosing function

A local class's member bodies are held and replayed the moment the class
closes - which is *in the middle of* the function that wrote it, through the
same `topLevel` that sets a function up and clears what it finds. So the
enclosing function lost its parameters and its locals:
`h(int k) { struct M { ... }; M m; m.z = k; }` was told `k` was not declared,
because reading `M::get` had emptied the table `k` lived in. `replayInlineBodies`
saves and restores the lot now - locals, frame vars, scopes, blocks, labels, the
frame size, `this`, and the current class - having previously restored only the
token position.

**And it found a memory bug older than any of this.** The signature a
definition looks up was held as a `const Signature *` into `functions_`, which
is a `std::vector`, while the body was read. Anything declared during that body
can grow the vector and move it. Nothing declared anything there until a class
could be defined inside a function - its member functions are declared exactly
then - and `Outer::m` came out under whatever string happened to be at the old
address: `m` in one build and `a` in the next. Only the symbol is wanted
afterwards, so only the symbol is kept, by value.
`tests/cases/local-class-in-member` is that case.

**A stale object file cost an hour of this.** `git stash` to build the previous
commit and `git stash pop` afterwards leaves mtimes that convince `make`
nothing changed, so the tree was half-old and crashed in a way neither `-O0`
nor ASan reproduced - because those were clean builds. `make clean` before
believing a crash that only one build shows, and see "How correctness is
established", which says the same thing about green suites.

**What is left for lambdas** is the lambda itself: a closure type generated
where the expression is written, with its captures as members and `operator()`
holding the body. The class it needs can now exist, and so can the call
operator.

## Two bugs an audit found that no suite would have

Both came from running plain C++ past the compiler and diffing accept/reject
and *output* against clang - the same method `tests/overload.sh` automates for
one feature, done by hand across the language. Neither was reachable from any
existing case, which is the point: a suite only tests what somebody thought to
write down.

**`S a[4];` ran no constructors at all.** The branch that builds a class local
asks `isStructOrUnion()`, and an array of S is an array - so it fell through to
an ordinary uninitialised local. It compiled, linked, ran, and every element
held whatever was on the stack: clang printed `3 3 3 3` where cxx1 printed
`-16 -1 1803348728 1`. **The only silent wrong answer found in the whole
audit**, and ordinary code.

Why nothing caught it: a class array as a *member* is built by the memberwise
path and always worked, and `new T[n]` is refused by name - only the local
declaration had nothing. `constructLocalArray` runs the default constructor per
element through the loop `eachElement` already provided for member arrays, and
unwraps every array level at once, so `T g[2][3]` is six elements of T rather
than two of `T[3]`.

**Its destruction is refused by name rather than half-built.** [class.dtor]
destroys elements in reverse when the scope ends, and `emitDestructors` is
shared with the exception paths on all three targets - it knows one object per
entry, and teaching it a count belongs with that machinery rather than beside a
declaration. So an array of a class with only constructors works and an array
of one with a destructor says so. Nothing is silently left undestroyed, which
is the failure the whole branch exists to stop.

**Taking a function's address used the written name, not the linkage name.**

```cpp
int g(int n) { return n * 2; }
int (*p)(int) = g;     // emitted `g` where the function is _Z1gi - link failure
int (*q)(int) = &g;    // typed `int (*)(int) *`, which nothing can be assigned
```

A *call* went through the signature and so through the symbol; a function named
as a **value** did not. Correct while this compiler was C, and wrong from the
moment rung 2 mangled anything - `extern "C"` kept working, which is why it
lasted this long. One line: `v->setSymbol(sig->symbol)` beside the `Var::global`
that the name builds.

And `&f` and `f` are the same thing for a function: [conv.func] has already
turned the designator into a pointer by then, so taking its address again built
a pointer to a pointer with no object under it. The test is the *shape* and not
the type - `&p` where p is a variable holding a function pointer is an ordinary
address-of and still is.

## Default arguments, and the scope they are read in

**A default is kept as a place in the token stream, not as a parsed tree.**
[dcl.fct.default]/9 evaluates it afresh at every call that leaves the argument
out, so one tree could not have been handed to two call sites anyway without a
general clone this parser does not have - and re-reading a recorded range of
tokens is what it already does for a member function's held body. The place is
recorded where the parameter list is read and the expression is parsed again at
each call that needs it.

**Three parameter-list parsers had to learn it**, which is the shape of this
file rather than of the feature: `parameterTypes` for declarations, the loop
`topLevel` keeps for definitions, and constructors through `declareConstructor`.
`pendingDefaults_` carries them the short distance to whichever `declare()`
records the function, the way the class-instantiation fields already carry a
tag, and `defaultArgs_` keys them by the **linkage name** so a redeclaration
cannot give one function two sets.

**In `topLevel` the handover sits before the prototype branch and not after
it.** That branch declares the function and returns, and
`int f(int a, int b = 3);` declared there and defined further down is the
ordinary way to write one - put the handover after it and exactly the common
form is the one that does not work.

**The caller's locals are put aside while a default is read**, and this is the
part worth keeping. The expression is parsed at the *call*, so a local in the
calling function could quietly capture a name the default meant globally. A
default at namespace scope may name globals, enumerators and static members and
may **not** name a local or another parameter - so hiding the locals is what the
declaration's scope actually is from here. `tests/cases/default-argument` pins
it with a `shadow()` whose local `base` shadows the global the default uses.

**Overload resolution now ranks a candidate against fewer arguments than it has
parameters**, the count a call may give being a range: `leastArguments` counts
the parameters with no default, and only the arguments actually written are
ranked - a default is the same expression for every candidate that has one and
cannot tell two of them apart.

[dcl.fct.default]/4 - the defaults must be a suffix - is one shared check called
from both parameter-list parsers, because a definition may carry them where the
declaration did not.

## A by-value parameter and a reference to it are the same match

**`f(int)` beat `f(const int &)` for an lvalue without a word**, where clang, g++
and cl all call the pair ambiguous. [over.ics.rank] gives neither a way to win:
one copies the argument and the other binds it, and both are the identity
conversion. The same for a prvalue, `f(4)`.

**Why it was wrong is the interesting part.** A reference binding is charged a
*qualification* conversion here for the const it adds - and that charge is not
a mistake, it is how two tiebreaks are encoded in one number: [over.ics.rank]
/3.2.6, the less qualified of two references winning (`hold(int &)` over
`hold(const int &)` for an lvalue), and /3.2.3, an rvalue reference beating a
const lvalue reference for an rvalue. Both only ever compare two *references*.
Against a by-value parameter the charge is not a difference at all, and letting
it decide is what made `f(int)` win. So `betterCandidate` neutralises it in
exactly that pairing - `sameMatchEitherWay`, a by-value parameter and a
reference to the same type - and the encoding stays for the two comparisons it
is right about. Deleting the charge instead was tried first and cost the
rvalue-reference preference: `g(S &&)` stopped beating `g(const S &)` for
`static_cast<S &&>(a)`, which the overload suite said at once.

**The neighbour found a second defect, and it was the worse one.** Writing the
case that had to keep resolving - `hold(int &)` against `hold(const int &)` -
showed `hold(7)` coming out *ambiguous* where all three oracles pick the const
one: a mutable reference was being ranked as viable for a temporary. With one
candidate it never showed, because the call path refuses the binding later and
by name; with two it did. `T &` is not viable for a value now, at ranking, which
is where clang says "no matching function".

Both are in `tests/overload/`, where clang is asked on every run rather than a
recorded answer: `ambiguous-value-vs-reference.cpp`, which both must refuse, and
`reference-constness.cpp`, which both must resolve the same way. **All three
oracles agree on every shape here** - measured on g++ 11.5 and cl 19.44 as well
as clang - which is a different situation from the const rule beside this one,
where cl stands apart.

## A const object has to be initialised, and the line is CWG 253's

**`const S s;` on a plain struct compiled and left the object holding whatever
was there**, which is [dcl.init]/7's whole subject: a const object that nothing
initialises can never be given a value afterwards, so the declaration is
ill-formed rather than merely unwise. It is refused now, at a local and at file
scope alike, `extern` excepted because that declares rather than defines.

**Where the line falls was measured against all three, fifteen shapes, because
the paragraph's letter and what compilers do are not the same.** [dcl.init]/7
says "a class type with a user-provided default constructor"; clang applies CWG
253, which asks instead whether anything would be left unset - **and g++ answers
identically on every one of the fifteen**, which is what made it safe to take:

```cpp
struct A { int a; };                 const A a;   // refused, and clang refuses
struct B { int a; B() : a(1) {} };   const B b;   // a constructor of its own
struct C { int a = 5; };             const C c;   // every member initialised
struct D { B b; };                   const D d;   // a member that answers for itself
struct E : B { };                    const E e;   // and a base that does
struct F { int a = 1; int b; };      const F f;   // refused: b is left unset
struct G { B b; int n; };            const G g;   // refused, for the same reason
const int n;                                      // refused
const A arr[2];                                   // refused
```

**cl is looser, in exactly three of the fifteen, and cxx1 does not follow it.**
Measured with cl 19.44 at `/std:c++14 /permissive-` - it has no C++11 mode to
ask: a bare POD local (`const A a;`), an array of one (`const A arr[2];`) and a
POD at file scope are all accepted there and refused by clang, by g++ and here.
It refuses `const int n;` and the mixed cases as they do, so its rule is closer
to "a class is fine, a scalar is not" than to CWG 253's walk. Where a *language*
rule has the two Itanium oracles agreeing against cl, this tree follows them and
records the difference - which is the opposite of how an *ABI* question is
settled, and worth keeping straight: cl is the authority on the Microsoft ABI
and no authority at all on what C++ means.

`constDefaultInitialisable` is that walk, and it is recursive because the rule
is: a user-provided constructor answers for the whole class, and otherwise every
base and every member has to answer for itself. A member the layout copied down
from a base is skipped, since the base already answered - `memberFromBase` is
that test, which the destructor walk had been spelling out inline.

**The refusal names two spellings that work.** `const S s = S();` and
`const S s = {};` both compile - the second only since 2026-09-03, and until
then it was the first thing a reader tried after this refusal and the second
wall they met. See "An empty pair of braces is value-initialisation" below.

## An empty pair of braces is value-initialisation

`{}` is not a list with nothing in it. [dcl.init]/11 sends it to /8, the
paragraph `T()` already followed here, and the two are the same thing written
two ways. Implemented 2026-09-03; before that every spelling of it was refused,
and by three different messages: one about lists needing a value, one about
list-initialisation, and for the rest `expected ';'` two tokens later.

Where it is read: an initialiser after `=` - `int n = {}`, `P p = {}`,
`const P p = {}`, at file scope, on a static local, on a static data member -
the direct form written with no `=` (`P p{}`, `int n{}`, `C c{}`), and
`new T{}` and `new T[n]{}`.

**The two halves of /8 are the whole of the work.** For everything without a
constructor it is a zeroing: `initZero` says it in a function, and at file scope
it is said by emitting no piece at all, which `segmentFor` already reads as
`.bss`. For a class with a constructor, value-initialisation *calls* it - and
zeroes the object first only where that constructor is implicit, since one
somebody wrote is the whole of the initialisation. `constructLocal` carries that
half, spelled as `ParserExprNew` had already spelled it for `T()` and `new T()`.

| written | what happens |
| --- | --- |
| `struct U { int a; int b; U() { a = 1; } }; U u = {};` | `U()` runs, and `b` is left as it was |
| `struct I { int a; int b; }; I i = {};` | zeroed, then the implicit constructor |

Measured on the arm64 assembly rather than argued: the first calls one function
and stores nothing, the second stores two zeroes and then calls.

**`= {}` is copy-initialisation and `{}` is not**, which is the whole of what an
`explicit` default constructor changes - clang refuses `C c = {};` there and
takes `C c{};`, and so does this.

**What is still refused, and by name.** Braces with a value in them are
list-initialisation - `P p{1, 2}`, `new P{1}` - which this compiler does not do.
The refusal says so and names `= {...}`, which for everything it reads braces on
means the same thing; before this it was `expected ';'` two tokens later.
`int a[] = {};` is refused because `{}` counts nothing and an array of no
elements is not C++ - clang calls that an extension and refuses it under
`-pedantic-errors` too. A member's own initialiser written `int a = {};` keeps
the refusal it had: that one is replayed as an expression once per constructor,
which is a different mechanism from any of the above. `auto x{}` now joins
`auto x = {}` in being refused for the reason that is true of it - deducing from
braces wants an `initializer_list` - where it used to be told it had no
initialiser at all.

`tests/cases/value-init-empty-braces.cpp` runs twenty of them,
`list-init-values-refused.cpp` and `empty-braces-no-length-refused.cpp` pin the
two refusals.

## An enumeration keeps its name, which is half of being a type

**`void f(Colour)` was `_Z1fi`**, so no cxx1 object naming an enumeration could
link against one from another compiler - and that is what blocked a
file-by-file differential against a clang build of the same sources, which is
the tool that then found a miscompilation in one file out of sixteen.

**An enumeration is now a distinct `Type` that is an `int` in every respect
except its name.** `Kind::Int`, the same size and alignment and conversions and
arithmetic, interned per tag, carrying the qualified name that only the
manglers read. Both spellings measured, four shapes each:

    void a(Colour)          _Z1a6Colour           ?a@@YAXW4Colour@@@Z
    void b(cc::BinaryOp)    _Z1bN2cc8BinaryOpE    ?b@@YAXW4BinaryOp@cc@@@Z
    void d(Colour, Colour)  _Z1d6ColourS_         ?d@@YAXW4Colour@@0@Z

Two details neither ABI makes obvious. Itanium spells an enumeration *exactly*
as it spells a class - the `N...E` appears for a namespaced one for the same
reason, and the whole name is a substitution candidate, which is what makes the
repeat `S_`. Microsoft writes `W4` where a class writes its `U` or `V`, and the
repeat is the argument table's digit - so an enumeration has to **take a slot in
that table**, which it did not, its kind being `Kind::Int` and the gate asking
`microsoftBuiltin`.

**What this is not is a type.** Underneath it is still an integer, so
`Colour c = n;` for an `int n` is still accepted where C++ requires a cast.
`docs/CONFORMANCE.md` says so plainly and says what finishing it costs: a
`Kind::Enum` carrying its enumerators, conversions in both directions, and
overload resolution ranking them. Held back deliberately - the ABI was what
blocked the differential, and opening the type system is its own round.

`nested-enum` lost its `.nonames` and `.nocl` with this: it had recorded the
old divergence on all three targets and now agrees name for name.

## A base's members were built twice, and the second time was wrong

**The layout copies a base's data members down into the derived class's list** -
that flattening *is* the layout - and the base's own constructor has already
built them by the time the derived constructor's body runs. Both constructor
walks built them again, default-constructing over whatever the base had set.

So a base whose constructor does work through a member that has a constructor
was correct alone and wrong the moment anything derived from it:

    Base b;      // cur holds what Base's constructor put there
    Derived d;   // cur holds its default-constructed state

**A plain `int` member hid it for a long time**: there is nothing to rebuild, so
the second walk writes nothing and the bug is invisible. It needs a member with
a constructor of its own to show at all.

**The destructor walk has skipped base members since implicit destructors
landed.** `memberFromBase` is that test and the comment beside it says why - a
base's own destructor deals with what it brought. Neither constructor walk asked,
and there are two: the one that synthesises an implicit default constructor, and
the one that fills in what a user-written constructor's initialiser list left
out. Both ask now.

**This is what made cxx1's build of Compiler++ reject every program.** Its
parser derives from a base whose constructor reads the first token into a
member, and that member was default-constructed again straight afterwards - so
the parser always saw `TOK_UNKNOWN` at line 0, whatever the input, including an
empty file.

**How it was found is the part worth keeping.** Seven probes of the shape had
already come back correct, each a guess about a program of sixteen files. What
found it was building the *same sources* with clang and cxx1 and diffing the
behaviour, then narrowing by writing a harness that called Compiler++'s lexer
directly - which proved the lexer right and moved the search to the parser.
The differential is the oracle; the probes were only ever hypotheses.

    128 of Compiler++'s own 129 test cases now behave identically under both
    builds. The remaining one aborts, and is its own finding.

## Weak definitions, and the link that follows from them

**781 duplicate symbols became none**, and Compiler++'s sixteen objects became
a program. Three kinds needed the marker, and finding them was a matter of
running the linker three times.

**An inline function.** [dcl.inline]/6 makes a member defined inside its class
implicitly inline, and an inline definition may appear in several translation
units - so the linker folds the copies rather than rejecting them. Everything
that reaches `topLevel` through a *replay* is one, which is exactly the set:
a member written inside a class body, and every member of a template
specialization, are replayed the same way. `replayingInline_` is that flag.

**A compiler-written special member.** [class.copy] and its neighbours make the
implicit definition inline too, and those never go through `topLevel` at all -
they are built directly, at five sites in `ParserClass.cpp`.

**And the three objects a polymorphic class emits**: its vtable, its typeinfo
and the typeinfo's name string. Those are data rather than code, so the flag
went on `Global` beside `prefixWord` and the globals path emits the same marker.

The spellings were already written down, measured from clang, in
`docs/CONFORMANCE.md` - `.weak` on ELF, `.weak_def_can_be_hidden` on Mach-O,
both beside the `.globl` rather than instead of it. The Spelling base class
answers with nothing by default, so a target that has not been measured says
nothing rather than something wrong: **Windows is that target today.**

### `std` is written `St`, and the bug was invisible twice over

**`std::string` is `St6string`, not `N3std6stringE`** - the Itanium ABI gives
`::std::` one of its predefined abbreviations. Every cxx1 object that named a
type from its own headers had the wrong symbol.

It was invisible in a single-file program, and invisible in an all-cxx1 link,
both being self-consistent. It shows the moment a cxx1 object meets a clang one
- and `tools/mangled-names` skips exactly the cases that would have caught it,
because a case including a C++ library header is compiled by clang against
*clang's* library and the two symbol lists have nothing in common. A `.nonames`
that says "there is nothing to compare here" is right about the library and was
hiding a fact about the ABI.

Two of the four measured rules are not guessable: `std::deep::inner` is
`NSt4deep5innerE`, the wrapper coming back once the name is deeper than one
component; and `f(std::string, std::string)` is `St6stringS_`, which says `St`
takes **no numbered slot** - the whole of `St6string` is candidate zero.

**A specialization is still spelled without it**, and `docs/CONFORMANCE.md`
records why: templates are keyed by their bare name, so `std::vector<int>` does
not know which namespace it came from. That is the same root as the local
`count` that lost to `std::count`, met from the other side.

## All sixteen of Compiler++ compile, and the link is a different question

**16 of 16 sources compile and assemble** as of 2026-09-04. That is worth
stating precisely, because it is not the same as "cxx1 compiles Compiler++":
the objects do not yet link, and nothing has been run.

The last source wanted three things, and only the first is about templates.

**A statement may begin with a type.** `std::vector<T>().swap(v);` -
`atDeclarationStart` saw a type name and claimed the line, so the declarator
path wanted a name where the `(` was. An **empty pair** after the type is what
says otherwise: a declaration needs a name between the type and the `(`, so
`T ();` could only be a function declaration with no name. `int (*p)();` has a
`*` there and is untouched.

**The walk over a qualified name stops at the `<`**, because for its own purpose
the name is what matters - so the empty pair was looked for at the wrong token.
`qualifiedTypeEndPastArgs` steps over the argument list by depth, counting a
`>>` as two, and both callers ask it.

**And a class template-id is a temporary where a plain class name already was.**
`refuseTemplateId` answered for every template in an expression; a class
template followed by `(` reaches `classTemporary`, the same function `P(1)` has
always used.

### What the link found, which no single-file case could

**A base's implicit default constructor was never defined.** `struct Decl :
Node { virtual ~Decl() {} };` has no constructor of its own, so the implicit one
runs and it is not trivial - something stores the vptr. A user-written derived
constructor that names no base calls it, and that path took the signature **by
value** where the branch beside it goes through `resolveOverload`, which marks
the table's entry used. The copy was marked; the table's entry was not; and
`defineImplicitFunctions` never gave it a body. It compiles, it assembles, and
the linker says `Undefined symbols: cc::Decl::Decl()`.

**It links until something derives from a class with a vptr and no constructor
of its own**, which is why a suite of single-file cases never saw it - every
case that could have was written with a constructor. This is the argument for
linking as an oracle rather than compiling: `tests/emit.sh` stops at assembly by
design, and an undefined symbol is invisible to it.

**And what stops the link now is recorded rather than new**: 781 duplicate
symbols, every inline member of every header in every translation unit.
`docs/CONFORMANCE.md` has had "an inline member function is a strong symbol"
since inline members landed - cxx1 has no COMDAT and no way to say a definition
is mergeable. A single-file program never meets it. Sixteen that share headers
meet it 781 times.

## `Base::f(...)` - the version an override replaced, and who may call it

**[expr.call]/1: naming a function with a qualified-id suppresses the
dispatch**, which is the whole reason an override writes `Base::f(...)`. Two of
Compiler++'s sixteen sources do, as `cc::Lowering::lowerDecl(d)`.

Three things had to be true, and the branch that already existed for a *static*
member function is the shape they were built on - it claims the name only where
the set holds a static, and refuses a non-static one by name, which is exactly
the door this came through.

**The lookup does not walk up.** A qualified call says which class's version it
means, so `findMemberOwner` is not asked and `memberCallWith` takes the owner as
given - one parameter, which also turns the dispatch off.

**The base's own name has to resolve.** `cc::Base::f` is in the type table;
plain `Base::f` is the **injected class name**, which is not - so the bases are
walked and their `localName()` compared. That is the same half the
mem-initialiser list needed one commit earlier, and finding it twice in two days
is the argument for a shared helper the next time it comes up.

**And protected reaches further than private does.** [class.access.base]/5 lets
a derived class name a protected member of its base, and `insideAccessOf` asked
only "are we inside that class" - so a protected static called unqualified from
a derived member was refused. It takes the member's access now, because private
and protected are different questions and answering them with one test made the
stricter answer wrong. A private member of a base is still refused, which is the
half worth testing.

## A base named with its namespace in a mem-initialiser list

**`: cc::Lowering(module, l, d)`** is how a class writes a base that is not in
scope unqualified, and both spellings were refused. The qualified one read a
single identifier and then wanted a `(`, finding `::`. The unqualified one -
`: Base(v)` for a `cc::Base` - resolved nothing, because the base's tag is
`cc::Base` and the entry was compared against it as a string.

**The comparison by type was already there and could not answer.** It was added
when a base in a namespace first appeared, and it needs `findTypedef(entry)` to
resolve the written name - but inside the derived class `Base` alone is the
**injected class name**, which is not something the type table holds. So the
base's `localName()` is compared as well, that being exactly what the injected
name is.

The list reads a qualified name now, and either spelling arrives at the same
base. What goes in the map is the tag either way, because that is what the walk
over the bases looks it up by - the string is the key, and the matching is the
part that had to stop being a string comparison.

## A local named `count` lost to `std::count`

**`findTemplate` keys every template by its bare name on purpose**, so that
`std::vector` finds `vector`; the note there says two namespaces cannot each
have a template of one name anyway, and that widening what can be *named* does
not widen what can be *declared*. True, and it had a second effect nobody
looked for: an **unqualified** `count` found `std::count` from anywhere, with
no using-directive in sight.

So including `<algorithm>` broke any program with a local called `count`,
`find`, `swap`, `min`, `max`, `sort`, `fill`, `copy`, `equal`, `merge` or a
dozen more - a wide class of ordinary programs, and one of Compiler++'s sixteen
sources stops on `count = pop().i;`.

**The fix is one condition and not a re-keying.** [basic.lookup.unqual] gives
the nearest declaration, so a name declared as an object is not a template
name. Re-keying every template by its namespace is the other repair and a much
larger one; this is the rule the standard states, and a program with both a
template and an object of one name in one scope is ill-formed anyway.

**Only unqualified lookup gets the test, and the first attempt got that
wrong.** The namespace branch consumes `std::` and then asks about the bare
name, so testing there killed `std::count(...)` beside a local `count` - which
is exactly what a program writes, and what the case now pins. A qualified name
has said which namespace it means and nothing local can be intended.

## Two declarators, one declaration, and a `continue` in a `do`/`while`

**`std::string a, b;` was refused** with `expected ';'` pointing at the second
name - as ordinary a line of C++ as there is. The entry in the table above
blamed the constructor *arguments*, because `P a(1), b(2);` is the shape it was
first written down in. It was never about the arguments: `P a, b;` failed the
same way, and so did every class with a constructor.

The declarator loop is a `do` / `while (consume(","))`, and the three branches
that build a class ended with

    if (!consume(",")) break;
    continue;

**`continue` in a `do`/`while` jumps to the condition**, which consumes a comma
of its own. So the comma was taken twice, the loop ended, and the second
declarator was left for `expect(";")` to trip over. The comma belongs to the
condition; the branches just `continue`.

Worth the section for the shape rather than the size. A `continue` that means
"next iteration" in a `for` or a `while` means "test the condition now" in a
`do`, and where the condition has a side effect the two are different
programs - which is why the fault reached three branches written at three
different times and none of them looked wrong on its own.

## A class template's member is instantiated only where it is called

**[temp.inst]/2: instantiating a class template instantiates the declarations
of its members, not the definitions.** A body is compiled only where something
calls it - which is exactly what lets `std::vector<T>` hold a `T` with no
default constructor while still *declaring* `vector(size_type)`, whose body
says `T()`.

cxx1 gated a replayed body on the member's **name**, and every constructor of a
class shares one key. So `Box<NoDefault> b;` marked that key used and replayed
*every* constructor's body with it - the one nothing called was compiled for a
`T` it cannot be compiled for, and the class would not instantiate at all.

**It was invisible until a second constructor existed.** Every class template in
the tree had one, so name and overload were the same question; adding
`vector(size_type n)` to `include/vector` broke four of Compiler++'s sixteen
sources at a stroke, none of them naming a constructor. The gate is the
overload now: each body carries the index of the signature its declaration
added, and falls back to the key where the declaration added none - a lambda's
call operator, which is recorded without one.

**And the element storage is raw**, which is a constraint rather than a
shortcut: placement new is refused by this compiler, so `vector` has no way to
construct an element in place and assigns into `calloc`'d memory instead. A
zeroed slot is therefore a real state for a `string` - null buffer, zero length,
zero capacity - and `reserve(0)` did nothing for one, leaving the
`buf_[len_] = 0` that follows every write to go to a null pointer.
`std::vector<std::string> s(2);` crashed for that and nothing else.

## A private nested type as a member's own return type

**[class.access]/6: a member's definition may name its class's private types**,
and the return type is written before the `C::` that says whose member it is.
`VM::Value VM::pop()` reads the type first, when nothing yet says this is a
member of VM - so `insideClass` answered no and the class was refused a
definition it had every right to write.

**The declarator ahead is asked instead**, which is the only thing that knows.
The scan stops at the first `(`, `;`, `{` or `=` at depth zero, so it cannot
wander past this declaration, and it takes the **longest run of `A::B::` that
names a type** - `Outer::Inner::f` asks about `Outer::Inner` rather than
stopping at `Outer`, which is the same longest-prefix rule `specifiers` already
follows. A class nested inside the owner counts, because its members reach the
owner's private names exactly as the owner's own do.

**What it must not do is let anyone else in**, and that is the half worth
testing: `VM::Value v;` in a function, `VM::Value other();` at file scope and
`sizeof(VM::Value)` are each still refused, and clang refuses all three too.
The look-ahead answers "is this the definition of a member of that class", not
"is that class mentioned nearby".

## `?:` as an lvalue, which was a missing shape rather than a missing rule

**[expr.cond]/4: a `?:` whose arms are glvalues of one type is a glvalue.** So
`int &r = p ? a : b;` is an ordinary reference binding, `(p ? a : b) = 20` an
ordinary assignment, and `&(p ? a : b)` an ordinary address. All three were
refused here from rung 2 on, and the refusal said the compiler could not build
one - which was true and unhelpful.

**The shape is the addresses.** `*(c ? &a : &b)` puts two pointers where the
arms were, which every backend already moves, and the dereference around them
is an lvalue: assignable, addressable, bindable. Nothing was added to any code
generator, which is the trade this file keeps recommending - where C++ adds a
*category*, look for an existing operation to lower it to.

**It is asked before the class lowering**, and that order is the whole of the
correctness. The class path copies both arms into a slot of its own; for two
lvalues of one type there is nothing to copy, and copying would take the
binding away again - `p ? x : y` would name a temporary rather than `x`, and
writing through the reference would reach nothing at all.

The types have to match exactly, cv-qualification included: a difference there
makes them different types, and [expr.cond]/5 sends those to the prvalue answer
the class lowering gives. What is left reaching the reference-binding path has
arms that are not both lvalues of one type, so its message says which of the
two rules applies rather than what the compiler cannot do.

## `long long` is a type of its own, and the streams had never heard of it

**`<ostream>` had insertions down to `unsigned long` and no further**, so a
64-bit value written through `<<` reached every arithmetic overload by a
conversion and none of them better than the rest. The call was ambiguous, and
the diagnostic listed seven candidates without naming the one that was
missing - which is the shape to remember, because a list of candidates answers
"which of these" when the question was "why is none of them right".

**On both Itanium targets `long` and `long long` are the same width**, which is
exactly why this was easy to miss: every value round-trips, every `sizeof`
agrees, and nothing looks wrong until overload resolution has to choose. They
are still distinct types, and [over.ics.scs] ranks a conversion between them
like any other.

A program that pins its own integer width writes this all day. Three of
Compiler++'s sixteen sources stopped here, all printing a `vmword` - which is
`long long` on everything but MSVC, where the same header spells it `__int64`.

The extraction pair went in with it, for the same reason: `in >> x` for a
`long long` lvalue had the same seven-way tie. `stream-long-long.cpp` runs a
value no `double` holds exactly and no 32-bit type holds at all, so a
conversion to any other overload would show.

## A converting constructor on `return`, and the `?:` that looked like its twin

**[stmt.return]/2 copy-initialises the returned object**, and a converting
constructor is part of that: `return "v";` from a function returning
`std::string` is `string("v")`. `userConversion` had done this for a by-value
argument since conversion functions landed, and `return` simply never reached
it - one call, two of Compiler++'s sixteen sources.

**`?:` looked like the same fix and is not.** Converting the arms the same way
made `c ? "_" : name` compile and then **abort at run time**, on three more
sources. A class-typed `?:` works here only where both arms are lvalues of one
type - `b ? s : t` - because `Conditional` yields a value the backends move as
a scalar, and a class needs storage of its own to be built into. Give either
arm a temporary and there is nowhere for the answer to live.

**So it is refused, and the refusal was made to say that.** It read
"incompatible types", which points at the arms when the problem is where the
result would go; it now names the rule and suggests an `if`. Compiling and
crashing is worse than refusing, and this is the one place today where the
difference between the two was a single `else` branch.

**It landed 2026-09-04, and it took three fixes in the order they had to
come.** The lowering was never the hard part: a frame slot for the result, each
arm building into it through `buildInto`, the conditional itself becoming the
`int` the arms answer with, and the expression wearing the `*(build, &tmp)`
shape `classTemporary` already produces.

**First, a guard is only as good as its initialisation.** The flags these
temporaries carry are cleared in front of the full expression by
`endFullExpression` - and a declaration never goes through it, flushing instead
through `flushTemporaries`, which runs *after* the statements it is destroying
for and cannot place anything in front of them. So on that path a guard held
whatever the frame did. They are cleared at the **function's entry** now, which
is the one place every path passes and which `flushTemporaries` does not need
to reach: cleared there and again as each object is destroyed, a guard is false
whenever its temporary does not exist.

**Then a temporary an arm made became that arm's alone.** One arm runs, so
destroying the other's temporaries is destroying objects that were never built
- `b ? take(T(5)) : take(T(9))` did, and `int r = ...` of that form ran two
destructors on slots nothing had written. Most temporaries are marked where
they are constructed; what needed `markArmTemporaries` is the object a *call*
returns, which cannot be marked there without wrapping the `Call` node the
elision paths find by `dynamic_cast`.

**And that turned up a live one, which is the find of the round.** `return
f();` for a class returned by value handed the caller the bytes in the result
slot *and* destroyed that slot at the end of the full expression - so the
caller received freed memory. `releaseTemporary` walks the expression to the
slot and a bare `Call` is not a `Var`, so it released nothing. It was
introduced by the commit that first registered a call's result as a temporary,
and no case saw it: `std::string::substr(pos, n)` is called directly
everywhere in the suite, and it is the one-argument `substr(pos)` - one line,
`return substr(pos, npos);` - that came back empty.

## Five walls out of Compiler++, and the shape they had in common

**macOS first, then Linux, then Windows** - and the reason that order costs
nothing is that every wall here was in `src/parser/`, which does not know what
machine it is compiling for. A front-end fix lands on all three at once; only
the ABI and the code generators differ, and 510 emit cases already walk those.

**A typedef at namespace scope was keyed by its bare name.** A class declared
there carried its namespace in its tag and was findable as `n::S`; a typedef
was registered as `sz` and so could be reached from inside the namespace, by
the walk in `findTypedef`, and never as `std::streamsize` written out - which
is how a program outside the namespace names it. Keyed by the qualified name
now, as everything else at that scope already was.

**A leading `::` works as a type and is still refused in an expression**, and
the split is not arbitrary. A type is one look in one table, and that table is
exactly the global scope by construction: a class at file scope is keyed by its
bare name, one in a namespace by its qualified name, one local to a function
lives in `localTypes_`. So `findGlobalTypedef` reaches the first and neither of
the others, and `::Lexer *lexer;` is that plus a token. A name in an expression
goes through `qualifyForLookup`, where a namespace and a using-directive get
their say - restricting that for one name means a flag put down again before
the call's arguments are parsed, and the half-built version *silently found
`cc::f` where the program asked for `::f`*. Measured, not guessed, which is why
it is refused rather than shipped.

**A redeclaration was looked up under a different key than the definition
registered.** `findGlobalToUpdate(d.name)` took the written name while the
registration below took `namespacePrefix() + d.name`, so a `cc::shared` beside
a global `shared` was reported as declared twice. One key, computed once.

**A `const T &` parameter took a user-defined conversion and not a standard
one.** [dcl.init.ref]/5 binds a const lvalue reference to a temporary
initialised by *any* implicit conversion sequence, and the reference binding's
rank is that sequence's rank. Only the user-defined half was here, so
`v.push_back(new Derived)` into a `vector<Base *>` and `v.assign(8, 0)` into a
`vector<unsigned char>` were both refused as no viable function - one a
pointer conversion, the other an integral one, neither of them exotic.

**And the library gaps were the rest**: `assign` and `swap` on `vector`, `swap`
on `map`, `strtol`, `strtoul`, `atof`, the one-character `find_first_of` and
`find_last_of` (the `const char *` forms were there and a `char` reaches them
through no conversion C++ has), and `rdbuf`.

**`ss << in.rdbuf()` is the whole reason a streambuf exists in most
programs**, and it is the one thing this library did not have a shape for. The
standard makes it a class with the buffering underneath; here the streams
buffer through `FILE *` already, so `rdbuf()` hands back a token naming the
stream it came from and the `operator<<` for it copies whatever is left. It is
deliberately not called `streambuf`: nothing else here takes one, and a class
with that name would promise the rest of the standard's interface.

**Found on the way and not fixed: inside a namespace, an unqualified name
finds the global one first.** `findTypedef` looks in the flat table before it
tries `qualifyForLookup`, so inside `namespace cc` a written `Lexer` is the
global `::Lexer` rather than `cc::Lexer` - the nearer scope should win. It is
recorded in `docs/CONFORMANCE.md` rather than mended here, because reversing
that order changes how every unqualified name in a namespace resolves and
wants a round of its own.

## A temporary during unwinding, and why the obvious fix is a regression

**Cleanup regions were built from `alive_`**, which is the list of objects with
names, and a temporary lives inside one full expression and is never in it. So
an argument copy and a class temporary were destroyed on the normal path and
leaked on the unwind - measured against clang with a throw through
`take(Owner(6))`, which ends with none alive there and two here.

**A pad covering the statement would have destroyed what the statement had not
built yet.** In `two(A(1), A(99))`, where the second constructor throws, it
would run `~A` on storage nothing constructed - a corruption in place of a
leak, which is the trade this tree refuses everywhere else. And the region
cannot begin after each construction instead: a `Try` wraps statements, and
that boundary is inside one.

**So each temporary carries a guard flag in the frame**, an int that is cleared
in front of the full expression, set the moment its constructor returns, and
cleared again as the object is destroyed - on the normal path and in the pad
alike. The pad destroys each temporary under its own flag, so one region covers
a whole statement and still destroys exactly what exists. It is MSVC's unwind
state variable written out by hand, and it needed nothing the AST did not
already have: `If`, `Assign` and `Comma`.

**Cleared in front of the expression, not once.** A statement inside a loop
runs again, and a flag left set from the turn before would have the pad destroy
an object this turn never built. `inLoop` in the case is there for that and
nothing else.

**And the pad clears as it destroys.** `_Unwind_Resume` carries on through the
enclosing regions of the same function, so a pad that destroyed without
clearing would be followed by one that destroyed the same object again - a
double free reached only by an exception, which no normal-path test could see.
The Windows funclets chain for the same reason and got the same treatment.

**The regions start at the top of the block now** rather than at the first
construction, because the first temporary can come before anything is alive and
would otherwise sit outside every region.

**Every temporary of the block is listed in every pad of it.** That reads
wasteful and is what makes it correct: the flag is what says which of them are
live at the point the exception left, so listing one that is not costs a test
and no more. Deciding *which* pad should carry which temporary would be the
statement-splitting this design exists to avoid.

**And a function body's regions reach back to its own by-value parameters.**
On Microsoft the callee destroys those, and `block()` bounded its regions at
what was alive when the body opened - which is *after* them. So an exception
leaving such a function destroyed everything but its parameters, on the one
target that owns them. `bodyCleanupFrom_` carries the earlier bound and only
the regions use it: the normal path already unwound them, through the `return`
and through the append that catches falling off the end.

**The one shape that stays broken is a Windows ABI question, not a design
gap.** Where an argument's constructor throws after an earlier argument was
copied into its parameter, nobody destroys that copy there: the caller does not
own it and the callee is never entered. Clearing the guard after the call
over-destroys instead - measured, because the callee's own regions destroy the
parameter on the way out and the caller's pad then does it again. Telling
"entered" from "not entered" is a state change at the call instruction, which
statement-granular regions cannot express. `docs/CONFORMANCE.md` records it and
the case names the shape rather than skipping it.

**The Windows answers here were measured in about fifteen seconds each**, not
by `verify-three`: the assembly is generated on the Mac, `scp`-ed to the box,
and assembled, linked and run there by a four-line `.cmd`. A full three-box run
rebuilds the compiler twice and answers one question in twenty minutes. Build
the short loop before the third question, not after the tenth.

**What is not covered is the object a call returns**, and the reason is
mechanical: setting a flag after the call means wrapping the `Call` node, and
both elision paths find their candidate by `dynamic_cast`-ing to it, so
wrapping would silently turn elision off. Elision routes that object into the
slot of whatever consumes it and those slots are guarded, so
`two(make(7), make(2))` balances; an un-elided result with a throw later in the
same expression does not. `docs/CONFORMANCE.md` records it, and the fix is a
field on `Call` plus a line in each code generator.

## A class returned by value was never destroyed, and the two halves that hid it

**One object leaked per call**, in every shape a call's result can appear in:
`make();` discarded, `make().v`, `T b = make();`, `T b(make())`, and
`take(make())`. It was reported as a double destroy and measured as the
opposite - which is the whole reason to count live objects rather than read
printed lines. `Guard g = Guard(5);` printing two destructors for one
constructor is **not** a fault: it is the legal non-eliding option, and
`clang -fno-elide-constructors` prints exactly what cxx1 prints. The fault was
next door and had the other sign.

**The caller never owned the result slot.** `completeCall` allocates a frame
slot for a class return - it is where the callee builds through the hidden
pointer - and nothing was destroying it. It goes on `pendingTemps_` now like
any other temporary, so the full expression ends it. What that needs beside it
is a way to *give it up*: `claimCallResult` takes the entry back off wherever
something redirects the result into storage of its own - the copy elision in a
declaration, and the argument-copy elision in `materialiseCopy`. Without that
the destructor would run on a slot nothing ever built, which is worse than
the leak.

**And the callee copied what it had already given away.** `return Owner(n);`
releases the temporary rather than destroying it - correctly, because its bytes
travel out through the hidden pointer and the caller owns them from that
moment. But the branch below it, which exists so that returning a glvalue this
function does not own calls the copy constructor, could not tell that case from
a released temporary: it built a second object and left the first undestroyed.
`releaseTemporary` answers whether it found one now, and a released temporary
*is* the object going out.

**The elision had to widen, and the reason is the point.** The gate asked
`nonTrivialCopy()`, so a class of plain members with a `~T` - trivial to copy,
observable to destroy - was copied rather than elided. Once the result slot was
being destroyed properly that showed up as two destructions where clang has
one, in three existing cases. Both readings are legal; matching the oracle is
worth more. **A destructor makes the copy observable even where the copy
itself is trivial**, and that is what the gate asks now.

**Found beside it and not fixed: a temporary is not destroyed when an
exception passes through.** Cleanup regions are built from `alive_`, which
holds named objects; `pendingTemps_` has no region of its own, so an argument
copy or a class temporary is destroyed on the normal path and leaked on the
unwind. Measured with a throw through `take(Owner(6))` - clang balances,
cxx1 leaves two alive - and it is pre-existing rather than opened by this
change: it needs cleanup regions built from the temporaries list, which is
real machinery and its own step. `docs/CONFORMANCE.md` records it.

**`return-by-value-balance.cpp` counts live objects and whether constructions
balance destructions, and never the number of constructor calls.** Elision is
permitted rather than required in C++11 and the two oracles take different
options, so a constructor count has no single right answer - while a leak or a
double destroy moves those two numbers and nothing else does. That is the
shape CLAUDE.md already prescribed for `return-copy-balance` and `move-only`,
and this is the case family it said was missing.

## A declaration in a condition, and the two rules that are not one rule

**`if (T x = e)` was deferred at rung 1** with the note that it "needs the
condition's scope to wrap both branches, which is a change to how `If` is built
rather than an addition to it". That was the right description and it is what
the fix turned out to be: the statement goes inside a `Block` of its own -
which already carries a scope - with the declaration hoisted in front of the
`If` and the destructors after it. Three of Compiler++'s sixteen sources were
stopped by it, all writing the same line: `if (cc::ArrayType *at =
dynamic_cast<cc::ArrayType*>(t))`.

**The `if` form and the `while` form are different rules, and reading them as
one is how to get this silently wrong.** [stmt.select]/2 evaluates the
condition of an `if` once, so hoisting is exact. [stmt.iter]/2 creates and
destroys the loop variable **on every turn** - so hoisting a `while`'s
initialiser would evaluate it once and then loop for ever on the value it got.
`while (int *p = pull())` is the shape that shows it, and it is an infinite
loop rather than a wrong number.

So the `while` form declares the slot once - it is one object as far as the
frame is concerned - and **moves the initialisation into the condition**,
`(x = e)`, which is what the standard asks for as long as there is no
constructor or destructor to run. A class there is refused by name, and that
refusal is the honest edge of this: it would need its constructor written where
the test is.

**The `if` form gets full generality for free**, because it goes through
`declarationBody` rather than through a second declaration parser. One flag
tells that function's tail that the `)` is the caller's and that a condition
declares one name; everything above the tail - `auto`, a class with a
constructor, the copy elision, the `alive_` bookkeeping - is the path an
ordinary declaration already took. **The tail is one place and the loop is
not**: the class branch leaves the declarator loop by `break` rather than by
falling out of it, so the check that was first written inside the loop was
skipped by exactly the declarations that needed it most.

**And the names suite found the half that was missing.** `tests/names.sh`
reported that clang emitted `_Unwind_Resume` for a function with a
condition-declared object and cxx1 did not - which reads as a naming difference
and was a missing cleanup region. The normal-path destructors run where the
statement ends, and unwinding does not go that way: an exception through either
arm would have left the object undestroyed. The condition's object needs the
same `built` record a block keeps for what it constructs, in the same shape -
a statement index and how many were alive after it.
`tests/cases/condition-declaration-unwind.cpp` runs it and the destructor
fires.

**Found on the way, and not fixed here: `Guard g = Guard(5);` destroys twice.**
One construction, two destructions - the temporary is elided into `g`'s storage
and then both are on `alive_`. It has nothing to do with conditions; it is an
ordinary declaration initialised by a class temporary, and for a class that
owns anything it is a double free. Recorded rather than mended in the same
change, because it is a different subject and wants a case family of its own.

## `mutable`, three lines and one exception that was already there

**[dcl.stc]/9 makes a `mutable` member writable through a const object**, which
is what a class reaches for when something it caches or counts is not part of
the value it presents. It was refused with "'mutable' is implemented, but not
where a type was wanted" - implemented on a lambda, and nowhere else - and one
`mutable bool` in one header stopped five of Compiler++'s sixteen sources.

**The propagation was already in one shape at each of the three places a member
is reached**, which is why the fix is three lines rather than a design. `.`,
`->`, and the implicit `this->name` inside a member function each ask whether
the object is const and qualify the member's type if it is - and each already
carried one exception, the reference member whose referent [dcl.ref] does not
reach. A mutable member is the second exception in the same condition.

**It is a property of the member and not of its type**, so the flag sits on
`Member` beside the access and the bit-field width, not in the qualifiers.
Putting it in the type would have made `mutable int` a type distinct from
`int`, which would reach the mangler - and neither ABI spells it, because the
keyword changes no name and no layout.

**The four things it may not be applied to are refused by name**, and each is a
contradiction rather than a gap: a static member is not part of any object, a
const one is what the keyword exists to undo, a reference cannot be rebound
whatever is said about it, and a function is not a member that is written.
clang refuses all four and so does this.

**Written after the type - `int mutable x;`, which is legal and nobody writes -
it is still refused**, by the keyword table with the message that keyword
already had. The specifier is read before `specifiers()` in the member loop,
which is one place rather than a fifth spelling inside a function that answers
about types.

## An enumeration inside a class or a namespace, and the three things it wanted

**`enum Kind { Red, Green };` written in a class body was refused with "this
declares nothing - a member needs a name"**, which is a message about a member
declaration answering a question about a type declaration. It is the ordinary
way C++ spells a small set of named constants, so this shut a door most
programs go through - five of Compiler++'s sixteen sources among them - and it
was not on any refusal list, because nothing refused it *by name*.

**Three things were missing at once and each was in a different file**, which
is why it read as one wall rather than three.

**The declaration.** In a class body `enum Kind { ... };` declares a type and
no member, exactly as a nested class does - and it is told apart by the
**keyword**, not by what the specifier answers with. A nested class is
recognised at the semicolon by the type being a struct with a tag; an
enumeration answers `int`, and there is nothing in an `int` to recognise. So
the member loop records whether the specifier started at `enum` before it
reads one.

**The name.** The tag and every enumerator take the enclosing class's tag or
the namespace prefix - `C::Kind`, `C::Red`, `n::Level`, `n::Low` - which is the
same qualified-string key a nested class already uses. Without it two classes
could not each write `Red`, and a class's enumerator collided with a global of
that name.

**And the lookup, which is a walk rather than one flat map.** `enums_` was
keyed by the written name and read by one `find`, so a prefixed enumerator was
findable only by its full name. `findEnum` now tries the flat map, then
`qualifyForLookup` for the namespaces, then `classStack_`, `currentClass_` and
`inlineOwner_` - which is the order every other name in this parser is looked
up in, and it is what makes `Green` mean `C::Green` inside a member of C and
nowhere else. `enumInClass` is the per-class half, written to mirror
`lookupInClass` so the two cannot drift.

**A qualified enumerator needed a fourth place, and it is not where the
namespace one lives.** `n::Low` is read by the namespace branch of `primary`,
which is guarded on `namespaces_` and never sees a class. `C::Red` goes to the
branch below it - the one that finds a static data member by the longest
prefix that names a class and has such a member - and it now makes the same
walk one step over for an enumerator. A value and not an object, so there is
nothing to take the address of and the number is the whole of it.

**An enumeration is still `int`, and this did not change that.**
`docs/CONFORMANCE.md` records it, and it is visible in the mangling: cxx1
spells `n::twice(Level)` as `_ZN1n5twiceEi` where clang writes
`_ZN1n5twiceENS_5LevelE`, and `?twice@n@@YAHH@Z` against `?twice@n@@YAHW4Level@1@@Z`.
`tests/cases/nested-enum.nonames` records that for all three targets. Giving an
enumeration a type of its own is a separate step, and it changes the name of
every function that takes one.

## Ordinary C++ this refuses, and none of it is on the ladder

**The gap this section exists to close.** `docs/CONFORMANCE.md` holds what
compiles and is wrong, and the ladder holds the features not written yet -
each of which is refused *by name*, so a reader who reaches for one is told
which one. Between the two there is a third kind, and it had no home: small
pieces of perfectly ordinary C++ that are refused by a message about something
else, belonging to no rung and named nowhere. They are invisible until
somebody writes one, which is how all three below were found - by running
plain C++ past the compiler and reading what came back, rather than by
reading the ladder.

Found 2026-08-30 and each checked against clang, which accepts all three.

| written | what comes back |
| --- | --- |
| `P a(1), b(2);` | `expected ';'` — **fixed 2026-09-04**, and it was never about the arguments: see below |
| `return P(1);` | `'P(...)' makes a temporary of type 'struct P'` — a class temporary written as a functional cast |
| `int f(int) { … }` | `a parameter of a definition needs a name` - **fixed 2026-09-04** |

**The third had a sharper edge than it looks, and it is mended.** The postfix
increment is declared `operator++(int)` and *nobody names that parameter* - it
exists only to tell the two forms apart, so every real program writing one hit
this. The prototype form was accepted and only the definition refused, which
is the inherited C rule: a body needs a name to refer to the parameter by. C++
does not require one, and a compiler that means to run other people's code has
to take it.

**An unnamed parameter still occupies a slot and a place in the calling
convention**, which is why skipping it was not an option and giving it `off =
0` would have put it on top of the frame's first word. It is declared under a
name no program can write - `$unnamed<n>` - and everything below that point is
the named path unchanged: the frame slot, the calling convention, and on
Windows the `alive_` entry that destroys a by-value class parameter. The same
device the lambda return typedef and a pack's members already used. Four of
Compiler++'s sixteen sources were stopped by it.

**The second is what a constructor is usually written through**, so it is the
one most likely to stop a real program: `return P(1);`, `f(P(1))` and
`P p = P(1);` are all it. What it needs is a materialised temporary with a
lifetime, which is also what `static_cast<T &&>` of a prvalue wants - see rung
7.4b, where that was refused for the same missing thing.

None of these is hard; they are recorded because nothing else records them and
because each was a surprise. **A fourth was found with them and is fixed**:
calling an operator function by its name - `a.operator+(b)`, `operator+(a, b)`
- was refused with `'operator' is not supported yet` about a feature this
compiler has. See "Operator overloading".

**A fifth was found here and is fixed**: `const S s = {};`, an empty braced
initialiser, was refused with "an initialiser list needs at least one value" - a
rule about lists answering a question about `{}` meaning `T()`. It was the first
thing a reader tried after the const refusal above, so the two together were a
wall. See "An empty pair of braces is value-initialisation".

## What is in `tools/`, and what was inherited and is dead

Audited 2026-08-30, because a fork carries its parent's scaffolding and nobody
notices until somebody runs one and it fails at a path that has not existed
since the fork.

| tool | state |
| --- | --- |
| `verify-three` | the three-box rule with a command behind it |
| `mangled-names` | cxx1's names beside clang's, all three targets |
| `cl-measure` | asks cl on the Windows box about the Microsoft ABI |
| `unwind-check` | rung 6's tables |
| `windows/` | the Windows half of `verify-three`, kept in the repo on purpose |
| `windows/asm-run.cmd` | one `.asm` assembled, linked and run on the box - seconds, not a rebuild |
| `backend-overlap` | python3; how much of the two code generators is the same algorithm twice. Still works, mentioned nowhere until now |
| `gen-corpus` | python3; generates a corpus of a given size. Still works; its comments cite a `tests/challenge.sh` that did not come across |

**Three were removed rather than left to rot**: `against-gcc`, `ccc` and
`cc1-as-clang`, all inherited from Compiler-C and all driving `$ROOT/cc1` - a
binary this repository has never had, since the one here is `cxx1.exe`. `ccc`
also pointed at a `docs/STATUS.md` that did not come across either. None of
them could run; keeping them meant the next reader had to work that out again.

**`tests/c-corpus/` is not obsolete and is not a pass rate.** Compiler-C's 425
cases, inherited and untriaged - much of it is valid C++11 and much is not.
Its status is above, under the fork; leave it until somebody triages it case by
case.

**`./build` was live, correct and orphaned, and `verify-three` goes through it
now.** It caps a build at 300 MB through `systemd-run` and says so plainly when
no cgroup is available, and it was written after an unbounded `dnf` drove the
419 MiB Linux box into swap until sshd could not fork. Nothing referenced it,
and `verify-three` - the only thing that ever builds on that box - ran a bare
`make -j4`: a safety net nobody was under. That is exactly what thrashed on
2026-08-30, when a killed run left its `make` orphaned on the far side, a
second run overlapped it, and eight `cc1plus` competed for 419 MB with zram at
100%. Under the cap the runaway is killed alone and the box stays up.

**-j2 and not -j4.** One `cc1plus` on the big parser files peaks near 50 MB, so
two fit inside 300 MB with room and four sit at the cap's edge - where a
legitimate build would be killed and reported as a verification failure. Raise
it deliberately, having measured, or not at all.

`obj/` and `tests/out-*/` are build output, gitignored, and regenerate on
demand - 36 MB of the tree between them. `.DS_Store` files are ignored too and
were swept.

## The names are asked of cl now, on the box where cl lives

`tools/windows/names-vs-cl.cmd`, run by `verify-three` and reported as
**windows names vs cl**. For every case it builds an object with cxx1 and one
with cl, dumps both symbol tables, and the Mac diffs them.

**Why, when `names.sh` already checks the names.** That one asks clang with
`-target x86_64-pc-windows-msvc`, which is a second implementation of the
Microsoft ABI. This page has said for a while that a Microsoft question goes to
cl first and clang second - and yet every mangled name this compiler emits was
checked only against the second, on a Mac, by a compiler nobody links with.

**It found a real defect on its first run, and one no name comparison could
have found.** Every `static` function came out of the object as an **External**
symbol: two translation units each with their own `static int total(...)` would
have collided at the link. The *names* were right and agreed with clang - what
was wrong was the storage class beside them, which only a symbol table shows.

The cause is a MASM default. cxx1's `PUBLIC` list was correct all along and
left a `static` function out of it, exactly as the Itanium side leaves the `L`
out of the name - but **MASM exports every `PROC` unless told otherwise**, so
the directive exported it anyway. `OPTION PROC:PRIVATE` in the preamble, and
70 of 78 cases now agree with cl where 37 did.

**`tools/mangled-names` had to be taught to ignore directives** in the same
change: its PROC rule reads `<name> PROC` as a definition, and
`OPTION PROC:PRIVATE` has that shape - so the line that fixed the bug was
scraped as a symbol called `OPTION` and reported as a difference in all 78
cases at once. A sweeping failure in a tool that has been stable for weeks is
worth suspecting *the tool* over the change.

**A `.nocl` file records a difference against cl**, as `.nonames` records one
against clang, and the reason is printed rather than swallowed. The eight are
worth knowing apart:

* `constexpr-function` - **a language-version difference, not an ABI one.** cl
  has no C++11 mode, its floor being `/std:c++14`, and C++11's rule that a
  `constexpr` member function is implicitly const was removed in C++14. So
  cxx1 writes `?twice@Board@@QEBAHH@Z` and cl writes `QEAA`, and both are right
  for the language they are reading. clang at `-std=c++11` agrees with cxx1.
* `local-class`, `local-class-main` - cl inlines the local classes' members
  away and emits nothing; cxx1 has no COMDAT and emits a real function. The
  same difference `docs/CONFORMANCE.md` records as "an inline member function
  is a strong symbol".
* `array-of-class` - cl calls its own vector constructor iterator `??_H`; cxx1
  emits the loop inline. Nothing outside the object can tell.
* `destructor`, `implicit-move`, `cleanup`, `try-catch-value` - a deleting
  destructor cl writes, and this target's exception funclets.

## Rung 7.6: lambdas, and the ladder is walked

**A closure is a class with a call operator, generated where the lambda is
written**, so this needed both of the pieces that came before it - a class that
can be defined inside a function, and `operator()`. Three decisions carry the
rest.

**The object lives in the enclosing frame.** A lambda expression *is* a class
temporary, and this compiler has none - the same gap that refuses
`return P(1);`. So the closure gets a frame slot of its own and the expression
answers with a `Var` naming it. Its lifetime is the function rather than the
full expression, which is longer than [expr.prim.lambda] asks for and harmless
while a closure has nothing to destroy. It is also what makes an immediately
invoked lambda work without any temporary machinery at all.

**The body is replayed as a member function**, through the same held-body path
a class written inside a function already used - so nothing in the definition
machinery had to learn what a lambda is. That path wants tokens shaped like a
definition, which a lambda's are not, so they are synthesised:
`<ret> operator ( ) ( <params> ) const { <body> }`, built from the lambda's own
parameter and body tokens and appended to the stream. An *index* into `tokens_`
survives the vector growing, which is what makes appending safe where a pointer
would not be.

**The return type is spelled as a hidden typedef.** Synthesising tokens for an
arbitrary type is not possible in general - `int` is one token, `const char *`
is three, a class is its tag - so the deduced type is registered under a
made-up name and that one identifier is written instead. Those names never
reset where the closure numbering does, or two functions each numbering from
zero ask for `$lret1` twice.

### Deduction reads the body, and follows clang rather than the letter

[expr.prim.lambda]/4 as C++11 wrote it says a body of the form
`{ return e; }` has e's type and **anything else is void** - so
`[](){ auto i = ...; return i(); }` would deduce void and then be ill-formed.
clang accepts it under `-std=c++11` anyway, applying the relaxation C++14 made,
and real C++11 code is written expecting that. **The oracle is followed here
rather than the letter, and this is the one place in this compiler where that
happens** - said out loud because it is a choice.

So the first `return` **at the body's own brace depth** is what deduces, and
depth is not a detail: in `{ auto i = [](){ return 1; }; return i(); }` the
first `return` token belongs to the inner lambda. The statements before it are
parsed too, in a throwaway scope with the parameters declared and the enclosing
function's locals put aside - the expression may name a local the body
declared, and jumping straight to the return leaves it undeclared.

**One closure per lambda written, however often it is read.** 7.1 reads an
`auto` initialiser twice, so `auto f = [](int a){...};` reached the lambda
parser twice and made two classes - and the declaration then refused its own
initialiser, `'f' is 'struct main::$_0' and this is 'struct main::$_1'`. Keyed
by the token the `[` sits at, which is the same on every reading.

### What it found in the machinery underneath

**`returnType_` belonged to the function being read, and the replay reads a
different one.** Local classes hid this: a member's body set it to whatever
that member returned, and the enclosing function's next `return` happened to
want the same type. A lambda made it visible - a `void` one, and the function
that wrote it was then told its own `return` was wrong.
`replayInlineBodies` restores it, with `functionName_` and `atFunctionBody_`.

**The closure numbering does not match clang's and does not have to.**
[expr.prim.lambda] makes a closure a unique unnamed type. Both compilers write
`$_N` within the enclosing function; cxx1 numbers in the order they are
written and clang does not - measured on a file whose *third* lambda came out
`$_0`. Nothing links against these: the call operator is reached through the
object and the object never leaves the function that made it.
`tests/cases/lambda.nonames` records it for all three targets.

### Captures, and rung 7.6 finished

**A capture is a member of the closure**, copied from the enclosing function
where the lambda is written - and reading one inside the body needed no new
rule at all: `operator()` is a member function, and an unqualified name in one
already means `this->name`. The class is laid out as any class is, each member
at the next offset its alignment allows.

**The copying happens on every reading of the lambda, not only the first**, and
that is the bug this cost. 7.1 reads an `auto` initialiser twice and the second
reading takes the cached class - so with the copying beside the building, the
object the declaration actually kept was never initialised at all. It printed
`-15 -341155503` where it should have printed `6 7`. `buildClosure` is called
from both the building path and the cache hit, and the capture list is kept
beside the type for exactly that.

The captures are declared in the deduction scope too. That reading hides the
enclosing locals - a lambda body sees its own parameters and, without a
capture, nothing of the function around it - so a captured name has to be put
back, or `[k](int a){ return a + k; }` cannot deduce its own return type.

### `[=]` takes everything the body reads, and finding it is a scan

An earlier refusal said a default capture "would need a second pass over the
body this parser does not make". It makes one now, and the shape of it matters:
**a scan of the body's tokens, not a parse.** An identifier that names a local
of the enclosing function is captured, unless the token before it says it is a
member name - `p.k`, `p->k` and `N::k` name no local.

**Over-capturing is harmless and under-capturing is not**, which settles every
doubtful case in that scan. A name the body declares itself shadows the member,
because a local is looked up before a member, and a parameter does the same -
so a copy nobody reads is the worst it can do. `docs/CONFORMANCE.md` records
that a closure can therefore be larger than the standard's minimum.

**A capture goes in a scope of its own**, outside the parameters and the body.
All three in one scope made `[=](int a)` where the enclosing function also has
an `a` report that `a` was declared twice, and did the same for a body that
declares a name it captured. Both are things C++ allows and both now shadow.

**A reference captured by value copies what it refers to**, and needed nothing:
every mention of a reference here is already lowered to a dereference, so
`objectRef` hands back the object.

**`[&]`, `[&x]` and a mixed list work too**, and they waited on reference data
members rather than on anything about lambdas - see below. Once those existed
this was mostly the same code as capturing by value, with `bindReference` in
place of the assignment and the same pointer-sized slot.

### `[this]`, and the three lookups that had to learn it

The closure holds a pointer to the enclosing object and the body reaches
through it. What made this more than a fourth capture is that **inside the call
operator `currentClass_` is the closure**, so every way of naming something in
the enclosing class looked in the wrong place - and each is a different piece
of the parser:

* the word **`this`** meant the closure's own, which is never what the reader
  wrote. [expr.prim.lambda] says it is the captured one.
* an unqualified **data member** was searched for in the closure and not found.
* an unqualified **member call** was searched for the same way, in the branch
  that runs *before* the free-function one - so `twice()` reported that no such
  function was declared, saying nothing about members at all. Putting the new
  check after that branch changed nothing, which is the tell: the free-call
  branch had already failed.

**A lambda has the access of the function it was written in**,
[expr.prim.lambda]/7, so a private member is reachable from one. Both access
checks ask `insideAccessOf` for that now rather than comparing `currentClass_`
themselves - **they had already drifted apart once in the writing of this**: the
data-member check learned about closures and the member-function one did not,
so a private field was readable from a lambda and a private method was not.
Two checks of the same rule will always drift; one helper is the fix.

**`this` is kept in the deduction scope**, unlike every other local. That
reading hides the enclosing function's locals - a lambda body sees its own
parameters and, without a capture, nothing else - but a body naming a member
reaches it through `this`, and hiding that left `[this](){ return n; }`
reporting that `n` is a member with no object to read it from, which is what
the machinery says when `this` has gone missing.

**The Windows box caught a bad case, not a bad compiler.**
`lambda-capture-this` first put a call that *mutates* the object among the
`printf` arguments that read it - and the order arguments are evaluated in is
unspecified. The two Itanium targets go one way and x86_64-windows the other,
so it printed `5 5 15 16 6 10` on two boxes and `6 6 18 19 6 10` on the third.
Both are right; the case was wrong, and only the third box could say so. A
mutation among arguments belongs in a statement of its own.

**`mutable` is the whole of the difference between a const call operator and a
non-const one**, and that is all it turned out to be: [expr.prim.lambda] makes
a closure's `operator()` const unless the lambda says otherwise, so a by-value
capture cannot be written through without it. One flag on the declaration and
one token in the synthesised body - and what is written is the closure's *own*
copy, the enclosing variable being untouched, which is what capturing by value
means. `[&]` is how you write through to the original and already did.

The keyword is still refused on a *data member*, which is a different feature
and stays on the list of words this parser knows and has no rule for.

### A lambda inside a lambda, taking the capture of the one around it

**The name is not a local by then**, which is the whole difficulty. The outer
lambda's capture is a member of the outer *closure*, so `findLocal` answers
nothing. `outerCaptureAccess` reaches through the outer `this`, and it is asked
in three places that have to agree: where a named capture is looked up, where
the `[=]`/`[&]` scan decides what to take, and where the closure object is built
and the copy is actually made. Miss the third and the class is right and the
object is uninitialised - the same shape as the bug captures had when they
first landed.

**The tag needed a counter, and the reason is not obvious.** A closure is named
after the function that writes it - but inside a replay that function is
`operator()`, so every level of nesting built `operator()::$_0` and the third
was told its own call operator was declared twice. The *owners* differ, each
closure's operator() having its own linkage name, so the owner decides and a
counter separates the display tags. It is the disambiguation local classes
already had, which is why it was three lines: **a general rule written once for
one feature paid for a second.**

With that, every capture C++11 has is here: by value, by reference, `[=]`,
`[&]`, `[this]`, `mutable`, and nesting to any depth. - by then that name is a member of the outer
closure and not a local, so the scan has nothing to take.

## Reference data members, refused since rung 3

**What one occupies is a pointer, and asking the type is the wrong question.**
`sizeof` a reference is the size of what it refers to, which is right for
`sizeof` and wrong for a slot - so the layout asks a *slot type* while the
declared type stays the reference, which is what tells every read of it to
dereference. That one sentence is the whole reason this was refused for so
long, and why a pointer member was not a substitute: it would have compiled and
lied about its own type.

**It is bound, never assigned**, and the mem-initialiser list is the one place
that can happen. `bindReference` supplies the address - the same road a
reference local's initialiser takes - and the member is typed as the pointer it
really is for the store.

**A const object does not reach through it.** [dcl.ref]: the const stops at the
reference, which could not be rebound anyway, so `h.r` on a const `h` is still
`int &`. Applying the object's cv-qualification here - which is right for every
other member - made a `const` member function unable to return its own
reference member, and that is how the rule was found.

**Three read paths had to learn it, not one**: `.`, `->`, and the implicit
`this->name` an unqualified name inside a member function takes. The first two
were changed together and the third was missed, which the case caught at once -
`return r;` inside a member function goes through none of the paths a reader
thinks of first.

This is what `[&]` was waiting for, and it is what the refusal said. Naming the
real blocker rather than calling the feature unwritten is what made the order
obvious when it came time to do it.

## Class temporaries: one gap, three symptoms

`P(1)` builds a temporary now. It was reachable three ways and refused in all
of them - as an expression, as `return P(1);`, and as `static_cast<T &&>` of a
prvalue - which is what made it the most valuable thing left rather than the
most visible. Lambda captures were a fourth: a capture has to be initialised
where the lambda is written.

**The object goes in a slot of this frame and the expression answers with its
name**, the constructor sequenced in front by a comma. That is the shape
`materialiseCopy` already built for a by-value class parameter; what was
missing was a way to ask for one by writing the type.

**The shape is `*(P::P(&tmp, 1), &tmp)` and not `(P::P(&tmp, 1), tmp)`.** The
second reads better and cannot work: `isGlvalue` gives a comma its right
operand's value category, so the parser would let anyone take its address - and
**taking the address of a comma is not something the three backends know**.
`take(P(4))` found that immediately, since passing by value takes an address.
Writing it as a dereference says the same thing with a shape all three already
handle, because the address of `*p` is `p`.

The temporary is destroyed at the end of the full expression, [class.temporary]
/4, which is what `pendingTemps_` was already for.

**What it did not fix**: `T(3);` as a statement on its own - a temporary built
and discarded. The statement parser reads a type name followed by `(` as a
declaration and reports `expected ')'`. It is the vexing-parse machinery that
would have to be told, `tests/cases/constructor-vexing` pins the behaviour that
makes it delicate, and a discarded temporary is worth less than that risk.

## Member initialisers, `int x = 5;` on the member itself

**Kept as a place in the token stream**, the way a default argument is and for
the same reason: [class.base.init] evaluates one per *construction*, in a
constructor that may not have been read when the class was, so one parsed tree
could not serve them all. Read again at each constructor that needs it, with
the enclosing locals put aside - a member's initialiser may name a global or an
enumerator and cannot name a local of whatever function happens to be
compiling.

**[class.base.init]/9 makes it a fallback and not an override**, member by
member rather than all or nothing. `S(int a) : x(a) { }` on a class with
`int x = 1; int y = 2;` takes x from the list and y from the class, which is
what `member-initialiser` pins.

**A class with nothing but an initialiser still needs a constructor**, and this
is where it could have gone silently wrong. The implicit default constructor is
only declared when it has *work* to do - a vptr, a base with one, a member with
one - and an initialiser on a member is now another thing that counts. Without
that line there is no function to put the store in, and `S s;` leaves x holding
whatever was on the stack: a silent wrong answer of exactly the kind the array
bug was.

**And the array path had to say it was using the constructor.** Every other
route to one goes through `resolveOverload`, which marks it used;
`constructLocalArray` looks the default up directly and did not. It went
unnoticed while the only classes with implicit constructors were ones that also
had members or bases with constructors - a class whose *only* reason to have one
is a member initialiser is what exposed it, and `S a[2];` called `S::S()` with
nothing defining it. A declared-and-never-emitted function is a link error, so
this one at least announces itself.

## Pointers to data members, and why no backend heard about them

**`int S::*` is not a pointer to anything.** What it holds is an *offset* into
an object of the class, so `s.*p` is `*(T *)((char *)&s + p)` - an add and two
casts all three code generators already knew. A new `Kind` in the type system
and not a line in any backend, which is the same trade a reference took when it
was lowered to a slot holding an address.

**The cast to `char *` is the one thing that can be got wrong quietly.**
Without it the add is scaled by the member's size and the answer is wrong
rather than refused, which is why it is written down here and in the case.

**`&S::x` is read where `&` is**, because nothing else would: the qualified-name
path in `primary` answers for a *static* member, which is an ordinary object
with an ordinary address, and a non-static member has no address of its own to
take at all. The offset is a `Num` with the member-pointer type on it.

**The declarator restarts.** `int S::*p` reaches the `::` loop that qualifies a
name being defined, and what follows is a star rather than a name - so the
member-pointer type is built there and `declarator` is called again from the
star. `int S::**pp` and `int S::*a[3]` then come out right with no rule of their
own.

**Size is the one thing that is not the same everywhere**: Itanium keeps a
`ptrdiff_t` and Microsoft an `int` for a class with single inheritance, so 8 and
4. Measured with clang for both, and `Type::size` already took a target.

**Both manglings measured**: Itanium writes `M` then the class then the member's
type - `int S::*` is `M1Si` - with the class going in *as a type*, so it takes
part in the substitution table like any other. Microsoft writes `PEQ` then the
class's scope then the type, `PEQS@@H`, where an ordinary `int *` is `PEAH`.

### And pointers to member functions, which are a different animal

Not an offset but a code address and how to find the `this`. Measured: Itanium
keeps a **pair**, 16 bytes; Microsoft keeps a single code pointer, 8. The two
ABIs disagree about the shape and not merely the spelling.

**It wears the shape of a struct, and that is the whole trick.** Giving it a
kind of its own would have meant teaching three backends to copy, pass and
return one, at each of the dozen places they ask `isStructOrUnion()` - the
by-value machinery rungs 3 and 4 depend on. Giving it the *shape* of a struct
meant teaching them nothing: `kind_` is Struct with the ABI's own members, and
a flag is what tells `describe()` and the manglers what it really is.

**The object and the code pointer have to reach the call together**, and no
expression here holds a pair. So `o.*p` answers with the code pointer and
leaves the address in the parser for the `(` in `postfix` to pick up as the
first argument. The window is one token wide, which is what makes it safe:
`(o.*p)` is only ever written in order to be called.

**The parenthesised-declarator branch had to learn the shape.** `int (*p)()`
and `int (S::*p)()` are the same thing to it - what is inside the parentheses
points at something, so what follows them is a parameter list and not an array
bound. Without that, `int (S::*f)()` built `int` and handed the inner
declarator the wrong base. Once it was told, the member-pointer code needed no
look-ahead at all: **the base says which of the two kinds this is.**

Both manglings measured: Itanium writes `M` then the class then the function
type - `M1SFivE`, the same `M` a data member pointer uses - and Microsoft `P8`
then the class then an ordinary member signature, `P8S@@EAAHXZ`, where a data
member pointer writes `PEQ`.

**A case must not print `sizeof` of one of these.** It is 16 on the Itanium
targets and 8 on x86_64-windows, both right, so a case that prints it passes on
two boxes out of three - and `member-function-pointer` did, until the Windows
box said so. That is the second time in one session a target-dependent value
was baked into an expected output, the first being an evaluation order; both
were caught by the third machine and by nothing else.

**Refused by name: a pointer to a *virtual* member function**, which keeps a
vtable index in the low bit on Itanium and branches on it at every call, and
calls a compiler-emitted thunk on Microsoft. And **a pointer to a *const*
member function**: the constness of `this` is decided where the member is
declared and is not part of a function type here, so taking the word would make
a pointer that could be given a non-const member's address and then called on a
const object.

## Decisions already taken

**Conform to the platform ABI; do not invent one.** Itanium C++ ABI on Linux
and macOS, Microsoft ABI on Windows. It costs more up front and it is what
makes clang and cl usable as oracles at the object level — mangled names and
vtable layouts can be diffed directly. An invented ABI links with nothing and
can be checked against nothing.

**Instantiate a template by replaying its tokens, and give up two-phase
lookup for it.** The reasoning is in the rung-5 section; what it buys is that
no second AST and no second lookup pass has to exist, and what it costs is that
cxx1 will accept a program clang refuses. Recorded here because it reverses
what this file used to say.

**Build the constant evaluator early.** Array bounds, enumerator values,
`static_assert` and every non-type template argument need it, and `constexpr`
is that evaluator exposed to the user. It is not a rung-7 feature.

**A keyword that is recognised but not implemented must be refused by name.**
`"'typeid' is not supported yet"`, not a parse error twenty tokens later.
Diagnostics ship with the rule that intercepts them.

## Understandable means checkable - the principle the 09-02 review applied

**A change makes this compiler more understandable when it makes more of it
*checkable*, and less when it buys tidiness with a fact nobody can re-measure.**
That is the one sentence every proposal in `docs/DESIGN-REVIEW-2026-09-02.md`
was judged against, and it is the reason that review reads as it does. What is
being maximised is not how the code looks at a glance. It is how much of what
the code claims a reader can go and confirm - against clang, against cl, against
a suite that fails when the claim is wrong.

Six rules fall out of it, each with the case in this tree that produced it.

**1. A measured fact stays re-measurable, and stays beside its measurement.**
`Abi` is eleven unlabelled positionals: the densest table of measured ABI facts
in the tree, unreadable at exactly the place someone would go to check it. The
mend names the fields without moving them. The same rule is what refuses an IR
and a register allocator: they would make register choice a global property, and
every `movq 56(%rsp), %rcx` recorded in this file would stop being a line anyone
can put to clang.

**2. Duplication is judged by which agreement it hides.** Two copies inside one
target - the caller and the callee each classifying arguments by hand - must
become one, because the agreement between them is what A-01 broke, consistently
on both sides, where no suite could see it. Two copies across targets stay two,
because divergence between targets is this project's house bug class and a
shared abstraction is exactly what would hide it. The LSDA is the one exception
and proves the rule: same table and same encoding bytes on both Itanium targets,
the difference only in spelling - so the difference goes into a small struct
where it is *visible*, rather than into a second hand-written copy where it is
one missed edit away from silent divergence.

**3. An invariant a person has to remember is a defect with a delay on it.**
Seventeen accessors forward class state through `unqual_` by hand and three do
not; `frameSize - slot` is computed at four sites; "mark this function used" is
spelled three ways. Every one of those was already a bug once here. The mend is
to make the rule structural - one `ClassInfo` the qualified copy points at, one
`establisherOffset`, one `markUsed` - and not to write the reminder more loudly.
A rule that can be forgotten will be.

**4. Refuse by name, and never accept quietly.** This is the standing rule
applied to design: a target that cannot do something says so where the reason is
known, which is what `.notarget` and the parser's refusals are for, and what a
`Backend::supports(feature)` table would take away by moving the refusal off the
line that knows why. The corollary the review found is sharper: a refusal that is
*unreachable*, or one that names a feature the compiler now has - `pending[]`
still calls `template` and `virtual` unsupported - is worse than none, because it
is a claim the compiler makes at the user and cannot support.

**5. A claim with no oracle is not allowed to be believed.** `tests/emit.sh`
compiles every case for three targets and counts what compiled; it never
compares the assembly. So "this refactor changes no behaviour" had nothing in
the suite to prove it with, and the whole plan reorders around building that
first. The same instinct is already written down two sections up as *a green
suite proves nothing until you prove what it ran against*; this is that rule
turned on the refactor rather than on the compiler.

**6. What must not change is half the finding.** Each of the four reports ends
with shapes that look wrong and are load-bearing: absolute token indices, the
by-value `Signature`, a member-function pointer wearing the shape of a struct,
`unqualifiedSpecifiers` answering void for `Point::Point(`. A review that
produces only a list of changes has not finished, because the next reader will
tidy one of those away and find out why it was there from a failing suite - or
worse, from a silent wrong answer.

**Where this came from, honestly.** Four of the six were in the brief the
reviewers were given: measure rather than read, refuse at the point of
interception, the suites are the oracle, and name what should not change. Two
were not. **The emit.sh gap was found unprompted and inverted the order of
everything else**, and *make the invariant structural rather than remembered*
was reached independently by three of the four, in three subsystems that share
no code - which is the kind of agreement worth promoting from an observation to
a rule.

**The test to put a future proposal through**, in this order: does it leave every
measured fact as easy to re-measure? Does it hide an agreement that is currently
checked, or reveal one that currently is not? And can its own claim of "nothing
changed" be *shown* - if not, build the thing that would show it first.

## What the compiler says about itself, and where it said it wrongly

**Four claims cxx1 made about itself were untrue, and three of them travelled in
its output.** The MASM preamble said `Generated by cc1`, the DWARF producer said
`cc1`, and `DW_AT_language` said **DW_LANG_C89** - inherited whole from the C
compiler this was forked from. The fourth was `make help` and the usage line,
which offered `<file.c>` and a `-g` that stops on "a line of C".

**The language byte is the one that does something.** With C89 a debugger treats
a symbol as a C name; with `DW_LANG_C_plus_plus` it treats it as C++. Measured on
the Mac after the change: lldb prints the frame as ``dw`::add(int, int)`` where it
had printed a bare `add`, stops at `dw.cpp:1:24` with `a=1, b=2` readable, and a
breakpoint set by *name* now matches every base name in the process the way it
does for a clang-built C++ program. That last one looks like a regression and is
the language being told the truth.

**The preamble change is what the golden was built for.** 135 files changed, all
of them `x86_64-windows`, each by exactly one replaced line - checked, not
assumed, which before the golden existed was not something anybody could say.

## How correctness is established

Differential testing against gcc, clang and cl over a growing corpus. That is
the method that produced Compiler-C's 418/418 and its zero disagreements
against cl, and with self-hosting off the table it is the only oracle here.

A green suite proves nothing until you prove what it ran against. Rebuild from
clean before believing a number, and record the commit it was measured at.

**Check `origin/main` before starting, not after.** This tree moves fast enough
that a clone taken days ago is a different compiler: rungs 5, 6 and most of 7
landed in the four days after rung 4, and the parser became nine files under
`src/parser/` in the middle of it. A session that began from a stale clone
re-fixed a bug `bd63799` had already fixed, wrote a second copy of
`tests/cases/virtual-inline` under another name, and produced a patch against
`src/Parser.cpp` - a path that no longer exists. None of it was detectable from
inside that clone, where the suite was green and the bug reproduced exactly as
reported. `git fetch origin main && git log --oneline HEAD..origin/main` costs a
second and answers it; run it before the first edit rather than when the patch
will not apply.

**cl on the Windows box is the primary venue for measuring the Microsoft
ABI.** clang will spell Microsoft names on any machine and stays useful for
that, but it is a second implementation of that ABI where cl is the ABI, and
the two have already been seen to differ about code generation - the secondary
vtable and the biased `this` in `tests/cases/thunk.cpp` were settled by cl and
not by clang. So a Microsoft question is asked of cl first.

    tools/cl-measure some.cpp             cl's assembly listing and symbols
    tools/cl-measure some.cpp symbols     just the symbols

cl has no C++11 mode - its floor is `/std:c++14` - so it answers about the ABI
and not about which language version a construct belongs to. That question
still goes to `clang++ -x c++ -std=c++11 -pedantic-errors`, and the two Itanium
targets have no other oracle at all.

**Its Windows half lives in `tools/windows/`, in this repository**, and that is
deliberate: scaffold kept only on the box comes back after a rebuild as what
looks like a network fault.

**The Windows runner checks the refusals too**, and did not until 2026-08-29.
It looped over `*.expected` alone - "every case that has recorded output" -
so 45 of 90 cases were checked there while the other two boxes ran all 90.
Mostly that repeated what they had already proved, since the parser is shared;
but not entirely, and the exception is the part that mattered. `throw`, `try`
and a local with a destructor are refused *only* for x86_64-windows, and
until the loop existed **no box checked those refusals at all**. It now runs
86 of 90 with four named skips - and the first thing it found was that
`throw-refused` stops for a different reason on that target, which is correct
and is why the case is skipped there rather than argued with.

The lesson is worth more than the fix: **a runner that iterates one kind of
file will quietly check one kind of thing**, and the count is the only place
it shows.

## namespace, and the fact that a namespace is not a type

**A namespace has no `Type`, and everything else here follows from that.** A
nested class is a member of an enclosing class, so `Outer::Inner` has an
`enclosing()` the manglers walk and the substitution tables recognise by
pointer. There is no object to point at for `N::S`, so a class declared inside
a namespace carries its scope in its **tag** - the same string key every table
in the parser already uses - and the manglers split the tag instead.

That makes one thing ambiguous, and it is worth naming because it cost a green
suite: a **local** class's tag has a `::` in it too. `f::L` is one name, not a
scope, and both ABIs spell it whole. Splitting on the spelling turned every
lambda into `10operator()` and broke two cases that had nothing to do with
namespaces. `Type::inNamespace()` is a flag set where the tag is built, and the
manglers ask it rather than looking for a `::`.

**Lookup is a search, written once.** `qualifyForLookup(name, exists)` tries the
enclosing namespaces innermost-out, then the ones a `using namespace` has
opened, then the name as written, and returns the first key that answers - where
"answers" is a predicate passed in, so functions, globals and type names share
the walk. It is a fallback and not a merge; `docs/CONFORMANCE.md` records what
that costs.

**Argument-dependent lookup, as far as an operator needs it.** `a + b` where
both are `N::V` has to find `N::operator+`, and nothing brings it into scope:
the call site is outside N and wrote no qualification, which is the whole point
of writing the operator beside its class. `lookupKeys` adds the operand types'
own namespaces to the names tried, for operators and for ordinary calls alike.

### What was measured

Both ABIs write a namespace exactly the way they write a class scope, which is
the useful part - nothing new had to be invented, only reached.

```
N::f(N::S, N::S)            _ZN1N1fENS_1SES0_        ?f@N@@YAHUS@1@0@Z
N::M::g(N::M::T, N::S)      _ZN1N1M1gENS0_1TENS_1SE  ?g@M@N@@YAHUT@12@US@2@@Z
outer(N::S, N::M::T)        _Z5outerN1N1SENS_1M1TE   ?outer@@YAHUS@N@@UT@M@2@@Z
N::B's vtable               _ZTVN1N1BE               ??_7B@N@@6B@
N::operator+(N::V, N::V)    _ZN1NplENS_1VES0_        ??HN@@YA?AUV@0@U10@0@Z
```

Two traps in there, both found by the names suite rather than by reading:

**Every namespace prefix is an Itanium substitution candidate, and the longest
one wins.** `_ZN1N1M1gENS0_1TENS_1SE` has `S_` standing for N and `S0_` for
`N::M` - so a candidate is registered per cumulative prefix, and writing `S_`
for N followed by `S0_` for `N::M` spells the same scope twice. A namespace is
not a `Type`, so these go in the table by name; `substitutedName` already
existed for template names and took them unchanged.

**An operator written in a namespace keeps its Microsoft code.** `??HN@@YA...`,
not `?operator+@N@@YA...`. The scoped branch of `Microsoft::function` had been
written for ordinary names and asked `operatorPrefix` only in the unscoped one.

### What is refused

`namespace { }` (the linkage is the feature, and `static` says it in one word),
`namespace N::M { }` (C++17), `namespace A = N;`, `using N::f;` - the
using-*declaration*, which is a different rule from the directive - and a
leading `::`. Each has a case in `tests/cases`.

`using namespace N;` works at file scope and inside a block, where it reaches
the closing brace and no further: `block()` truncates the list on the way out,
which is the whole of the scoping.

## nullptr, which the backends never heard about

**At the machine it is a pointer-sized zero and nothing else.** `Num(0)` with a
type on it is the whole of the code generation, and no backend changed. What
the type buys is entirely in the front end: a null pointer constant stops being
spelled the same as the number 0.

That single fact is the feature. `f(0)` picks `f(int)`, because 0 is an int
that happens to convert; `f(nullptr)` picks `f(char *)`, because
std::nullptr_t is not an integer and `f(int)` is not viable for it at all. And
`int n = nullptr;` is a diagnostic rather than a zero.

**Where the kind sits in the enum is load-bearing, twice.** `Kind::NullPtr` is
between `Void` and `Bool`: TypeTable builds one Type for every value in the
Void..Function range, so a fundamental type has to be inside it; and
`isInteger()` is a range check starting at `Bool`, so anything before `Bool` is
automatically not an integer - which is the one thing this type must never be
mistaken for.

**Most of the work was already written.** `isNullConstant` was the parser's
name for "the literal 0 in a pointer context", and it gates comparison, the
`?:` arms and the conversion. Teaching it that `nullptr` is one too made all
three work without a new call site.

### The bool rule, which is the part worth not guessing

[conv.bool] gives std::nullptr_t a conversion to bool **for direct-
initialization only**. An argument is copy-initialized, so `f(bool)` is not a
candidate for `nullptr` at all - not a candidate that ranks worse. Measured:
with `f(bool)` alone clang says there is no matching function, and against
`f(char *)` it picks the pointer with no ambiguity to report. Ranking bool
beside the pointer conversion would have invented that ambiguity.

Meanwhile std::nullptr_t **is** a scalar ([basic.types]/9), so `!nullptr`,
`nullptr && x` and `if (nullptr)` all work - a contextual conversion that
`convert()` lowers to a comparison against zero like any other. The two live
together because the diagnostic gate for copy-initialization
(`requireConvertible`) is a different function from the lowering (`convert`),
and only the second was taught the conversion.

### What was measured

```
void f(decltype(nullptr))    _Z1fDn                   ?f@@YAX$$T@Z
sizeof(nullptr)              8
```

`decltype(nullptr)` is how a program spells the type here; the name
`std::nullptr_t` needs `<cstddef>` and there are no headers. Diagnostics print
`std::nullptr_t` all the same, because that is what it is called.

Conversions run both ways at the same rank - `nullptr` reaches any pointer and
any pointer to data member, and the literal 0 reaches std::nullptr_t - so
`f(void *)` against `f(char *)` is ambiguous for `nullptr`, and
`f(decltype(nullptr))` against `f(char *)` is ambiguous for `0`. Both are
refusals clang makes, and both have a case in `tests/overload/`.

### What is refused

`int n = nullptr;`, `bool b = nullptr;`, ordering (`nullptr < nullptr`,
`p < nullptr`), arithmetic, `*nullptr`, and `++` on a std::nullptr_t object.
Each has a case.

One shape it does **not** reach, and not because of nullptr: a pointer to
member *function* has no null value yet by any spelling - `= 0` is refused in
the same words - because it is two words on both ABIs and zeroing it is a
struct store rather than a scalar one.

## static_assert, a declaration that declares nothing

The condition is folded in the parser and a zero is a diagnostic carrying the
program's own words. **Nothing reaches the AST**, so no backend, no emitter and
no mangler heard about this feature - which is what makes it one function,
`staticAssertion()`, called from three loops rather than three implementations.
Those three are file scope (`topLevel`), a block (`statementBody`, where it
becomes the empty statement the way a using-directive does) and a class body
(the member loop in `ParserType.cpp`) - three places in two files, and the rule
is one rule.

`fold` already existed, for array bounds and enumerators and `case` labels, so
the constant evaluation is not new work. What is new is one parse and three
call sites.

### The message is required, and that was measured rather than read

C++17 made it optional; C++11 did not. **clang accepts the one-argument form
under `-std=c++11`** and only calls it an extension under `-pedantic-errors` -
so a default clang build would have said the form was fine. It is refused here
for the same reason `namespace N::M {}` is: a file that builds here must not
stop building on the C++11 compiler it was written for.

### What it deliberately does not accept

The condition has to be an **integral** constant expression, which is narrower
than the standard's "contextually converted constant expression of type bool".
`static_assert(1.5, "")` and `static_assert("abc", "")` are both legal and both
refused. Neither is a thing anybody writes, and `fold` answers about integers -
widening it is constant-evaluation work, not static_assert work.

The message must be written out as a literal, adjacent pieces included. A
`const char *` initialised beside it is refused, because the message is printed
by the compiler and there is no program running to read a variable.

### A trap in the *test*, not the compiler

`tests/cases/static-assert.cpp` asserts about a `constexpr` function and a
file-scope `const int`. clang emits **neither** symbol: a constexpr function
nobody odr-uses folds away, and so does a const int every reader of which is a
constant expression - a `printf` argument included. `names.sh` then reports
cxx1 emitting symbols clang did not, which looks like a naming failure and is a
difference about *emission*. The case calls the function and takes the
variable's address so both compilers emit both. Worth remembering the shape:
**when the names suite reports a symbol cxx1 has and clang does not, ask what
clang folded away before looking for a mangling bug.**

## explicit, which changes nothing about a function

`S s(3);` and `S s = 3;` call the same constructor with the same argument and
differ in one thing: whether that constructor may be chosen without being asked
for by name. So `explicit` is **one bool on the signature** - not on the class,
because it is one constructor of a set that is explicit - checked at each place
the standard calls copy-initialization. No mangled name, no vtable and no
emitted code changes.

The check is made **after** overload resolution rather than by hiding the
constructor from the candidate set. That way the reader is told which
constructor was found and why it could not be used, instead of that nothing
matched - which sends them looking for a constructor that is there.

### The three copy-initializations, and only one is written with an '='

This is where the work was, and finding all three took measuring rather than
reading:

* **`S s = x;`** - the obvious one, in `constructLocal`.
* **`return s;`** - [class.copy]/31 copy-initializes the caller's object, so a
  class whose copy constructor is `explicit` **cannot be returned by value at
  all**. The function is ill-formed on its own, before any caller is looked at.
  And the rule is checked *even though this compiler elides the copy* - the
  constructor is selected and checked whether or not it is called.
* **A by-value parameter** - [dcl.init]/17, in `materialiseCopy`. The least
  obvious of the three, because nothing at the call site has an '=' in it.

Two of those reach past `constructLocal`, and the elision branch reaches past
it as well - `S b = make();` builds straight into `b` and calls no copy
constructor, and is still refused. **Where the rule is checked and where the
call happens are different places**, which is the shape to remember: a check
attached to the code that emits the call would have missed all three.

### What it is not able to do here yet

`explicit` on a **conversion function** is the other place C++11 allows it, and
this compiler has no conversion functions at all - so the refusal names *that*
rather than saying the keyword applies to constructors, which would send the
reader to fix the wrong half.

And `explicit` bites in fewer places here than in a full C++ compiler simply
because cxx1 has fewer implicit conversions: a converting constructor is not
tried for a function argument or a return value at all, so there is nothing for
`explicit` to stop there. What it does stop is real; what it would stop is not
reachable yet.

### A test trap, the second of its kind

For a constructor defined **inside** its class, clang on x86_64-linux emits
only the base-object `C2` and calls it, where cxx1 emits and calls the
complete-object `C1`. Both are self-consistent, both link, and each translation
unit carries its own inline copy - so nothing is broken. But `names.sh` reads
it as seven disagreements. Defined out of line, both compilers emit both.

That is the same lesson as the constexpr-and-const-int one under
static_assert, and now it has happened twice: **when the names suite reports a
disagreement on a case that changes no names, the case is usually asking the
two compilers a question about emission.**

## The named casts

`static_cast` was already here. This adds `const_cast` and `reinterpret_cast`,
and refuses `dynamic_cast` by name.

**Neither of the two generates anything.** Every conversion they allow is
between things of the same size, so the value is unchanged and what moves is
the type. What the code here does is *say which claim was made* - which is the
whole reason C++ gave each a name of its own instead of letting the C cast do
all of it silently.

### The line between them is const, and it runs both ways

`reinterpret_cast` may not take const off; `const_cast` may not change what is
pointed at. Letting either do the other's job would make one of them redundant,
and would let a program take a const off while looking like it was only
changing a type. Doing both means writing both, in either order. Both halves
have a refusal case.

`const_cast` also asks that the two types be **similar** in the standard's
sense: strip the pointers in lockstep, ignore the qualifiers at each step, and
arrive at the same type. `const int *const *` to `int **` is similar and legal;
`const int *` to `char *` is not. Only `const` is a qualifier this compiler
has - `volatile` is parsed and dropped - so that is the only thing either cast
can move.

### dynamic_cast is a rung, not a missing branch

It asks what an object *actually* is, which only a `type_info` beside its
vtable can answer, and this compiler emits none for a class on any target. The
work is the type_info, the inheritance graph it carries, and the `__cxa_` call
that walks it. Refused by name, and the refusal points at `static_cast`, which
does the direction that needs no run-time answer.

### `long` is not a portable pointer-sized integer

A `long` is 8 bytes on x86_64-linux and arm64-darwin and **4** on
x86_64-windows, so `reinterpret_cast<long>(p)` compiles on two targets and is
refused on the third. `long long` is 8 on all three. This was caught by
`emit.sh` - which compiles every case for every target and stops at assembly -
before any box was asked, which is what that suite is for: **a target-dependent
mistake in a case is cheapest to find on the machine you are sitting at.**

## noexcept, both of them

**In C++11 the exception specification is not part of the function's type.**
Measured: `void f() noexcept` and `void f()` both mangle to `_Z1fv` on Itanium
and `?f@@YAXXZ` on Microsoft. So no name, no overload set and no signature
match changes - which is most of why this rung is small. C++17 made it part of
the type; this compiler targets C++11.

Because nothing else holds a declaration and its definition together, **the two
have to say the same thing**, and that check is the only structural work the
specifier needs. clang refuses a mismatch in both directions and so does this;
without it the two would silently be one function carrying whichever promise
was read last.

`throw()` is the C++03 spelling and is taken as the same promise. `throw(T)` is
the *dynamic* exception specification - a different feature needing a run-time
check of the thrown type against a list - and is refused by name rather than
read as `throw()`, which would be a promise the program did not make.

### The operator counts during the parse

`noexcept(e)` does not run `e`. The operand is parsed for its meaning and
thrown away, the way `sizeof`'s is, and what is kept is whether anything in it
could throw. That is counted **while** it is being read rather than by walking
the tree afterwards, which keeps the whole thing to one integer: every call
already passes through `resolveOverload` (which knows which function it
reached) or `completeCall` with a non-null callee (a call through a pointer,
which promises nothing), and every `throw` through one place.

A call through a function pointer is always potentially-throwing, and that
falls out of the same rule that made this rung small: the promise is not part
of the type, so a `int (*)()` says nothing about what it points at.

### The specification is recorded and not enforced

A throw escaping a `noexcept` function propagates here where it should call
`std::terminate`. **The compile-time half is complete and exact** - the
operator agrees with clang everywhere it was measured - and the run-time half
is in `docs/CONFORMANCE.md` with the reason: enforcing it means wrapping every
such function in an implicit `try`, which would inherit the two try/catch
limits already recorded here and start refusing programs that work today. Lift
those first.

## Comments in src/ are at most three lines

**No comment group in `src/` runs longer than three lines** - a group being a
run of `//` lines with nothing between them - and that is a standing rule
rather than a tidy-up somebody did once. It was applied across the tree on
2026-09-02: 640 groups, 4,575 of the 5,642 comment lines there, rewritten to
keep the finding and the measurement and drop the reasoning around them.

**What was dropped is not lost, and this file is where it went.** The long
form - why an ABI answer is what it is, what a bug looked like before it was
mended, which oracle was asked - belongs here, in `docs/CONFORMANCE.md`, or in
a case's own header comment, where a reader looks for it deliberately. What
stays beside the code is the sentence a reader needs *at that line*.

So when a new fault is mended: write the three lines beside the fix, and put
the story in the section of this file that owns the subject. A comment that
wants a fourth line is a sign the story belongs here instead.

## Build

```
make                 build cxx1.exe with clang++ (Mac) or g++ (Linux)
make test            run the four suites
make clean
```

**`tests/emit.sh --record` keeps what it emitted as a golden, and every later
run diffs against it.** That is the oracle a refactor needs and the suite did not
have: counting what compiled says nothing about what came out, so "this change
emits the same code" was a claim rather than a measurement. Record before the
change, run after it, and the answer is `golden - N of 411 files changed`. It
never fails the run - a changed file may be the point of the change - and it
prints the commit and date the golden was taken at on every run, so a stale one
says so itself. `make golden` is the same thing.

**The golden survives `make clean`, alone among `tests/out-*`.** It is recorded
before a change and read after one, with a rebuild in between, so a clean that
took it away would delete the baseline at the moment it was about to be used.
Remove it by hand or record over it. It is not shipped to the other two boxes
either: `tools/verify-three`'s tar excludes `tests/out-*`, so a run there says
there is no golden, which is the truth - a golden is a *local* before-and-after,
not a fact about the tree.

The assembly is byte-stable across runs - measured, on all three targets - so a
diff of it means what it looks like it means.

**The golden is copied file by file, and it counts itself afterwards.** A plain
`cp -R` of `tests/out-emit` under ~/Documents came back with 411 files and 411
`x 2.s` beside them - the duplicate phenomenon this file records elsewhere - and
the next run read a golden of 822 and reported 411 removed. It caught its own
corruption, which is the behaviour to keep; recording now removes the duplicates
and refuses to record at all if the count does not match what was emitted.

**`tests/corpus.sh` runs the 424 inherited C cases and gates on nothing.** They
came from Compiler-C untriaged, they are C, and this is a C++11 compiler - so
the count is not a pass rate and `tests/c-corpus/README` says what each part of
the failing set actually is. Measured 2026-09-02 on the Mac: 365 ran and agreed,
58 refused, 1 wrong. What it is for is the failing set, diffed across a change.

`tests/run.sh` compiles and runs each case in `tests/cases/` on this machine
and diffs against its `.expected`; a case with a `.error` file instead must
fail to compile with that text in the message. `tests/emit.sh` compiles every
case for all three targets and stops at assembly, so it needs no assembler and
runs anywhere. `tests/overload.sh` is the fourth and works differently - see
"Overload resolution is checked against clang, not against a recorded answer".

```
tools/verify-three            Mac, Linux box and Windows box
tools/verify-three linux      one of them
tools/verify-three windows
```

A change is not done until all three have passed, and this relays the *working
tree* rather than a commit so it can be run before committing - which is the
order that rule wants. Two things it has to do and that are easy to leave out:
`COPYFILE_DISABLE=1` on the tar, or macOS ships an AppleDouble `._Driver.cpp`
that `src\*.cpp` in `msvcuild.cmd` hands to cl; and comparing the Windows
output back on the Mac, because a `.expected` has Unix line endings and a
program's own output leaves through the CRT with Windows ones, so `fc` would
call every case different.

`-MMD -MP` writes header dependencies. Do not remove it: a stale object here
links perfectly and corrupts the heap three passes away, which is exactly what
happened in Compiler-S.
