# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Two languages, and they are not the same one

**`src/` is ISO C++14. The language `cxx1` compiles is C++11.** Keep these
apart in every sentence you write about this project, because almost every
confusing question here comes from conflating them.

The consequence worth stating up front: **this compiler can never compile
itself**, and that is deliberate rather than a gap. A C++11 compiler cannot
read C++14 source. Self-hosting is not a milestone here and offering it as one
is a mistake — see "How correctness is established" for what replaces it.

`-std=c++14 -Wall -Wextra -Werror -pedantic` in the `Makefile`. A Mac cannot
enforce C++14: Apple's libc++ hands you `std::string_view` in C++14 mode, so a
C++17-ism compiles clean here and is refused only when it reaches real g++.
Build on the Linux box before believing a clean Mac build. Same trap, same
answer as Compiler-C.

Trigraphs are still live in C++14, so write `'?\?'` and not `'??'` in any
diagnostic. A C++ compiler's messages are full of quoted punctuation, so this
will come up more here than it did in Compiler-S, where it cost an afternoon.

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

**Two-phase name lookup is a day-one decision, not a template-era one.**
Getting it wrong is the classic rewrite. Even before templates parse, the
parser must distinguish dependent from non-dependent names.

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
| 4 | Inheritance → virtual functions and vtables → multiple inheritance | in progress |
| 5 | Templates: function → class → deduction → partial spec → SFINAE → variadic | |
| 6 | Exceptions: `__cxa_*`, `.gcc_except_table`, unwind data | |
| 7 | The C++11 layer: `auto`, `decltype`, move, lambdas, `constexpr`, range-for | |

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
change to how `If` is built rather than an addition to it.

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

**A jump out of a scope holding a live object is refused**, conservatively -
`break`, `continue` and `goto` while anything is alive. It refuses some
programs whose jump would not have crossed the object at all. The precise rule
needs each jump to know which scopes it leaves, which is a change to how jumps
are built rather than an addition, and skipping a destructor silently is the
one outcome worth refusing loudly.

**`tools/mangled-names` asks clang with `-fno-exceptions` now**, and that
change is about the comparison rather than the code: a class with a destructor
makes clang emit a cleanup path - the personality routine, `.gcc_except_table`,
`_Unwind_Resume` - and this compiler has no exceptions until rung 6, so those
symbols read as a disagreement about names when they are a difference in
features. Mangling is unchanged by the flag.

**Rung 4 opens with single, non-virtual inheritance**, and the base subobject
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

Rung 5 is roughly half of what remains after rung 4. Rung 6 is where the three
targets stop being symmetric: Windows EH is SEH-based and needs unwind data,
`ml64` cannot emit CodeView, and arm64-darwin objects here carry no unwind
info. Expect Windows exceptions to lag, and do not promise otherwise.

## Decisions already taken

**Conform to the platform ABI; do not invent one.** Itanium C++ ABI on Linux
and macOS, Microsoft ABI on Windows. It costs more up front and it is what
makes clang and cl usable as oracles at the object level — mangled names and
vtable layouts can be diffed directly. An invented ABI links with nothing and
can be checked against nothing.

**Build the constant evaluator early.** Array bounds, enumerator values,
`static_assert` and every non-type template argument need it, and `constexpr`
is that evaluator exposed to the user. It is not a rung-7 feature.

**A keyword that is recognised but not implemented must be refused by name.**
`"'typeid' is not supported yet"`, not a parse error twenty tokens later.
Diagnostics ship with the rule that intercepts them.

## How correctness is established

Differential testing against gcc, clang and cl over a growing corpus. That is
the method that produced Compiler-C's 418/418 and its zero disagreements
against cl, and with self-hosting off the table it is the only oracle here.

A green suite proves nothing until you prove what it ran against. Rebuild from
clean before believing a number, and record the commit it was measured at.

## Build

```
make                 build cxx1.exe with clang++ (Mac) or g++ (Linux)
make test            run both suites
make clean
```

`tests/run.sh` compiles and runs each case in `tests/cases/` on this machine
and diffs against its `.expected`; a case with a `.error` file instead must
fail to compile with that text in the message. `tests/emit.sh` compiles every
case for all three targets and stops at assembly, so it needs no assembler and
runs anywhere.

`-MMD -MP` writes header dependencies. Do not remove it: a stale object here
links perfectly and corrupts the heap three passes away, which is exactly what
happened in Compiler-S.
