# cxx1 and Compiler++, read together

Two compilers by one author, reviewed in the same week by two reviewers who
each saw one. cxx1 at `1799815` (2026-09-03), Compiler++ at `bad5f8c`
(2026-09-01). Both reviews ran about seventy probe programs against clang;
both found every suite green; both found five silent wrong answers that no
suite could see. This report is about what the two sets of findings have in
common, where they differ, and what that says about the two designs — judged
by the author's own rule, written in cxx1's `CLAUDE.md`: *a change makes a
compiler more understandable when it makes more of it checkable*.

## Verdict

**Different question, different winner, and the second question matters more.**

*Which is simpler to comprehend?* **Compiler++**, on the record and the
corpus: a 267-line design document and a 265-line ledger a reader can finish
before lunch, against a 5,063-line changelog that sends a newcomer to six
sections scattered between line 6 and line 4,653. Not on the code, though: the
Compiler++ reviewer measured about 4,600 lines across five files before a
*correct* C++-layer edit, against about 3,000 across four in cxx1 — the
two-layer derivation makes a newcomer read both layers to change one, and the
cost shows up in exactly the place the bugs cluster.

*Which is simpler to check?* **cxx1**, and not narrowly, for everything below
the language: ABI classification, mangled names, layout, overload verdicts —
each asked of clang or cl on every run, and the reviewer re-measured them (12
aggregate shapes in four link pairings, 141 names, 28 overload files, ten
cross-target constants) and found every one true. Compiler++'s only external
oracle is the *answer* of a program, through a 33-line shim; nothing outside
its tree can check its IR, its bytecode or its VM word, and its ledger already
carries a silent scalar bug (unsigned narrower than 8 bytes sign-extended)
that only an oracle below the answer would have refused to let stand.

*But for the class of bug both reviewers actually found* — an object copied,
constructed or destroyed on one path and not on its twin — **neither tree can
check anything today**, and Compiler++ is one case away from being able to
while cxx1 is one suite away. That is the interesting result, and it is
symmetric: five silent wrong answers in seventy probes in each tree, at the
same seam, for the same structural reason, after ten reviewer-sessions on cxx1
and one on Compiler++.

Which question matters more for this author: checkability, because that is
the criterion the author chose, and because the two trees' comprehension costs
are within a factor of 1.5 of each other while their oracle positions differ
in kind. cxx1's position is the better one to be in — it can acquire
Compiler++'s advantage (a live differential over the whole corpus) with a
shell script it has already written once, whereas Compiler++ cannot acquire
cxx1's (an object-level oracle) without giving up the VM. Normalised for
scope, cxx1 is no buggier; per line it is cleaner; per idea it is the same.

## 1. What is being compared, and what is not

| | cxx1 | Compiler++ | comparable? |
| --- | --- | --- | --- |
| source language | ISO C++14 | C++98 | yes, both a standing rule |
| accepted language | C++11, near-complete: templates, exceptions, lambdas, MI, move, namespaces | a named subset: no templates, exceptions, MI, `long long`, `?:`, `&`, about two dozen exclusions | **no** — roughly a 3× difference in surface |
| target | native assembly, 3 targets, 2 real ABIs | own stack VM, 8-byte word | **no** |
| source size (measured) | 26,204 lines, 47 files | 13,685 lines, 32 files | per-line figures below |
| age at review | 9 days, 162 commits | 4 days, 77 commits | commits/day: 18 vs 19 |
| prior review | 5-reviewer audit (23 defects, 19 fixed) + 4-reviewer design review, both within 3 days | none | **no** — cxx1's findings are post-audit residue |
| checked programs | 271 cases + 28 overload files | 129 cases | |
| suites | 4 (`run`, `emit`, `names`, `overload`) + 3-box relay | 6 (`tests`, `exec`, `roundtrip`, `differential`, `driver`, `amalgamated`) | |
| external oracle | clang and cl, at the object level, live | clang/gcc/cl, at the answer level, live | different level |
| design record | `CLAUDE.md` 5,063 + `docs/` 820 | `CLAUDE.md` 267 + `KNOWN-GAPS.md` 265 | 11 : 1 |
| probes in review | ~70 | 70 | yes |
| silent wrong answers found | 5 (F1–F5; F1 crashes) | 5 new (F1–F5) + 1 already in ledger | yes |
| refusals wrong or unnamed | 6 findings (F6–F11, F11 holding 3) | 3 findings (F6–F8, F8 holding 6) | yes |
| silent per KLOC | 0.19 | 0.37 | per line, cxx1 cleaner |
| silent per checked program | 1 : 60 | 1 : 26 | per case, cxx1 cleaner |
| silent per feature reached | ~5 in a 3× surface | ~5 | per feature, Compiler++ cleaner or equal |

What normalisation can honestly say: the same absolute yield from the same
number of probes, in a tree three times the language surface and twice the
lines, is a lower defect density for cxx1 on every per-unit measure except
per-feature. What it cannot say: that either reviewer sampled the language
uniformly. Both went to the class/lifetime seam and both found their five
there, so the yield measures the seam, not the compiler. And cxx1's five
survived a five-reviewer audit two days earlier that found twenty-three and
fixed nineteen; Compiler++'s five are a first pass. Read that either way —
cxx1 leaks at the same rate *after* review that Compiler++ leaks *before* it,
or the audits close instances and not the seam — it is the seam that is the
finding.

## 2. Measured baselines, and how each tree records itself

Both reviewers rebuilt from clean and ran every suite. Both trees were green.
Both trees' documented numbers were behind the measured ones, and in the same
direction — reality ahead of the record, nobody over-claiming.

| | documented | measured | drift | recorded as known? |
| --- | --- | --- | --- | --- |
| cxx1 `run.sh` | 223 (status table) / 228 (handover) | **271** | 48 / 43 behind | yes — handover open item 5, "the status table is behind" |
| cxx1 `emit.sh` | 348 / 357 | **420** | 72 / 63 | same item |
| cxx1 `names.sh` | 117 / 120 | **141** | 24 / 21 | same item |
| cxx1 `overload.sh` | 26 | **28** | 2 | same item |
| cxx1 `CONFORMANCE.md` elision claim | "elides in three places, matching clang at -O0" | two shapes do not (`T t = T(5)`, `return T(v)` with a user copy ctor) — F12 | wrong in kind | no |
| Compiler++ differential | 62 + 2 allowed of 65 | **64 + 2 of 67** | 2 cases | no — cases 119, 120 added after |
| Compiler++ `-pedantic` warnings | 12 | **16** | toolchain-dependent count | no |
| Compiler++ ledger, `main.cpp` argv | listed open | fixed in `de2d3b1`, pinned by `run_driver.sh` | stale | no |
| Compiler++ ledger, `VM.cpp:660` | line 660 | line 871 | line drift | no |
| Compiler++ exclusion table | `operator`, `friend` "not supported" | both supported | wrong in kind | **yes** — the ledger says so itself and tells the reader to "ask a build before believing any one line" |

What this says about each record. cxx1 records in accretion order — a
changelog that has grown a reading order on top — and the one table a
newcomer is sent to (reading-order step 2, "Where this stands") is the one
that is stale; the truth lives in the newest handover, one level down. The
tree knows this and lists it as open work, which is honest, but a number
copied into prose by hand has no oracle and the tree's own rule 5 (*a claim
with no oracle is not allowed to be believed*) applies to its own
documentation. Compiler++'s record is small enough that its drift is small,
and its ledger is unusual in stating its own unreliability as a method. But it
has the same disease: `62 agreed … of 65` is prose, and nothing regenerates
it. Neither tree has any suite write its own count into the document that
quotes it. In both, the gap between record and measurement is stale-conservative
rather than inflated — which is the right way round, and the same way round.

The larger point about the records is scale. cxx1's `CLAUDE.md` is 19 times
longer than Compiler++'s and holds *why* every ABI number is what it is — the
measurement behind a line — and the reviewer's re-measurement of twelve
aggregate shapes was possible precisely because the record said what had been
measured and how. Compiler++'s record holds the shape of the tree and its
traps and defers the facts to the ledger and the code. One is a laboratory
notebook; the other is a map. A newcomer wants the map first; a reader trying
to confirm a claim wants the notebook. The author's rule favours the notebook,
and the reviewer who had the notebook confirmed more.

## 3. The two designs against the author's rule

### 3.1 What each central idea claims, and whether it carries the weight

**Compiler++: two grammatical layers, the C++ classes deriving from the C
ones.** The claim is that the C layer is honestly C and does not know the C++
layer. The reviewer grepped it: `AST`, `Parser`, `Lower`, `Lexer` — zero
`cxx::` mentions; the one leak (`IR.cpp`'s manglers, 3 mentions) is the one
the record already names. So the *name* boundary holds. The *shape* boundary
does not: the C layer exposes 21 `virtual` hooks (`Parser.h:162-171`,
`Lower.h:78-189`) — `parseFunctionTail` "a ctor's initialiser list",
`emitPrologue` "a ctor's base call", `reassertVPtr`, `cloneForeignType`,
`isBoolType` — whose only implementer is the C++ layer. `Lower.h` cannot be
read without `Lower1.h`. That is the comprehension cost, and it is what makes
a correct edit 4,600 lines rather than 2,400.

The checkability cost is sharper, and it is the reviewer's central finding:

> the C-layer `lowerAddress` is the fall-through for every expression the C++
> layer did not claim. Three of the wrong answers below (chained `operator=`,
> temporaries as operands, implicit assignment) are exactly the C-layer routine
> at `Lower.cpp:783-787` running on a C++ node because
> `cxx::Lowering::lowerLayerAddress` (`Lower1.cpp:161-185`) handles only
> `MemberAccessExpr` and a bare member name. Derivation puts the *default* in
> the base class, and the default for a C++ construct is C semantics — a byte
> copy, an "internal" error.

I read both sites. `lowerLayerAddress` handles two node kinds and returns
`false` for everything else; `lowerAddress` ends in `"internal: expression has
no address to lower"` after treating an assignment as "an lvalue; its address
is the left side's" — which runs `lowerAssign`, whose object branch is "An
object is copied byte for byte" (`Lower.cpp:1017-1022`). So the derivation's
default for a class-typed node is *C's answer, silently*. Every C++ construct
the C++ layer has not yet claimed gets a wrong-but-plausible lowering rather
than a refusal. That is the opposite of the author's rule 4 in cxx1 — *refuse
by name, and never accept quietly* — and it is built into the inheritance.

**cxx1: replay-the-tokens templates, lowering onto operations the backends
already have, three hand-written generators over one walker.** The cxx1
reviewer tested each claim against the language. Replay carried: deduction,
partial specialisation, class templates, `sizeof(T)`, a template holding a
`new[]`'d buffer — "worked across everything I wrote", at the documented cost
of over-acceptance (a name declared after the template binds). Lowering
carried where the lowered thing is a value: 13 `bool` conversion shapes agree
with clang; member pointers as offsets and structs work. It *lost* where the
lowered thing carried information the primitive does not: references lowered
to pointers forget which kind of reference they were (F5: a call returning
`S &&` is an lvalue, so `S d = mv(c)` prints `copy 2 2` where clang prints
`move -1 2`), and forget that a base binds a derived (F8: `B &r = x` refused
for any `D : B`, "the single most common C++ idiom the compiler cannot
take"). The reviewer's sentence is the right one: "lowering in the parser
makes the backends simple and keeps every ABI fact local, but it moves the
*entire* correctness burden onto the parser, and the parser's checks are then
the only checks."

Three generators over one walker carried on its own terms, and the evidence
is the strongest in either review: the arm64 caller/callee classification was
re-measured with 12 aggregate shapes and an 18-argument spill, linked in all
four pairings (clang/clang, cxx1/cxx1, and each cross), and all four printed
identical output. That is a claim about the design's *checkability* that the
design made and the reviewer confirmed from outside the tree. Its cost is the
reviewer's F1: the parser hands each backend a `Cast` from `const P` to `P`;
`X86_64Linux::genConversion` (`X86_64Linux.cpp:626`, `if (!fromF && !toF) {
canonicalise(to); return; }`) and `Arm64Darwin::genConversion` both treat it
as an integer canonicalisation and truncate the object's address to its size
— `lea -4(%rbp), %rax; mov %eax, %eax; … movl 0(%rax)` — and `const P p = {
5 }; P q = p;` segfaults on both targets. "The three generators share the
fault the way three photocopies share a smudge."

So the two central ideas fail in mirror-image ways. Compiler++'s base class
gives every unclaimed C++ node a *C default*; cxx1's backends give every
parser lowering a *faithful replica*. One design's default is wrong
semantics; the other's default is wrong semantics copied three times.
Neither default refuses.

### 3.2 What each suite can and cannot see

| question | cxx1 | Compiler++ |
| --- | --- | --- |
| does this program print what clang prints? | `run.sh`: 141 cases, against clang's output **recorded once** | `run_differential.sh`: 64 of 67 `run_` cases, **host asked every run** |
| does the compiler accept/refuse what clang does? | `overload.sh`: 28 files, **live**, verdict before output; `run.sh`: 130 `.error` cases against a substring, no oracle | **never asked**: 39 `err_` cases rest on a golden exit status and message; the subset's exclusions are a `grep` |
| are the linkage names right? | `names.sh`: 141 cases vs clang for three ABIs, **live**; vs cl on the Windows box | n/a (own VM); manglers unchecked |
| is the calling convention right? | linking against clang's objects; the reviewer's 12×4 cross-link | n/a; the VM word is its own ABI |
| is the object layout right? | via the above | **checkable and not checked**: reviewer matched 5 shapes against `offsetof`/`sizeof`, nothing asserts it |
| did this refactor change the output? | `emit.sh --record`, local golden, **not in the repository** | 129 golden `-ast -layout -ir` dumps, in the repository, `--accept` re-records |
| does the object file reproduce the compiler? | n/a | `run_roundtrip.sh`: 90 cases, bugs included |
| are constructions and destructions balanced? | **no case counts them across the shapes that fail**; `by-value.cpp` declines to count constructions because oracles disagree on elision | `93_run_lifetime_balance` and 7 more cases with a `live` counter — for the shapes someone wrote |
| exit status of the compiled program? | yes (`run.sh` runs it) | **no**: `main returned 0` whatever main returned (F10, in the ledger) |
| a refusal that fires on one target only? | **no** — handover item 4, "`.error` is judged on the host target" | n/a |
| what the IR / bytecode / layout *should* be? | — | **nothing outside the tree**; goldens are the compiler's own output |
| a bug the compiler planted in itself? | mutation pass, 09-01 audit: 9 planted, **4 survived every suite** | never run |

Two rows decide the argument. Compiler++ asks the oracle about *every runnable
case on every run*; cxx1 asks it on every run about 28 of 299 and about the
names of 141, and compares the other 141 against an answer it wrote down once.
cxx1 asks the oracle about the *object* — layout, names, convention — where a
wrong answer never reaches the program's output at all; Compiler++ cannot, and
its ledger shows what that costs: `CodeGen.cpp:180` "hardcodes flag 1
(sign-extend) on every `IR_Load`", so `unsigned int v = 3000000000; v / 2`
gives `-647483648` and `(1u<<31) >> 31` passed to `print_int` prints
`4294967295`. cxx1's equivalent layer was probed with ~45 arithmetic
expressions and ten `sizeof(long)`-dependent constants on two targets and
agreed on all of them. The oracle below the answer is what makes the bottom of
cxx1 trustworthy and the bottom of Compiler++ a matter of reading.

### 3.3 Which position is better to be in, and for which bug

For a bug in the *machine* — convention, layout, name, word size — cxx1's
position is categorically better: the bug shows as a link failure or a
cross-link mismatch against a compiler the author did not write. Compiler++'s
machine bugs (the sign-extension, `main`'s return, `round()` at the edges) sit
in the ledger because someone read the code.

For a bug in the *language* — lifetime, copy, value category — the two are
level, and both are blind: every silent finding in both reviews was invisible
to every suite. Here Compiler++ is better *instrumented* (the live differential
exists, and needs only a case) and cxx1 is better *positioned* (its cases are
already valid clang input, and `overload.sh` already does verdict-then-output
for one feature; generalising it is a loop). The cxx1 reviewer's own words:
"the differential runs (`run.sh`) only cover what someone wrote a case for."
The Compiler++ reviewer's: "the golden-file suites are an inventory, not a
specification, and the one external oracle checks answers only."

So the honest ranking is: cxx1 for the class of bug it has already conquered
and Compiler++ cannot reach; a tie for the class both leak; and cxx1 the
better base to build from, because its missing instrument is a shell script
and Compiler++'s is a different backend.

## 4. The shape of the flaws

### 4.1 The twin path

Lay the ten silent findings side by side and one shape accounts for nine of
them: a rule written once for one of two symmetric paths, applied to that path,
pinned by a regression case for that path, and never applied to its twin.

| rule | the path that has it | the twin that does not | tree, finding |
| --- | --- | --- | --- |
| a destructor destroys the members | implicit destructor: `synthesizeDestructor`, `ParserClass.cpp:985-1060` | a *written* destructor: `ParserStmt.cpp` ~2364-2376 appends base destructors, not members | cxx1 F2: `+1 ~S` for `+1 ~S -1` |
| a class temporary is registered for destruction | `T(...)`: `ParserExprNew.cpp:114,170` pushes `pendingTemps_` | a class *returned* by a call: `completeCall`, `ParserExprCall.cpp:275` allocates the slot and never pushes | cxx1 F3: `+1 k=2 \| +2 yes \| +3 \|` for `+1 -1 k=2 \| +2 -2 yes \| +3 -3 \|` |
| an rvalue reference is an xvalue | direct `static_cast<S &&>`: `setXvalue`, `ParserExpr.cpp:243` | a reference *returned* by a call: `useReference`, `ParserExpr.cpp:1068` lowers to `*ptr` and forgets | cxx1 F5 |
| copying ignores top-level const | `checkAssignable`, `ParserOverload.cpp:625` | `convert`, `ParserOverload.cpp:53-100`, falls through to a `Cast` | cxx1 F1 (crash) |
| skip a member that belongs to a base | destructor and copy paths use `memberFromBase()` (`ParserClass.cpp:551`) | both constructor loops (`ParserClass.cpp:1317`, the `isCtor` block in `ParserStmt.cpp`) | cxx1 F6 (refusal) |
| a converting constructor is an implicit conversion | at a declaration: `S s = 5;` | at a call: `rankArgument`, `ParserOverload.cpp:200` | cxx1 F9 (refusal) |
| a class is copied memberwise | copy *constructor*: `Semantic::synthesiseCopyConstructors` | copy *assignment*: no `synthesiseCopyAssignment`; `Lower.cpp:1017` byte-copies | Compiler++ F1: `46` + double free for `440` |
| a reference slot holds an address | a *local* reference: `Lower.cpp:747-748` | a reference *member*: `Lower1.cpp:1051-1054` stores the value, `:161-172` reads the slot | Compiler++ F2: `58` for `89` |
| `T b = T(1)` constructs in place | a *local*: `Lower1.cpp:417-427` | a *global*: `initGlobal`, `Lower1.cpp:603-609` ignores `vd->init` | Compiler++ F3: `0` for `11` |
| `a = b` calls user `operator=` | at *statement* level: `Lower1.cpp:191-192` | as an *operand*: no `BinaryExpr` branch in `lowerLayerAddress`, so `Lower.cpp:783` | Compiler++ F4: `23` for `34` |
| a temporary has an address | the *value* path: `Lower1.cpp:247-257` builds `$temp` | the *address* path: `lowerLayerAddress` has no `TempExpr` | Compiler++ F6 (internal refusal) |

The only silent finding outside this shape in either tree is Compiler++'s F5,
the ignored `f` suffix — a lexer fact recorded (`Lexer.cpp:314`) and then not
consulted (`Lower.cpp:800-801`), which is the same disease one level down.

Both reviewers noticed and said so, independently and in almost the same
words. cxx1: "The 09-02 handover's 'open, silent' item — a written
*constructor* not building an unnamed member — was fixed in `3058a89`; this is
its mirror on the destructor, and the commit message of `3058a89` even
describes the old symptom as 'and then ran ~M on it from the destructor the
compiler wrote' — true only for the implicit one." Compiler++: "This is the
assignment twin of the closed ledger entry 'A class holding an array was
copied byte for byte …' The fix was made for construction and not for
assignment." And on F2: "Compare the local-variable path at
`Lower.cpp:747-748`, which does: 'A reference's slot holds another object's
address …' That sentence was never applied to fields."

cxx1's `CLAUDE.md` has a rule for exactly this — rule 3, *an invariant a
person has to remember is a defect with a delay on it* — and names three
instances it made structural. The tree that wrote the rule down has as many
live instances as the tree that did not. The rule is right; it is not yet
applied where the lifetime rules live. The cxx1 reviewer priced it: "Four of
the five are one-line-shaped and would be cheap to make structural (one
`constructMember` entry that skips base members itself; one place that
allocates a class-typed result slot and registers it; one `destroyMembers`
called from both destructor paths)."

Why the suite never sees a twin: the regression case for the fixed path pins
that path. `98_run_temporaries` and `110_run_return_by_value_copies` "between
them never return a `T(...)` from a class with a user copy constructor"; cxx1's
"functional-cast temporary `T(62).v` *is* destroyed correctly, which is why
the case suite's temporaries look right." A case written to prove a fix
proves that fix.

### 4.2 What each design makes structurally likely

**Compiler++'s default-inherited byte-copy path produces** silent C semantics
for any C++ node the C++ layer has not claimed — a byte copy where a user
`operator=` should run, a value where an address should be stored, a
"Nothing to call" branch where a constructor should be called — and, when even
C has no answer, `error: internal: expression has no address to lower` with no
construct named. Three of five silent findings and the internal refusal run
through two lines of `Lower.cpp`. The reviewer's whole finding set "clusters
at the seam between the C layer's byte-copy defaults and the C++ layer's
objects." That is not an accident of this week; it is where derivation puts
the default.

**cxx1's parser-lowers-everything with three faithful backends produces** a
wrong lowering reproduced identically on three targets (F1, the truncated
address, on x86_64-linux and arm64-darwin alike), a value-category or
binding fact discarded at the lowering site and unrecoverable after it (F5,
F8), and — when the parser accepts a shape the backends cannot take an
address of — a backend refusal with no position and two spellings (F10:
`codegen: the address of this expression is not supported yet by the
arm64-darwin backend` at `Arm64Darwin.cpp:231`, `codegen: this has no
address` at `X86_64Linux.cpp:304`). The tree's stated house bug class is
divergence *between* targets; the reviewer looked for it (ten constants, two
targets; twelve shapes, four pairings) and found none. What it found instead
is agreement on a parser error, which the cross-target design "cannot catch,"
and which is the exact complement of the house rule: "the design catches
divergence *between* backends and cannot catch agreement *on a parser error*."

**The same program, two designs, one failure.** cxx1 F10:

```cpp
struct T { int v; T(int v) : v(v) {} T(const T &o) : v(o.v) {} ~T() {} };
T mk(int v) { return T(v); }
```
→ `codegen: this has no address`. Compiler++ F6(d), "with a user copy
constructor declared: `return T(k);`" → `error: internal: expression has no
address to lower`. Two compilers with no code in common, one shape — a
prvalue of a class with a user copy constructor in a position that needs an
address — and one failure: the front end accepted an AST the lowering cannot
take an address of, and the refusal came from the layer that does not know
the construct's name. In cxx1 this is the one finding outside the refuse-by-
name discipline; in Compiler++ it is the flagship of the layer fall-through.
Both trees had fixed the *other* spelling: cxx1's "Class temporaries: one
gap, three symptoms" section records `return P(1)` as mended, and
Compiler++'s ledger's closed entry "A class returned by value was copied with
`memcpy`, so its copy constructor never ran" is what "made the copy
constructor a caller and did not give the temporary an address." A fix, and
its twin.

**What differs in what leaks.** cxx1's five silent findings are all in class
semantics — lifetime, value category, a downcast through a non-first base.
Its scalar, ABI and arithmetic layers were probed and held. Compiler++'s leak
reaches the scalar layer (the `f` suffix; the ledger's sign-extension; `main`'s
return), because nothing below the answer is checked. cxx1's leak reaches
*refusal at the wrong layer* more often (F10, and six wrong-name refusals)
because its parser is the only checker and its refusals are the only
specification. In both, the most consequential *refusal* is not a feature but
an idiom: cxx1 cannot bind `const Base &` to a derived object (F8);
Compiler++ cannot bind `const T &` to an rvalue (ledger, "the most common
idiom in the language being targeted"). Two different gaps in reference
binding, one consequence — neither compiler can take the most common way a
C++ function is handed an argument.

### 4.3 The refusal disciplines, both holding at the centre and both broken at the edge

cxx1's rule is *refuse by name, at the point of interception*; measured: 478
`src_.fail` sites, 60 saying "not supported yet", "nearly all naming the
feature and often the reason." The reviewer calls it "the tree's best property
for a reader." Where it breaks: six ordinary C++11 constructs refused with a
message about something else (F6 "does not name 'm' in its initialiser list"
about a *base's* member; F7 `'V::V' is not a const member function` for
`V(x + 1)` inside V; F8 "a reference binds to the type it names" for a
derived-to-base bind; F11's `expected a type` for `G g1(1);` at file scope),
plus F10 from the backend. None of the six is in `CONFORMANCE.md` or the
"Ordinary C++ this refuses" section — which is the section built to hold
exactly this kind, and which caught three earlier ones the same way.

Compiler++'s rule is *one mistake costs one line*, an excluded construct
"lexed, named once, and skipped"; measured: 40 sites, 37 messages in three
files, and a ledger table derived by grepping them. Where it breaks: six
constructs refused without being named at a cost of 4–7 cascading errors each
— `10u` gives five ending in `undeclared identifier 'u'`, `.5` gives seven,
unary `+` six, `G ga(1);` four — and two rows of its own exclusion table
(`operator`, `friend`) say "not supported" about things it supports. The
reviewer's count: "the boundary is therefore enforced in one *kind* of place
(the point of interception, per the house rule) but not in one place, and the
honest list is the grep plus this table."

Same shape again: six each. Both disciplines are real where the author
expected a construct to arrive, and absent where the construct arrived by a
road nobody pictured — an integer suffix, a global with constructor arguments
(refused wrongly in *both* trees: cxx1 F11 `expected a type`, Compiler++ F3
`expected a parameter type`), a base binding a derived. A refusal discipline
is a list, and a list is complete only for the things on it.

*Measure rather than read* holds in cxx1 wherever there is an oracle to
measure against — the `Abi` fields each carry their measurement, the manglers
were "measured, not read," and the reviewer's re-measurement found them true —
and does not hold for lifetime, where there is no oracle and the facts were
read. It holds in Compiler++ for the answer of a program and for nothing else;
the layouts that *could* be measured were not, though they turned out right.

### 4.4 What the suites are constitutionally unable to see

Not "did not happen to see" — unable, by construction:

- **cxx1 `run.sh`** compares against clang's output *recorded once*. It can see
  a regression against the recording; it cannot see that the recording was
  wrong, and it counts destructors nowhere, so F2 and F3 — a leak of whatever a
  destructor releases — are outside its universe. `emit.sh` "compiles every
  case for three targets and counts what compiled"; its golden is local and
  "the assembly of HEAD is not itself a checked-in fact." No suite can pin a
  refusal for one target only.
- **Compiler++ `run_tests.sh`, `run_exec.sh`, `run_roundtrip.sh`** compare
  the compiler with what the compiler said last time, or with itself. Every
  `err_`, `warn_`, `trap_` and dump-only case "rests on a golden the maintainer
  read once." `run_differential.sh` compares output, not status, so a wrong
  `main` return is invisible; it runs only `run_` cases, so a valid program
  wrongly refused (F7, the promotion ranking) or an invalid one wrongly
  accepted (the ledger's `A b = p;`) is never asked of the host.
- **Both**: the shape a case was not written for. The Compiler++ record says
  it outright — "The suite passes while the compiler is wrong … every defect in
  KNOWN-GAPS.md was found by reading or by adversarial probing, never by the
  suite" — and cxx1's audit measured it: nine planted bugs, four survived.

## 5. The verdict, stood behind

The criterion is the author's: how much of what the compiler claims can a
reader confirm against something outside the tree. On that criterion cxx1 is
the better-built compiler, because the thing it chose to conform to — two real
ABIs, three real targets — is what gives it an oracle at the level where
Compiler++ has none, and because the reviewer went and used that oracle and it
held. Its cost is real and measured: a parser that lowers everything is the
only checker of everything it lowers, and five of its lifetime rules are wrong
today in ordinary C++11. But those five are the same five shapes that are
wrong in Compiler++, at the same seam, for the same reason, in a tree with a
third of the surface and no prior audit. The seam is not a property of either
design. It is a property of how this author fixes a bug — one path, one case
— and it will keep producing twins in both trees until the case that is
written for a fix is the case that counts *balance* rather than the case that
reproduces the report.

Compiler++ is the more comprehensible artefact and the better-shaped harness:
six one-script suites with the assertion beside the check, a ledger ranked by
whether the compiler lies, an oracle asked on every run about every runnable
case. Those are things to take, not reasons to prefer it. What it cannot do is
grow an oracle below the answer, and its ledger already holds the bug that
proves why one is needed.

If the two are asked as one question — which tree would I rather be handed to
make correct — cxx1, because the instrument it lacks is one it has already
built once for a smaller suite, and the instrument Compiler++ lacks is a
different backend.

## 6. What each could take from the other

Concrete, small, and already existing in the other tree. Evidence beside each.

### cxx1, from Compiler++

**First: a lifetime-balance case family, on the pattern of
`tests/cases/93_run_lifetime_balance.cpp`.** One `live` counter incremented in
every constructor and decremented in every destructor, printed after each
shape and asserted back to zero. Its header says why it exists: "the test that
would have caught by-value parameters, return temporaries, member arrays and
globals in one go, since none of them were being destroyed." cxx1's
`by-value.cpp` declines to count constructions — "a case that counted them
would be recording one compiler's choice as though it were the language's" —
and the reviewer's diagnosis is that "the side effect is that nothing counts
destructions where they do not." A *balance* is elision-invariant: whatever a
compiler elides, it elides the construction and the destruction together, so
`after 0` is the same on clang, cl and cxx1 while `returned 107` is not.
Compiler++'s case shows the split in practice — it is on the differential's
allowed list for the copy count and not for the balance lines. The shapes to
put in it are the review's: a written destructor with members (F2), a class
returned into an operand, a condition and a discard (F3), the MI tail, `T t =
T(5)` (F12), `delete` through a base. Three of the five silent findings and
the documentation finding show as a nonzero line. This first because it is
the cheapest instrument that sees the class of bug the tree actually leaks,
and because it turns the tree's rule 5 on the one subject where the tree has
no oracle.

**Second: one ledger, ranked by whether the compiler lies.** `KNOWN-GAPS.md`'s
three tiers — accepts-and-miscompiles, accepts-what-it-should-reject,
rejects-what-it-should-accept — with the instruction to fix tier one first and
"resist" tier three. cxx1 has three homes today (`CONFORMANCE.md`, the
"Ordinary C++ this refuses" section, the newest handover's open list) and
F6–F11 are in none of them; the reviewer's F8 is "the single most common C++
idiom the compiler cannot take" and it is recorded only as a code comment
(`ParserOverload.cpp:206-209`, "More is a rung of its own"). A ranked ledger
also gives the stale status table a replacement that is not a number.

**Third: `overload.sh`'s discipline over the whole corpus.** Compiler++'s
`run_differential.sh` asks the host about every `run_` case every run. cxx1's
cases are already valid clang input by design ("A case in `tests/cases/` can
be handed straight to `clang++ -x c++ -std=c++11` now"), and `overload.sh`
already does verdict-then-output for 28 files and "found a real bug the moment
it was written." Looping it over 271 turns 141 recorded answers into live ones
and makes every `.error` case a verdict comparison. The Mac-only cost is
already paid by `names.sh`.

Small and evidenced: `run_driver.sh`. cxx1's own design review noted "the
Driver's own options (`-j`, `-g`, `-D`, `-masm=`) are exercised by nothing";
Compiler++'s nine-assertion runner, check beside the argument line, is the
shape.

### Compiler++, from cxx1

**First: refuse at the layer seam.** cxx1's rule 4 — *refuse by name, and
never accept quietly* — applied to the two lines the reviewer traced three
silent findings and the internal refusal through: `cc::Lowering::lowerAssign`'s
`isObjectType` byte-copy branch (`Lower.cpp:1017-1022`) and `lowerAddress`'s
`"internal: expression has no address to lower"` (`Lower.cpp:787`). A
class-typed node that reaches the C layer's default should be refused with the
construct's name and position — "copy-assignment of a class whose member
defines `operator=` is not supported", "a class temporary as an operand is not
supported" — never byte-copied and never called internal. This converts F1 and
F4 from lies into refusals and F6 into a positioned one, and it is structural
in the sense cxx1's rule 3 means: the base class's default for an object stops
being C's answer and starts being a question the C++ layer must have
answered. Two sites. This first because it is tier one of the tree's own
ledger, and because the seam it closes is the one every finding clustered at.

**Second: the verdict half of the differential.** cxx1's `overload.sh`
"compares the verdict before it compares the output, and that half is what
catches bugs." Compiler++'s differential runs only `run_` cases, so no
`err_` case is ever put to the host. Run them: an `err_` case the host also
refuses is an ill-formedness check; one the host *accepts* must carry a marker
naming the exclusion — which makes the subset's exclusion list a checked
artefact of the corpus instead of "the grep plus this table," and which is
where the two wrong rows (`operator`, `friend`) and F7's promotion ranking
would have shown up. It also answers tier two of the ledger, which the suite
cannot see at all today.

**Third: a `differential_layout.sh`, and one mutation pass.** The reviewer
measured five class shapes against the host's `offsetof`/`sizeof` — sizes 24,
4, 16, 24, 16, offsets identical — and priced the runner at twenty lines. That
is cxx1's "measured, not read" applied to the one fact below the answer that
Compiler++ *can* put to an outside compiler, and the tree does not assert it
anywhere. And the number cxx1 has and Compiler++ does not: nine planted bugs,
four survived. A mutation pass would put a figure on "the suite passes while
the compiler is wrong," which the record states and has never measured.

One thing neither should take from the other: cxx1's 5,063-line record is not
a model for Compiler++, and Compiler++'s 267-line one is not a model for cxx1.
The notebook exists because cxx1 has facts to re-measure; the map exists
because Compiler++ has a shape to learn. Each is the right document for the
tree it is in. What both need is the same one small thing: a suite that
writes its own count into the line of prose that quotes it.
