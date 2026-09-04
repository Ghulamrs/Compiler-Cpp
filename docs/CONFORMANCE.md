# Where cxx1 and C++11 disagree

Everything on this page is a place cxx1 accepts or produces something the
standard does not ask for. It exists so that a gap is a recorded decision
rather than a surprise found later by a program that relied on it.

A feature that is simply not written yet is **not** on this page — it is on
the ladder in `CLAUDE.md`, and it is refused by name when a program reaches
for it. This page is only for things that compile and are wrong, or compile
and are right by accident.

## An enumeration is not a distinct type

`enum Colour { Red, Green, Blue };` makes `Colour` a type name, and it names
`int`. The standard makes it a distinct type with its own values.

What that costs, concretely:

```cpp
enum Colour { Red, Green, Blue };
int n = 1;
Colour c = n;        // cxx1 accepts. C++ requires a cast.
int m = Green;       // both accept: an enum converts to int
c = 47;              // cxx1 accepts. C++ does not.
sizeof(Colour)       // 4 here; implementation-defined but need not be int's size
```

So a program that treats an enumeration as a small set of named integers
compiles and behaves correctly. A program that relies on the compiler
*refusing* a wrong assignment gets no help.

Fixing it means a `Kind::Enum` carrying its enumerators, a conversion rule in
both directions, and overload resolution eventually having to rank those
conversions — so it is held until the type system needs to be opened anyway,
rather than done twice. Inherited from Compiler-C, where C's own rules made it
very nearly correct.

## `wchar_t` is not a distinct type

It names the target's underlying integer - `int` on Linux and Darwin,
`unsigned short` on Windows - where the standard makes it a type of its own
that merely has the same width and signedness. So an overload on `wchar_t`
will not be distinguishable from one on `int` when overloading arrives, and
`sizeof` and the arithmetic are right meanwhile. The same trade as the
enumeration above, for the same reason: a distinct type needs a `Kind`, and
opening the type system for one of these should open it for both.

## A parameter's top-level const is dropped from a Microsoft name

```cpp
void f(char * const);      // cl writes ?f@@YAXQEAD@Z, cxx1 writes ?f@@YAXPEAD@Z
```

The standard says top-level const on a parameter is not part of the function's
type - `void f(char *)` and `void f(char * const)` declare one function - and
cxx1 strips it before the type is built, so the mangler cannot see it. The
Microsoft ABI encodes it anyway, with `Q` where there would be a `P`. Itanium
drops it and agrees with us.

What it costs: a function declared that way, compiled by cl in one object and
by cxx1 in another, will not link to itself. Nothing else. Keeping it would
mean carrying the parameter type twice - once as declared and once as the
function's type - and that is not worth doing for a name.

## A static local keeps its own name in the object file

A `static` variable inside a function becomes a global here, named
`function.variable`. Itanium names it `_ZZ8functionvE8variable`. Both are
internal to the object and nothing outside can reach either, so this shows up
only in a symbol listing side by side with clang's.

## A `void *` converts to any object pointer on its own

```cpp
void *raw = malloc(4);
int *p = raw;        // cxx1 accepts. C++ requires a cast; C does not.
```

This is C's rule, inherited from Compiler-C along with the parser, and it is
what lets the untriaged C corpus reach `malloc` without a cast. It is the one
direction that is wrong: `int *` to `void *` is legal in both languages.

The const hole in it *is* closed - `const int *` will not convert to `void *`,
only to `const void *` - because that route would have undone the whole of the
const work in one line.

## A class name cannot be hidden by an object of the same name

```cpp
struct stat { int x; };
int stat;            // legal C++ - the object hides the class name
struct stat s;       // and the elaborated form still reaches the class
```

cxx1 registers a class or enum tag as a type name in the one table it has for
type names, so the declaration of `stat` as an object collides with it. This is
the C compatibility rule, it exists for headers written before C++ did, and no
program here wants it. It costs a second lookup table to fix.

## Eliding a copy is allowed, and the two oracles disagree about when

C++11 permits a compiler to elide a copy constructor and does not require it,
and the two oracles take different options at their lowest optimisation
setting: clang elides at `-O0`, cl does not at `/O0`.

```cpp
Counted giveCounted() { Counted c; return c; }
Counted r = giveCounted();      // clang: one construction. cl: a copy as well.
```

cxx1 elides, which is clang's answer, in three places: a declaration
initialised by a call that already returns the class through a hidden pointer,
such a call passed straight in as a by-value argument, and a local returned by
value - which is not destroyed on the way out, because the caller destroys it
where it lands.

That third one is not only a choice about how many constructors run. Copying
the bytes out *without* eliding the destruction would destroy the same object
twice, once in the callee and once in the caller.

**And one place cxx1 does not elide where clang does**: a class-typed member
with its own initialiser.

```cpp
struct E { M m = M(2); };       // clang: one construction, built into the member
                                // cxx1:  the temporary, then the copy, then ~M
```

Both are C++11 - [class.copy]/31 permits the elision and does not require it -
and the difference is visible only through a copy constructor that does
something. It reaches the symbol table too, which is why
`tests/cases/member-init-class.nonames` exists: cxx1 emits `M`'s copy
constructor and clang, having elided the call, never needs it.

**And here the two oracles agree with each other**, which they do not on the
return above: measured on the Windows box, cl elides this one as well and does
not emit the copy constructor either - `member-init-class.nocl` records it. So
cxx1 is alone in making the copy, and the case counts live objects rather than
constructions precisely so that all three can be compared at all.

So a program that *counts* constructor calls has no single right answer here,
and `tests/cases/by-value.cpp` deliberately does not count them.
`member-init-class.cpp` counts *live objects* instead, which is 0 either way.

## Three const declarations cl accepts and this refuses

`const A a;` for a plain struct, `const A arr[2];`, and the same at file scope
are ill-formed by [dcl.init]/7 as clang and g++ both read it - CWG 253 - and are
accepted by cl 19.44 at `/std:c++14 /permissive-`. cxx1 refuses them, so a
program written against MSVC alone can meet a refusal here.

The three of them are the only shapes of fifteen measured where the oracles
disagree; cl refuses `const int n;` and the mixed cases exactly as the other two
do. **This is a language question, not an ABI one**, which is why the two that
agree win: cl is the authority on the Microsoft ABI and no authority on what C++
means. `tests/cases/const-uninitialised.cpp` and its `-ok` neighbour pin both
sides of the line.

## A `const` member does not make the special members deleted

```cpp
struct Holder { int i; const int k; };
Holder a;            // cxx1 accepts. C++ deletes the default constructor:
                     // k has no initialiser and never could get one.
Holder b;
b = a;               // cxx1 accepts, and copies k's bytes. C++ deletes the
                     // copy assignment, because k cannot be assigned to.
```

The copy assignment half is handled as far as the compiler's own work goes:
where a class has a `const` member, no implicit copy assignment is *declared*,
which is this compiler's way of saying the standard's "deleted". What is left
is the older rule underneath it - a struct has always been assignable here,
inherited from Compiler-C along with the parser, and that assignment copies
every byte including the const member's.

So the diagnosis is missing rather than the semantics wrong: the bytes that
move are the bytes clang would have moved if the program had been legal.
Refusing it means refusing struct assignment for a case C refuses too, which
is a change to the C path and to a 424-case corpus that has not been triaged
for it - so it is recorded here rather than done in passing.

## A failed `new` terminates rather than throwing

`new` here calls the platform's own `operator new`, which throws `std::bad_alloc`
when it cannot allocate. cxx1 has no exceptions until rung 6, so nothing catches
it and the program terminates.

That is not a compiler this page can excuse into correctness, and it is also not
a thing to fix twice: an allocation that cannot fail is the only one this
compiler can currently promise, and the honest fix arrives with exceptions
rather than before them. The alternative - calling the `nothrow` operator and
answering a null pointer - would make `new` mean something the standard does not
say, in a program that could then be compiled by clang and mean the other thing.

Worth knowing because it decides what a program may rely on: `new` here either
returns memory or ends the program. It never returns null.

## An inline member function is a strong symbol

A member function defined inside its class is *implicitly inline*, and an
inline function may be defined in several translation units - so the linker has
to fold the copies rather than reject them. clang says so in the object:

```
.weak    _ZN7CounterC2Ev              # Linux
.weak_def_can_be_hidden __ZN7CounterC1Ev   # Darwin
```

cxx1 emits an ordinary strong global instead. In one translation unit that is
invisible and everything links; two units that both include such a class would
collide on every inline member.

There is a second half to it on Linux: for an inline constructor clang emits
only `C2` and no `C1` at all, where the out-of-line case emits both. cxx1 emits
both in either case, which is why `tests/cases/inline-body` is compared for
Darwin and Windows and skipped for Linux - Darwin's spelling keeps `.globl`
beside the weak marker and so still agrees name for name.

**A destructor loses a form the same way, and one of the two is in the
vtable.** Measured over `tests/cases/virtual-inline`, where every special
member is written inside its class: clang emits `D2` and `D0` and no `D1`
anywhere, and puts `D2` in the complete-object slot; cxx1 emits all three of
Itanium's forms and puts `D1` in that slot.

```
.quad _ZN6AnimalD2Ev      # clang
.quad _ZN6AnimalD1Ev      # cxx1
```

Nothing behaves differently for it. `D1` and `D2` are the same code for a class
with no virtual base, which is why clang is free to emit one and name it twice
over - the disagreement is in the symbol table and nowhere else. It is recorded
in `tests/cases/virtual-inline.nonames` as the reason that case skips the Linux
name comparison.

**The special members the compiler writes are inline in exactly this sense**,
so they carry both halves of it. clang marks an implicit default, copy or
assignment `.weak` on Linux and `.weak_def_can_be_hidden` on Darwin, and cxx1
emits a strong global. And clang emits only the constructor *forms* a use asks
for, where cxx1 emits C1 and C2 for every constructor - so a class that is only
ever built as a base gets a `C2` from clang and a `C1` and a `C2` from cxx1.
`tests/cases/implicit-special` is compared for Windows, where there is one name
per constructor and the lists agree exactly, and skipped for the two Itanium
targets with that reason on the line.

Emitting only the used form is not the fix. cxx1 knows which form an *implicit*
constructor was called by - that is the same `used` flag that decides whether
it gets a body at all - but a written constructor would still emit both, and a
compiler with two rules for that is worse than one with a recorded divergence.

Fixing it is backend work in three places - `.weak` in the GNU spelling,
`.weak_def_can_be_hidden` on Darwin, a COMDAT section on Windows - plus knowing
at emission time that a function came from inside a class. Recorded rather than
half-done, because a program built the way this compiler is used today, one
translation unit at a time, cannot see it.


## A `[=]` closure can be larger than it needs to be

[expr.prim.lambda] captures only the entities a lambda *odr-uses*. cxx1 finds
them by scanning the body's tokens for identifiers that name a local of the
enclosing function, which is a scan and not a parse - so a name that appears
without being read, or one the body goes on to shadow with its own declaration
or with a parameter, is captured all the same.

```cpp
int k = 5, unused = 99;
auto f = [=](int a) { return a + k; };   // cxx1 copies `unused` too
```

What that costs is object size and a copy nobody reads. It cannot change what
the program means: a name the body declares shadows the member, because a local
is looked up before a member, and a parameter does the same.

The direction is deliberate. Over-capturing costs a copy; under-capturing fails
to compile a program that should, and the two are not equally bad. A class with
a copy constructor that has side effects is the one case where the difference is
observable, and it is the reason this is written down rather than left implicit.


## A `using namespace` hides an outer name instead of competing with it

[basic.lookup.unqual] with a using-directive open is a *merge*: the names a
directive brings in are considered as if they had been declared where the
namespace and the using-directive both sit, which is usually the global scope -
so they stand beside whatever is already there, and two equally good candidates
are an ambiguity.

cxx1 searches instead of merging. `qualifyForLookup` tries the enclosing
namespaces innermost-out, then the ones a directive has opened, then the name as
written, and the first that answers wins.

```cpp
int f();
namespace N { int f(); }
using namespace N;
int main() { return f(); }   // clang: ambiguous. cxx1: calls N::f
```

The same shape decides an overload set: where a directive opens a namespace
holding `f(double)` and the global scope holds `f(int)`, cxx1 ranks only the
namespace's, so `f(1)` calls `f(double)` rather than choosing between the two.

What it costs is confined to programs that write one name in two scopes and
then open one of them - and in those, the standard's answer is usually a
diagnostic rather than a different call, so the failure mode is a program that
builds here and does not build elsewhere. Refusing outright would be worse: it
would refuse the ordinary case where nothing is shadowed at all.

Fixing it means candidate sets rather than a first-match search, in the three
places lookup is done - `qualifyForLookup`, `resolveOverload`, and
`resolveOperator`. That is the same change argument-dependent lookup wants,
below, and the two belong in one step.

## Argument-dependent lookup reaches only the operand's own namespace

[basic.lookup.argdep] builds an associated set from an argument's type: its own
namespace, the namespaces of its base classes, of its template arguments, and of
its enclosing classes. cxx1's `lookupKeys` takes the first of those and stops,
and it uses what it finds as a *fallback* - only where ordinary lookup found
nothing - rather than adding the candidates to one set.

```cpp
namespace N { struct B { }; int f(B); }
struct D : N::B { };
int main() { D d; return f(d); }   // clang: calls N::f. cxx1: 'f' not declared
```

This is a refusal rather than a wrong answer, which is why it is separated from
the entry above: a program that needs the wider set is told so. It is here
because the two rules are one piece of work and the narrow version is a
deliberate stopping point, not an oversight.


## A `noexcept` function that throws propagates instead of terminating

[except.spec]/9: when an exception escapes a function whose specification does
not allow it, `std::terminate` is called. cxx1 records the promise and does not
enforce it, so the exception unwinds past the function like any other and an
outer handler catches it.

```cpp
void f() noexcept { throw 1; }
int main() {
    try { f(); }
    catch (int e) { return e; }   // clang: terminate. cxx1: returns 1
    return 0;
}
```

**The compile-time half is complete and exact.** `noexcept(e)` answers the same
as clang everywhere it was measured, and the specification is not part of the
type in C++11 - so no name, no overload set and no signature match differs. The
gap is only in what happens when the promise is broken, which is a run-time
question.

Enforcing it means emitting, for every `noexcept` function, a landing pad that
calls `__cxa_begin_catch` and then `std::terminate` - which is what clang does,
under the name `__clang_call_terminate`. The machinery exists here: cxx1 has
landing pads and `catch (...)` on all three targets. What stops it being a
small change is that wrapping every such function in an implicit `try` inherits
two limits that are written down in CLAUDE.md - a local with a destructor and a
`try` in one function is refused, and on x86_64-windows a handler is a funclet
that cannot `return`. Those would then fire on ordinary `noexcept` functions
that have nothing to do with exceptions, refusing programs that work today.

So the order is: lift those two limits first, then this becomes the small
change it looks like. Recorded rather than half-built, and rather than paid for
with refusals of correct code.

**What it costs meanwhile.** A program that relies on `noexcept` to stop
unwinding - which is a program relying on it to crash - keeps running instead.
Nothing that relies on the *promise* being true is affected, because a program
that keeps its promise cannot tell the difference.

## An un-elided call result is not destroyed when an exception passes through

[class.temporary]/4 destroys a temporary at the end of the full expression it
was made in, and [except.ctor]/1 destroys it during unwinding if the full
expression does not finish. cxx1 does both now for a class temporary and for a
by-value argument copy: each carries a guard flag the cleanup pad reads, so one
region can cover a whole statement and still destroy exactly what exists at the
point the exception left it.

**The object a call returns is the exception, and it is marked unguarded.**
Setting a flag after the call means wrapping the `Call` node, and the two
elision paths find their candidate by `dynamic_cast`-ing to it - so wrapping
would silently turn the elision off, which is a worse trade. In practice
elision routes that object into the slot of whatever consumes it, and those
slots are guarded, so `two(make(7), make(2))` balances. What is left uncovered
is an un-elided result temporary with a throw later in the same full
expression:

```cpp
int f(A a, int n);
f(make(1), boom());     // make(1)'s result leaks if boom() throws
```

It is a leak rather than a wrong answer, and bounded by the frame. Fixing it
means letting a `Call` carry the flag to set on return - a field on the node
and a line in each of the three code generators - and it was deliberately not
folded into the change that guarded the other two, which touched the exception
tables on every target and wanted its own verification.

`tests/cases/temporary-unwind.cpp` pins the four shapes that do balance.

### And on x86_64-windows, a copy made for a call that is never entered

The Microsoft ABI puts the destruction of a by-value class parameter on the
**callee**, where Itanium puts it on the caller. So where an argument's
constructor throws after an earlier argument was already copied into its
parameter, nobody destroys that copy on this target: the caller does not own
it, and the callee is never reached.

```cpp
int two(A p, A q);
two(A(1), A(99));       // A(99)'s constructor throws; A(1)'s copy leaks
```

Measured on the Windows box: `live 1` there against `live 0` on both Itanium
targets and on clang. Telling "the callee was entered" from "it was not" is a
state change at the call instruction itself, and cxx1's cleanup regions are
statement-granular - so this is the one place the guard flag cannot answer.
Clearing the guard after the call over-destroys instead, which was measured
too: the callee's own regions destroy the parameter on the way out, and the
caller's pad then destroys it again.

The shape is named in `temporary-unwind.cpp` and left out of it rather than
skipped, so that the case says the same thing on all three targets.