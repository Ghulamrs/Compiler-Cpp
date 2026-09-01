# cxx1 — a C++11 compiler

`cxx1` compiles C++11 to native assembly for **x86_64-linux**,
**x86_64-windows** and **arm64-darwin**. It is written in ISO C++14 and
depends on no compiler framework: its own lexer, preprocessor, parser, type
system and three code generators.

It is early. See `CLAUDE.md` for the ladder it is climbing and where on it the
tree currently sits.

## Build

```
make
```

Needs a C++14 compiler and nothing else. `clang++` on a Mac, `g++` on Linux;
both are checked, and `cl` is the third when the MSVC project is generated
from `tools/`.

## Use

```
cxx1 f.cpp -o f          compile, assemble and link for this machine
cxx1 -S f.cpp -o f.s     stop at assembly
cxx1 -S -arch x86_64-linux f.cpp
```

Targets are named `x86_64-linux`, `x86_64-windows` and `arm64-darwin`. A
target that is not this machine implies `-S`: the compiler will not ask the
local assembler to build for hardware it is not on.

## What it is not

**It cannot compile itself, and never will.** `src/` is C++14 and `cxx1`
accepts C++11, so the source is out of reach of the program by construction.
Correctness is established by differential testing against gcc, clang and cl
rather than by bootstrapping.

**It ships no standard library.** `<vector>`, `<string>` and the rest are not
here and are not planned. What will arrive, because the *language* requires
them, is the small set the core language cannot do without:
`<initializer_list>`, `<type_traits>`, `<new>`, `<typeinfo>` and `<cstddef>`.

## Where it came from

Forked from Compiler-C, a C90 compiler sharing the three code generators, on
2026-08-26.
