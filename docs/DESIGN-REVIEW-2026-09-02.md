# Four design reviews, and what to do about them

Fable 5.1, four reviewers, one question each: **is this tree as understandable
as it could be, and what would make it more so?** Read-only; nothing here was
changed by them. Areas: the parser, the code generators, the front-end core
(types, AST, mangling, lexer), and the tree as a thing a newcomer reads.

Written against `d66ef16`, with the comment pass (`567c0a5`) landing after they
read - so a line number below may be off by a few, and every claim was checked
against the code and not against CLAUDE.md.

## The one finding that changes how the rest is judged

**`tests/emit.sh` does not diff assembly.** It compiles every case for all three
targets and counts what compiled; it never compares the `.s` it produced with
anything (`tests/emit.sh:29-36`). Verified here after the review reported it.

That matters because every "this refactor changes no behaviour" claim below
rests on comparing output before and after, and the suite as written cannot make
that comparison. **So the first change, before any of the others, is a golden
mode**: `tests/emit.sh --record` copies `tests/out-emit` aside, and a plain run
diffs against it and reports "N files changed" without failing. It is small, it
is safe, and it is what turns each proposal below from a claim into a
measurement.

## The parser

**Where a reader stumbles.** `topLevel` is not the top level - it is "define one
function", 1,100 lines, and three other paths call it to define one
(`replayInlineBodies`, `instantiatePending`, the out-of-line replay). A reader
asking where a constructor calls its base has no name to grep for.
`structOrUnionSpecifier` is 890 lines and lays out Microsoft bitfields twice,
verbatim. "The function being parsed" is not a type: `replayInlineBodies` saves
and restores 17 fields by hand, and three other places save different subsets of
them. Seven places assemble a constructor call; "mark this function used" is
spelled three ways, one of them the pointer arithmetic `Parser.h` warns against.

**What to do, cheapest first.** Extract `readMemInitialisers` and
`memberInitStatements` out of `topLevel` and name the primitive `defineFunction`;
make the per-function state one `FunctionScope` struct, listing in its comment
the fields deliberately *not* restored (`alive_`, `pendingTemps_`,
`staticSymbols_`); turn the one-shot handoffs (`pendingDefaults_`,
`pendingNoexcept_`, the five class-instantiation fields) into values passed
between the functions that set and read them; one `constructAt` and one
`markUsed`; two small helpers (`sameParameters`, `thisMember`) that between them
remove six loops and twenty-two hand-built `(*this).m` chains.

**Leave alone**: absolute token indices (every replay depends on them), the
by-value `Signature` discipline, `unqualifiedSpecifiers` answering void for
`Point::Point(`, the `Trial` guard's deliberate minimalism.

## The code generators

**Where a reader stumbles.** `Abi` is eleven unlabelled positionals, initialised
as `{ regs, 6, regs, 8, false, 0, 16, false, true, ... }` in three files - the
densest table of measured ABI facts in the tree, and unreadable at the point
where it is measured. `visit(Call)` carries four parallel vectors indexed by
argument. Caller and callee classify arguments in two hand copies *per target*,
which is exactly the shape of the A-01 hidden-return-pointer bug: consistent with
itself on both sides, so no suite saw it. The LSDA is written twice by hand, and
the x86 copy's comment sends the reader to the arm64 file to understand it.

**What to do.** Name the `Abi` fields with a per-target builder (C++14 has no
designated initialisers; a `static Abi sysV()` gives the same effect). Pass the
`Abi` to the parser instead of three positional bools. One `placeArguments` per
target, called by both sides of the call - this removes *intra*-target divergence
and leaves cross-target divergence exactly as visible as it is now. One LSDA
writer with a small `LsdaSpelling` struct, on the `DwarfSpelling` precedent.
Name the Windows frame invariant (`establisherOffset`) once instead of computing
`frame - slot` in four places. Move the Microsoft `try` machinery out of
`Walker` into `Masm`, where its seven no-op-default members belong.

**Leave alone**: no IR and no register allocator - the accumulator scheme is what
makes every ABI fact a local one that clang or cl can be asked about;
`Spelling` staying x86-only; the tail-composing routines, which differ in their
scratch registers on purpose; `.notarget` as the way a target says "I cannot".

## Types, AST and mangling

**Where a reader stumbles.** Seventeen accessors on `Type` forward class state
through `unqual_` by hand and three do not - and the three are safe only by
accident of when they are set. That is the shape of the `findMember` bug already
recorded. "Is it const?" is two different questions answered three ways.
`pointee_` means six things and `length_` two. `symbol()` falling back to
`name()` makes "unmangled" and "somebody forgot `setSymbol`" the same value,
which is the Windows-only link bug this round already mended once.

**What to do.** Put the state a class gains after creation into one shared
`ClassInfo` the qualified copy points at, so forwarding happens by construction
and cannot be forgotten. Add `isQualifiedItself()` and one `findDerived` helper
that bakes the skip-qualified rule into the only loop that exists. Name the six
meanings (`paramIndex()`, `memberName()`, `packPattern()`, `owner()`). Give the
two manglers a shared `signature(fn)` - twelve copies of the parameter-list tail
- and one `Mangled` result type, which collapses 28 `microsoftNames()` branches
in the parser to zero. Keep `Ast.h` one header; rename its `Local` to
`FrameLocal`, which collides with `Parser::Local` today.

**Leave alone**: the interning rules themselves, a member-function pointer
wearing the shape of a struct, `TemplateParam`/`DependentMember` as types,
references lowered in the parser, and `Source::fail` as the one choke point that
makes SFINAE possible.

## The tree as a thing to read

**There is no reading order, and the material for one already exists.** README
sends the reader to a 4,700-line CLAUDE.md in accretion order whose first two
sections are changelog entries and whose "Rung 6: exceptions, planned" heading
contradicts the table 2,000 lines above it. The proposal is 40 lines: a "Reading
this tree" section naming seven CLAUDE.md sections, the pipeline by file
(`main.cpp` → `Driver.h` → `Lexer.h` → `Parser.h`, which at 1,448 lines is the
front-end's design document and nowhere says so), one case per rung, the four
suite headers.

**Three things the compiler says that are not true.** `Parser.cpp`'s `pending[]`
list still names `friend`, `inline`, `mutable`, `namespace`, `operator`,
`template` and `virtual` as unimplemented, so a misplaced one of those is
reported as "not supported yet" - measured: `int x = template;` says exactly
that. `Masm.cpp` writes `; Generated by cc1` into every Windows `.s` and
`Dwarf.cpp` sets the DWARF producer to `cc1`. `make help` says cc1 emits System V
assembly. All are one-line fixes; the producer and preamble edits change every
emitted file by one line, so they want their own commit.

**The suites as documentation.** 261 of 263 cases open with a header comment,
and `.notarget`/`.nonames`/`.nocl` are required to state why - both good. What is
missing: no case includes any `lib/` header, so the subsystem that was once dead
for a month could be dead again; `tests/c-corpus` is the largest directory under
`tests/` and nothing runs it, so a newcomer will take it for the main suite; the
`-refused` suffix is on 46 of 125 refusal cases and absent from the rest; and the
Driver's own options (`-j`, `-g`, `-D`, `-masm=`) are exercised by nothing.

## Order to land

1. `emit.sh --record`, the golden mode. Everything else is measured against it.
2. The four one-line truths: `pending[]`, the two `cc1` strings, `make help`.
3. Name the `Abi` fields; pass the `Abi` to the parser.
4. `tests/c-corpus/README`, then the reading-order section.
5. The mechanical parser helpers (`sameParameters`, `thisMember`, `markUsed`).
6. `ClassInfo`; `establisherOffset`; the shared LSDA writer.
7. `FunctionScope` and the `topLevel` extraction; `placeArguments` last, since it
   is the only one whose wrong step is a whole-suite churn.

Nothing above is started. Each is a proposal with a first step and a way to prove
it changed nothing, which is the form this tree asks for.
