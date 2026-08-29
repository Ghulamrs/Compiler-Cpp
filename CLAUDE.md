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
| 6 | Exceptions: `__cxa_*`, `.gcc_except_table`, unwind data | in progress: 6.1-6.4 and Windows `throw` **done**, 2026-08-29 |
| 7 | The C++11 layer: `auto`, `decltype`, move, lambdas, `constexpr`, range-for | in progress: 7.1 and 7.3 **done**, 2026-08-29 |

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

## Rung 6: exceptions, planned

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
runs no destructors during unwinding yet - that is 6.4.

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

## Rung 7: the C++11 layer, planned

**The last rung, and the widest.** Six features that mostly do not depend on
each other, so this splits where templates and exceptions had to be pushed
through in one piece. The order below is by what each one needs from the
others, and nothing after 7.1 is written.

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

**7.2 - `decltype`.** The type of an expression without evaluating it. The
parser already types every expression, so the work is the *rules*, which are
where the surprises are: `decltype(x)` is the declared type of `x` while
`decltype((x))` is a reference, and an rvalue gives a plain type where an
lvalue gives `T &`. It wants 7.4's references to say the second half at all.

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

**7.4 - move semantics**, and this is the large one. `&&` as a type, the
value categories that decide when one binds, an extra rank in overload
resolution, and the implicit move constructor and move assignment beside the
four special members rung 3 already writes. It changes overload resolution,
which is why nothing else in the rung should be waiting on it.

**7.5 - `constexpr`.** The constant evaluator has been here since rung 1 -
`fold` is what array bounds and enumerators use - so `constexpr` on a
*variable* is close to `const` with a required constant initialiser. A
`constexpr` *function* is the real work: evaluating a call at compile time
means an interpreter over the AST, which is a second execution model beside
the backends.

**7.6 - lambdas**, last because they need the most from the rest: a closure is
a class with a call operator, generated where the lambda is written, holding
its captures as members. cxx1 has no local classes at all, so that comes
first; captures by reference need 7.4's story about lifetime; and a generic
lambda would need 7.1's `auto` in a parameter, which is C++14 and out of
scope.

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

## How correctness is established

Differential testing against gcc, clang and cl over a growing corpus. That is
the method that produced Compiler-C's 418/418 and its zero disagreements
against cl, and with self-hosting off the table it is the only oracle here.

A green suite proves nothing until you prove what it ran against. Rebuild from
clean before believing a number, and record the commit it was measured at.

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
