# The second round of 2026-09-09: the register was built in the morning and closed two by the evening

**HEAD was `3782fac` on `main` when this was written, and is `b139ec2` now** -
the round below carried on into the evening and closed the third item of its
own open list, which is noted where that item stands. Four commits since
`48f28a3` at the time of writing, and the first of them is the round the
earlier handover on this date describes, which had been left staged and
unverified rather than committed.

| | |
| --- | --- |
| `6262d11` | the record of the round that closed the audit's five miscompiles - `docs/HANDOVER-2026-09-09.md`, written at 48f28a3 and left untracked |
| `cf14cb3` | V-09 is closed, and the tail it came from is a directory now |
| `29b163a` | a capture-default takes `this` as well, and both of them do |
| `3782fac` | a class template's name, in the three symbols that could not spell it |

## What was found staged, and what it needed

`cf14cb3`'s work was already written - the V-09 fix, the three lambda cases,
`tests/open/` and its fourteen programs, five `.nonames` files and the
duplicate filter in `names.sh` - and none of it was committed or verified. Two
things were missing and both were found by the three boxes rather than by
reading:

- **the build did not link.** iCloud had left `ParserExpr 2.cpp`,
  `ParserExprCall 2.cpp` and `ParserExprLambda 2.cpp` in `src/parser/`, which
  `$(wildcard src/parser/*.cpp)` picks up and make then splits on the space:
  `No rule to make target 'src/parser/ParserExpr'`, after a link that had
  already failed with every symbol in that file duplicated. **10,258 such
  copies were in the tree**, 9,928 of them under `tests/`; they are deleted.
- **`lambda-capture-this-const` needed a `.nocl`.** cl spells a closure with a
  content hash where cxx1 writes `$_0`, which no comparison can match. Its
  constructors are worth reading, though: `PEBUS@@` for the two in const member
  functions and `PEAUS@@` for the third, which is [expr.prim.lambda]/18 read
  off the Microsoft mangling.

## The two fixes, and where they came from

Both were entries in `tests/open/`, the register that `cf14cb3` created, and
both were closed the same day it was written - which is the movement that
directory exists for. Each moved to `tests/cases` with its `.expected`.

**A capture-default takes `this`.** `[=]` and `[&]` capture it implicitly where
the body names a member, and both capture the *pointer* - `[=, *this]` is
C++17. cxx1 scanned the body for names that are locals of the enclosing
function, and a member is not one, so the shape most lambdas in a class are
written in did not compile at all. The const half came free because V-09 had
been fixed first: the implicit capture asks the same helper the written
`[this]` does, so a const member function's `[=]` reaches the object exactly as
far as the function does.

**A class template's three symbols.** `_ZTV`, `_ZTI` and `_ZTS` were built by
counting the letters of the display tag - `_ZTS6P<int>`, which the assembler
refuses - where the ABI encodes the arguments, `1PIiE`. The mangler already
spells such a type wherever one is written in a signature; the symbols ask it
now. **Sanitising the brackets would have been wrong** and was the advice on
offer: `P<int>` and `P_int_` are one name after such a substitution, and
neither links against anything else.

That fix's own case found **two older bugs, one per ABI**, both in ordinary
linkage names: a specialization in a namespace was written as a bare
template-id, missing Itanium's `N...E` wrapper (`_Z1f2ns1RIiE` for
`_Z1fN2ns1RIiEE`) and missing the namespace altogether on the Microsoft side
(`?$R@H@@` for `?$R@H@ns@@`). Twelve emitted files moved on the second. `std`
had hidden the first: `St3vecIiE` carries no wrapper, and std's were the only
namespaced specializations in the tree.

## What is open, added to what the earlier handover lists

1. **A function other than `main` that owns an unwind region emits a `.pdata`
   entry pointing at a `$cppxdata$` label it never lays down**, so ml64 answers
   `A2006: undefined symbol`. A constructor taking a class **by value** reaches
   it, template or not. No existing case emits such a reference at all, which
   is why nothing had caught it; found by a new case failing on the box.
2. **`tests/open/` holds thirteen**, one of them new:
   `mem-init-template-id-base` - `D(int x) : P<int>(x) {}` is answered
   "expected '('" at the `<`, so a class deriving from a specialization cannot
   pass its base an argument.
3. ~~**`__cxa_end_catch` is not called when a handler is left by `return`**~~ -
   **closed the same evening, at `b1d97a5`.** Every jump out makes the call
   itself now, once per handler it leaves, with `break` and `continue` counted
   against the loop depth each handler was entered at and `goto` refused by
   name. The Microsoft side gained the two refusals it was missing - `break`
   and `continue` out of a funclet, which `return` had been refused for alone.
   And the exception thrown *out* of a handler is closed too, at `2b4f64a`:
   the handler's block is a cleanup region whose pad makes the call, handing
   over to the next handler's pad, then to an enclosing `try`'s chain, then to
   `_Unwind_Resume` - the first of those learned from a leak, an inner handler
   jumping past an outer one and ending one catch of the two. **That region
   then lifted an exclusion**: a local with a destructor inside a handler
   compiles on both Itanium targets as of `b139ec2`, the row it needed being
   the one the region added. `tests/open/` holds thirteen -
   `lambda-return-through-try` arrived with it.
4. Everything the 09-09 handover lists that this round did not touch: the
   Microsoft virtual-base layout, `noexcept` where the function owns a region,
   a mem-initialiser needing a temporary, ~~`-masm=gnu` as the Windows
   default~~ - **decided 2026-09-10 and it is the default now**, because ml64
   has no COMDAT directive and so cannot link a program of more than one file;
   see CLAUDE.md - the `runTool` temp-batch race, `run.sh` on the Windows box,
   and the four `<type_traits>` gaps.

## What is measured, and at which commit

| | | measured at |
| --- | --- | --- |
| Mac | run 440/0, emit 760 recorded, names 259/0, overload 30/0 | `3782fac` |
| Linux | run 440/0, emit 760/0 | `3782fac` |
| Windows | cases 422/0, names vs cl 171 agreed / 0 differed | `3782fac` |
| `verify-three` | exit 0 | `3782fac` |
| `open.sh` | 13 still differ from clang | `3782fac` |

The emit golden was re-recorded at `3782fac` with no duplicate copies in it -
the previous one was recorded at `48f28a3` over a tree carrying 395 modified
files and 1,467 iCloud copies.
