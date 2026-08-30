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

So a program that *counts* constructor calls has no single right answer here,
and `tests/cases/by-value.cpp` deliberately does not count them.

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
