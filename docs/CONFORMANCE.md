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
