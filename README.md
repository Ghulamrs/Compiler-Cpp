# cxx1 — a C++11 compiler

`cxx1` compiles C++11 to native assembly for **x86_64-linux**,
**x86_64-windows** and **arm64-darwin**. It is written in ISO C++14 and
depends on no compiler framework: its own lexer, preprocessor, parser, type
system and three code generators.

It is early. `CLAUDE.md` opens with **"Reading this tree"**, which is the order
to read this one in - the ladder, the pipeline by file, a case per rung, and the
suites - and then holds the measurement behind each decision. What the compiler
does *not* accept is `docs/EXCLUSIONS.md`, a refusal site per entry, cited by
file and line.

## Build

```
make
```

Needs a C++14 compiler and nothing else. `clang++` on a Mac, `g++` on Linux;
both are checked, and `cl` is the third, through `msvc\build.cmd` on the
Windows box. `tools/verify-three` builds and tests on all three from the Mac.

## Use

```
cxx1 f.cpp -o f          compile, assemble and link for this machine
cxx1 -c f.cpp            stop at an object file
cxx1 -S f.cpp -o f.s     stop at assembly
cxx1 -S -arch x86_64-linux f.cpp
cxx1 -I dir f.cpp        add a directory to the ones <...> searches
```

`-D` defines a macro and `-U` removes one, `-j` sets how many files compile at
once, `-g` writes a line table for a debugger (x86_64-linux and arm64-darwin
only), `-masm` picks `masm` or `gnu` syntax for x86_64-windows, and `-time`
reports each phase. `cxx1` with no input prints the whole list.

Targets are named `x86_64-linux`, `x86_64-windows` and `arm64-darwin`. A
target that is not this machine implies `-S`: the compiler will not ask the
local assembler to build for hardware it is not on.

## The library

**A subset ships, and it is a subset on purpose.** `include/` holds 27 C++
headers and `lib/` 18 C ones, a little over 2,200 lines together. They are here
for two reasons: the core language cannot do without some of them -
`<initializer_list>` is what a braced initialiser *means* - and a compiler with
no containers cannot be pointed at a real program. **They are not an
implementation of the C++11 library and are not becoming one.**

| | |
| --- | --- |
| containers | `<vector>` `<string>` `<map>` `<set>` |
| algorithms | `<algorithm>` `<numeric>` `<utility>` |
| streams | `<iostream>` `<ostream>` `<istream>` `<sstream>` `<fstream>` `<ios>` `<iomanip>` |
| exceptions | `<exception>` `<stdexcept>` |
| language support | `<initializer_list>` `<limits>` |
| C, inside `std` | `<cassert>` `<cctype>` `<cfloat>` `<climits>` `<cmath>` `<cstddef>` `<cstdio>` `<cstdlib>` `<cstring>` |

`lib/` holds 18 C headers - `assert.h` through `time.h` - and the nine `<c…>`
names forward into it. **The forwarding is a using-declaration**, `namespace std
{ using ::size_t; }`, rather than a repeated typedef: a typedef would be a
second type that happened to agree, and the two would differ to an overload.
Measured complete - every standard name `stdio.h`, `stdlib.h`, `string.h`,
`ctype.h` and `math.h` declare is named in the `<c…>` beside it, and the only
names left behind are the platform's own, `fileno` and `__acrt_iob_func`, which
do not belong in `std`. `<cassert>`, `<cfloat>` and `<climits>` forward nothing
and are right not to: what they provide is macros, and a macro belongs to no
namespace.

**How a header is found.** `<...>` searches every `-I` directory in the order
written, then `include/`, then `lib/`. C++ comes before C because `<cstddef>`
has to be found before it can include `<stddef.h>`. Both directories are
compiled into the binary as absolute paths - `$(CURDIR)/include` and
`$(CURDIR)/lib` at build time - so a `cxx1.exe` copied elsewhere still reads
the headers of the tree it was built in, and stops finding them if that tree
moves.

### What these headers are honest about

Every one of them documents its own limits at the top. The ones that change
what a program *means*:

- **`std::cout`, `cerr`, `clog` and `cin` are `static`** - one set per
  translation unit rather than one per program. A header-only library has
  nowhere to put a definition. They also have no constructor: `ostream` is an
  aggregate built from constant initialisers, and the `FILE *` is resolved at
  the point of use, so nothing has to run before `main`.
- **`vector<T>::iterator` is `T *`**, not a class. That is what keeps the header
  short - `++it`, `*it`, `it->m` and `it != v.end()` are all built in - and the
  cost is that any insertion invalidates every iterator, `begin()` on an empty
  vector answers null, and a `vector<int>` iterator compared against a
  `vector<char>` one is not caught.
- **`vector` drops a slot without destroying it** on `pop_back`, `clear` and
  `erase`. Correct for `vector<POD>` and `vector<T *>`; a
  `vector<std::string>` leaks. Its storage is `calloc`'d and assigned into,
  because placement new is refused by this compiler, so a zeroed slot is a real
  state a class held here has to tolerate.
- **`map` and `set` are sorted vectors**, binary-searched. The interface is the
  standard's and the complexity is not: an insertion moves elements.
- **`sort` is an insertion sort** - quadratic, no scratch memory, and it steps
  the iterator both ways, which is all any iterator here can promise.
- **`numeric_limits` means something only where it is specialised.** The primary
  template answers `T()` to every query.
- The functions in `<algorithm>`, `<numeric>` and `<utility>` are `static`, so
  each translation unit gets its own copy.

### What is absent

**`<stdexcept>` is here as of 2026-09-07**, with the standard's nine classes
over a `std::string` and `std::exception` in `<exception>` beside it, as the
standard splits them. Nothing else in the library throws: a container that runs
out of memory or is indexed past its end does not raise, it misbehaves, and
`at()` **is** provided and does not throw - it indexes like `operator[]`, so a
past-the-end `at()` misbehaves rather than raising `std::out_of_range`. That is
the one place the sentence above is not kept, and it is worse than not providing
it: a caller who wrote `at()` for the check does not get one. `include/vector`.

`<memory>`, `<type_traits>`, `<new>`, `<typeinfo>`, `<functional>`, `<deque>`,
`<list>`, `<unordered_map>`, `<thread>` and the rest of C++11 are not present.
Two of those are held up by the language rather than by effort: `<new>` has
little to declare while an overloaded `operator new` is refused - so placement
new is refused with it, though plain `new` and `delete` work - and
`<typeinfo>` waits on `typeid`, which is refused by name. `dynamic_cast` does
work, and has since each vtable got a `type_info` beside it.

## What it is not

**It cannot compile itself, and never will.** `src/` is C++14 and `cxx1`
accepts C++11, so the source is out of reach of the program by construction.
Correctness is established by differential testing against gcc, clang and cl
rather than by bootstrapping.

**It is not a conforming implementation**, and the headline says so: the
language is C++11 minus `docs/EXCLUSIONS.md`, with the library above rather
than the standard's. A refusal that names a feature is the intended answer; a
refusal naming nothing is a defect, and finding those is what the sweeps in
`CLAUDE.md` are.

## Where it came from

Forked from Compiler-C, a C90 compiler sharing the three code generators, on
2026-08-26.
