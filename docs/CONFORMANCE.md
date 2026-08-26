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
