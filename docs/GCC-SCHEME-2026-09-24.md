# Following GCC's optimization scheme in cxx1: a study and a layout

Research and design only, 2026-09-24. Nothing in the repository was edited.
Read against branch `opt` at `101426c` in `~/Documents/Claude/cxx1dev/C++`.

The question: *what does it take to follow GNU g++'s optimization scheme, as its
open-source documentation describes it, and what transformation layout should
cxx1 follow next?*

The short answer, argued below: cxx1 already took the one decision that matters
(`docs/OPTIMIZER-IR.md`: an RTL-shaped IR below the walker, the walker standing
in for GCC's expander). What follows GCC from here is (a) the RTL pass order,
including GCC's `web` pass in place of SSA, (b) a global register allocator in
GCC's position, last, and (c) an inliner with a cost, which GCC has and cxx1's
-O2 has not. One item of the existing design should change: "SSA over MIR"
becomes "webs and def-use chains", which is what GCC's own RTL level does.

Sources (design read, nothing copied; GCC is GPL-3, cxx1 is not):

- GCC Internals, *Passes and Files of the Compiler*
  <https://gcc.gnu.org/onlinedocs/gccint/Passes.html>, with its sub-chapters
  *Gimplification pass*, *Pass manager*, *IPA passes*, *Tree SSA passes*
  <https://gcc.gnu.org/onlinedocs/gccint/Tree-SSA-passes.html>, *RTL passes*
  <https://gcc.gnu.org/onlinedocs/gccint/RTL-passes.html>.
- GCC Internals, the IR chapters *GENERIC*, *GIMPLE*, *Tree SSA*, *RTL*
  (<https://gcc.gnu.org/onlinedocs/gccint/GENERIC.html>, `.../GIMPLE.html`,
  `.../Tree-SSA.html`, `.../RTL.html`).
- GCC Manual, *Options That Control Optimization*
  <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html> (the -O1/-O2/-Os
  flag lists).
- `gcc/passes.def` on the GitHub mirror
  <https://github.com/gcc-mirror/gcc/blob/master/gcc/passes.def> (the pipeline
  order, read 2026-09-24).

---

## 1. GCC's scheme in brief

### 1.1 The IR levels

| Level | What it is | Where it is made | What runs on it |
|---|---|---|---|
| **GENERIC** | The front end's language-independent tree: expressions, statements, declarations, types. C++ front end lowers its own trees to it. | Parser | Nothing optimizing; a few front-end folds. |
| **GIMPLE** | GENERIC gimplified: three-address statements, temporaries for every subexpression, no nested expressions, `if` flattened to conditional jumps, EH regions explicit. Target-neutral. | `pass_lower_cf`, `pass_lower_eh`, `pass_build_cfg` (the "lowering passes") | Early IPA, then all the *Tree SSA* scalar and loop passes. |
| **GIMPLE-SSA** | GIMPLE with every scalar in SSA form (PHIs at joins, one definition per name), over a CFG with dominators; memory is *not* in SSA (virtual operands). | `pass_build_ssa` | ccp, forwprop, fre/pre, dce, dse, sra, vrp, phiopt, lim, ivopts, tail calls, ... |
| **RTL** | Target-shaped instruction patterns (from the machine description) over an unbounded set of *pseudo registers* plus the hard registers the ABI pins. Not SSA. Mode (width) on every operand. | `pass_expand` (out of SSA, then expand each statement through the target's `insn` patterns) | cse, fwprop, cprop, gcse/pre, loop2, web, combine, ifcvt, sched, **IRA+LRA (register allocation)**, postreload cleanups, peephole2, block reorder, final. |

The reason for four levels: GIMPLE-SSA is where value-based reasoning is cheap
(one definition per name, so propagation and redundancy elimination are table
lookups), and RTL is where the target's shape is known (addressing modes,
two-address instructions, flags, calling convention), so instruction selection,
combining and register allocation see real costs. GCC pays for the split with an
expander in between.

### 1.2 The pass pipeline and why it is ordered as it is

From `passes.def`, reduced to the passes that move code size or speed for
ordinary C++ (warnings, sanitizers, OpenMP, profile instrumentation, Graphite
and vectorization left out):

| Phase | Passes in order | Why here |
|---|---|---|
| **Lowering** | lower_cf, lower_eh, build_cfg, build_cgraph_edges | Structure first; everything after wants a CFG. |
| **Early local (per function, before IPA)** | build_ssa; early_inline; ccp, forwprop, sra_early, fre, early_vrp, dse, cd_dce, phiopt, tail_recursion, local_pure_const, split_functions | Cheap cleanup *before* the inliner measures bodies, so its size estimates are of optimized code. `early_inline` inlines the always-profitable calls first. |
| **IPA (whole unit)** | ipa_icf, ipa_devirt, ipa_cp, ipa_sra, ipa_fn_summary, **ipa_inline**, ipa_pure_const, ipa_modref, ipa_reference | The inliner decides once, with summaries of every function and a growth budget; what it learns about purity and side effects feeds the later scalar passes. |
| **Main scalar (per function, SSA)** | ccp, forwprop, fre, thread_jumps, vrp, dse, dce, copy_prop, phiopt, tail_recursion, ch (loop header copy), sra, dominator, reassoc, ccp, lim, pre, sink, dse, dce | Propagate, then remove redundancy, then remove dead; repeated because each opens the other. `sra` sits before the second round so the scalars it makes get propagated. |
| **Loops (SSA)** | unswitch, scev_cprop, iv_canon, complete_unroll, iv_optimize, lim | After the scalar passes have simplified the bodies. |
| **Late scalar** | reassoc, slsr, fre, dominator, vrp, ccp, dse, dce, forwprop, sink, phiopt, store_merging, cd_dce, **tail_calls**, uncprop | Clean up after loops; tail calls last, when the return sequence is final. |
| **Expand** | nrv, lower_switch, cleanup_cfg, **expand** | GIMPLE-SSA to RTL: out of SSA (coalesced copies), then target patterns. |
| **RTL early** | jump, lower_subreg, **cse, fwprop, cprop, pre/gcse, hoist, store_motion, cse_after_global_opts, ifcvt** | The same trio (propagate, redundancy, dead) again, now that the target's addressing and immediates are visible. |
| **RTL loops** | loop2: move_loop_invariants, unroll_loops, doloop | Invariants that only appeared after expansion (address arithmetic). |
| **RTL mid** | **web**, cprop, cse2, dse1, fwprop_addr, ud_rtl_dce, ext_dce, **combine**, late_combine, if_after_combine, jump_after_combine, split_all_insns, sched1 | `web` splits each pseudo into one pseudo per independent live range so the allocator can colour them apart. `combine` merges 2-3 dependent instructions into one pattern (the load-into-operand and add-into-displacement folds). |
| **Register allocation** | **ira, reload/lra** | Last of the pseudo passes: everything above it is simpler with unbounded registers, and the allocator's costs are best once the code is final. |
| **Post-reload** | postreload_cse, gcse2, ree, compare_elim, thread_prologue_and_epilogue (shrink-wrap), dse2, **stack_adjustments**, jump2, **peephole2**, regrename, fold_mem_offsets, **cprop_hardreg**, fast_rtl_dce, reorder_blocks, sched2 | Clean up what spilling and reload created; the prologue/epilogue placed only now, once it is known which registers are saved. |
| **Final** | compute_alignments, shorten_branches, dwarf2_frame, final | Layout, then text. |

The recurring shape: *propagate → remove redundancy → remove dead*, run at each
level and repeated, with the expensive structural decisions (inlining, register
allocation) placed where their inputs are most settled: inlining after early
cleanup, allocation after everything else.

### 1.3 What -O1 and -O2 turn on (the ones that matter for size and speed)

| | -O1 | -O2 adds | -Os |
|---|---|---|---|
| **Propagation** | tree-ccp, tree-copy-prop, tree-forwprop, tree-dominator-opts, cprop-registers, forward-propagate | tree-vrp, thread-jumps, cse-follow-jumps, cse-skip-blocks | as -O2 |
| **Redundancy** | tree-fre | tree-pre, gcse (+gcse-lm), code-hoisting, rerun-cse-after-loop, store-merging, tree-tail-merge, crossjumping | as -O2 |
| **Dead** | tree-dce, tree-dse, dce, dse | tree-builtin-call-dce | as -O2 |
| **Scalarization** | tree-sra, tree-ter, tree-coalesce-vars | ipa-sra | as -O2 |
| **Control** | if-conversion, if-conversion2, ssa-phiopt, tree-ch, reorder-blocks | optimize-sibling-calls, tree-switch-conversion, reorder-blocks-algorithm=stc | no stc |
| **Loops** | move-loop-invariants, ivopts, tree-scev-cprop | finite-loops, loop vectorize (very-cheap) | as -O2 |
| **Inlining** | inline-functions-called-once | inline-small-functions, inline-functions, indirect-inlining, partial-inlining, ipa-cp, ipa-icf | inline-functions tuned for size |
| **Registers / frame** | omit-frame-pointer, shrink-wrap, combine-stack-adjustments, defer-pop, split-wide-types | caller-saves, ipa-ra, lra-remat, schedule-insns(2) | as -O2 |
| **Late** | compare-elim, cprop-registers | peephole2, expensive-optimizations, align-functions/jumps/labels/loops | no alignment |

Two things to take from the table. First, GCC's -O1 is already the *whole
structure* (SSA passes, inlining of called-once functions, full register
allocation, frame-pointer omission); -O2 adds the global/partial redundancy
passes, the size-growing inliner, and the layout passes. Second, **-Os is -O2
minus alignment and block layout, with inlining costed for size** - the same
pipeline with a different cost. That is exactly the stance `docs/OPTIMIZER-IR.md`
takes for cxx1's -O1 (cl's /O1 = /O2 with /Os), and it is confirmed rather than
contradicted by GCC.

---

## 2. Gap table: GCC's stages against cxx1 today

Files are under `src/backend/` unless said. "Payoff" is for the workload named
in the brief: Compiler++, call-heavy C++, x86_64-windows, judged against cl.

| GCC stage / pass family | cxx1 today | Status | Payoff for cxx1 | Note |
|---|---|---|---|---|
| GENERIC | `src/Ast.h`: typed AST | have | - | cxx1's AST is its GENERIC. |
| GIMPLE / lower_cf / lower_eh / build_cfg | none; the walker (`Walker.cpp`, `X86_64Linux.cpp`) goes AST → instructions directly, as a stack machine | missing | small now | The three ABIs' knowledge is in the walker; a GIMPLE would duplicate the expander. Deliberately not built (section 3). |
| build_ssa (SSA) | none | missing | small at RTL level | GCC's RTL is not SSA either; see section 3.2. |
| Early inline / early cleanup before inlining | `X86_64Linux::inlineTarget` + `small()` (`X86_64Linux.cpp:1811-1826`): `statementsIn <= 8`, no landing pads, no variadic, no stack args, **every site, no budget** | partial | **large** (size): -O2 .text is 854,782 vs -O1 600,302; cl /O2 623,092 | The one decision GCC makes with a cost and cxx1 makes without. |
| ipa_inline budget / fn_summary | none | missing | **large** | Needs a per-function size estimate and a per-caller growth cap. |
| ipa_icf (identical code folding) | LINK's `/OPT:ICF` (the user's linker, merged 2026-09-22) | have (at link) | done | cl also does it at link; nothing to add in cxx1. |
| ipa_cp, ipa_sra, ipa_pure_const, ipa_modref | none | missing | small-medium | pure/const knowledge would let FRE keep a member load across a call; workload is call-heavy, so real but second-order. |
| ipa_devirt | none | missing | small | Compiler++ is virtual-light. |
| tree-ccp / copy_prop / forwprop / cprop_hardreg | `OptValues.cpp forwardValues` (per block; constants, frame addresses, conditions, unknown-equality) | partial | medium | Block-local. Across blocks needs the dominator walk (GCC's `dominator` / `cse-follow-jumps`). |
| tree-fre / RTL cse | `forwardValues` recognizes equal unknowns within a block | partial | medium | Full value numbering over the dominator tree is the -O1 piece still missing. |
| tree-pre / gcse / hoist | none | missing | small | Little partial redundancy in this code; measure before building. |
| tree-dce / dce / ud_rtl_dce / ext_dce | `OptDead.cpp removeDead` (+ the sign-extension case), `removeUnreachable` (`OptCore.h`) | have | - | Global liveness over the CFG, already a fixpoint. |
| tree-dse / RTL dse | `OptMemory.cpp removeDeadStores` (frame slots, whole functions only, "no address reaches it") | partial | medium | Not in functions cut by a funclet, which is most real C++ functions with a `try`. |
| tree-sra | `OptFrame.cpp promoteLocals`: 4/8-byte scalar locals only | partial | medium | Small aggregates by value and `this`-members stay in memory. |
| tree-ter / expand into pseudos | the walker's stack machine: `push`/`pop` temporaries; `OptValues coalesceCopies` pairs some | partial | **large** (speed) | GCC never materializes a temporary in memory; cxx1 pushes it, then the optimizer pairs the pop. Stage 1 of the IR doc promises these as pseudos; `MirWebs.cpp` today makes pseudos only of *register* webs (`candidate()` = GPRs minus rsp/rbp). |
| Parameters in pseudos | "Parameters into their slots" (`X86_64Linux.cpp:1606`): every parameter is stored to its home slot and reloaded | missing | **large** (speed) | cl keeps unaddressed parameters in registers from entry. |
| phiopt / ifcvt (setcc, cmov) | conditions tracked as values (`OptValues Value::Condition`); no cmov | partial | small-medium | GCC has if-conversion at -O1; x86 `cmov` where both arms are cheap. |
| tail_recursion / tail_calls (sibling calls) | none | missing | medium (speed, call-heavy) | -O2 in GCC. Needs the frame gone before the jump; possible with an rbp frame. |
| lim / loop2 move_loop_invariants | none (loop depth is only a weight: `OptFrame loopDepths`) | missing | small on this workload, ~2x on kernels | GCC has it at -O1. |
| ivopts / unroll | none; unrolling "built and measured at nothing" (memory note) | missing | small | Skip. |
| lower_switch (jump tables) | check the walker (not read here) | ? | small-medium | GCC lowers `switch` to tables/bit tests at -O2 (`switch-conversion`). |
| RTL web | `MirWebs.cpp buildWebs`: reaching definitions per register, union-find, pins by ABI/occurrence | have (stage 1) | prerequisite | Pins whole webs (5,142 of 6,412 on one case, per the IR doc): the "pin an occurrence" fix is pending. |
| combine / fwprop_addr / fold_mem_offsets | `OptValues foldLoads` (load-once → memory operand), `foldOffsets` (constant add → displacement) | have | - | This *is* combine's work at cxx1's level. |
| Register allocation (IRA + LRA) | `promoteLocals`: locals only, callee-saved registers only (2 at -O1, 5 at -O2), no interference graph, no spilling, off in frames with funclets | partial | **large** (speed): ~2x on loop kernels measured, ~3% on Compiler++ so far because parameters and temporaries are not candidates | The central missing piece. |
| shrink-wrap / prologue placement | prologue is an event rewritten with the saves (`Optimizer::improve`) | partial | small | Fine as is. |
| combine-stack-adjustments / defer-pop | shadow space once at the frame floor (`5f48757`), stack args stored into the outgoing area (`101426c`) | have | done | |
| peephole2 / shrink | `OptShrink.cpp shrink` (no REX where the upper half is unread; narrowed arithmetic) | have | - | |
| reorder_blocks / crossjumping / tail_merge | `removeUnreachable` drops jump-to-next; no block reorder, no crossjump | partial | small (size) | Crossjumping merges identical tails - measurable on exception-heavy epilogues; try after the allocator. |
| compute_alignments | measured: nothing (Optimizer.cpp comment, 843 vs 844 ms for 3,904 bytes) | decided against | none | Matches GCC's own -Os stance. |
| sched1 / sched2 | none | missing | none on x86 OOO | cl does not schedule seriously either. Skip. |
| Pass manager (gates, TODO flags, dump files) | `Optimizer::rounds`/`improve`: fixed order, "run until nothing changes", one `Level` table | partial | small but cheap | A per-pass dump (`-fdump-mir-<pass>`) would pay for itself on the first miscompile. |
| Exception edges in the CFG | none: a landing pad is a block with no predecessors (`OptCore.h FlowOf::build`), safe only because no pass runs in a function that has one | missing | prerequisite for everything in functions with `try` | GCC: `lower_eh` makes EH edges explicit and every later pass honours them. |
| Volatile | `Driver.cpp:783`: a file that writes `volatile` is compiled without -O | missing (refused honestly) | prerequisite before memory passes get stronger | GCC keeps `volatile` on the type through every level; every pass tests it. |

---

## 3. The recommended transformation layout for cxx1

### 3.1 The IR levels cxx1 should have

| cxx1 level | GCC analogue | Made where | Passes on it |
|---|---|---|---|
| **AST** (`src/Ast.h`) | GENERIC | parser | Inline *decision* (with cost) and callee size summary; nothing else. |
| **MIR** (`Mir.h`, `OptIr.h`): x86 opcodes, `Operand`s, pseudos above `kPhysical` | RTL after expand | the `Optimizer` spelling holds the walker's stream; the reader turns webs, temporaries and unaddressed locals/parameters into pseudos | Everything in 3.3 up to allocation. |
| **MIR, allocated**: only physical registers | RTL after reload | the allocator's `assign` | The post-reload passes: cprop_hardreg (= `forwardValues` again), dead saves, `shrink`, jump cleanup. |
| **Text** | final | the real spelling (`Masm`, `X86_64Linux`) | none |

This is `docs/OPTIMIZER-IR.md`'s layout and it should stand. The walker *is*
the expander: it holds the ABI knowledge GCC keeps in its machine descriptions
and `expr.cc`, and it is green on three targets. Building a GIMPLE between the
AST and the walker would mean a second expander or a walker rewritten to consume
it; the payoff (alias-aware FRE, SRA, tail recursion on a neutral form) is
second-order for this workload and every one of those passes has an RTL-level
counterpart in GCC's own pipeline that cxx1 can build instead. **Deliberate
difference #1: no GIMPLE; AST → MIR directly.** Revisit only if the arm64
reader (stage 5 of the doc) turns out to duplicate too much of x86's.

### 3.2 Where the existing design should change: webs, not SSA

`docs/OPTIMIZER-IR.md` step 2 says "SSA, over the control-flow graph and its
dominators", then "Out of SSA, copies on edges, then coalesced" before
allocation. GCC does not do this at the RTL level: its RTL passes work on
pseudos with **def-use chains** (`df`) and the `web` pass, and get cse, fwprop,
cprop, dce and combine from those. SSA over a two-address machine IR with flags
and pinned registers means PHIs for `rax` at every `idiv`, copies on every
critical edge, and a coalescing problem the allocator then has to undo.

**Deliberate difference #2 (a change to the doc): replace "SSA over MIR" with
"webs + def-use chains + a dominator tree".** `buildWebs` already computes
reaching definitions per register; keeping the def→use and use→def lists it
builds, plus dominators over `Flow::blocks`, gives:

- `forwardValues` across blocks (GCC `dominator` / `cse-follow-jumps`): walk the
  dominator tree with the value table inherited from the parent block.
- copy propagation over a web with one definition (GCC `cprop`): a def-use
  lookup.
- global value numbering (GCC `fre` at -O1): hash of (opcode, operands' value
  numbers) over the dominator walk; a redundant instruction becomes a copy.
- DCE as today, DSE with reaching stores over the same machinery.

SSA can still be added later above this if a GIMPLE level ever arrives; it is
not a prerequisite for the allocator, and the doc's stage numbering should say
so (stage 3 becomes "def-use chains and the -O1 scalar passes").

### 3.3 The ordered pass list, mapped to -O1 and -O2

Order follows GCC's RTL pipeline where it applies. "size" = -O1, "speed" = -O2;
both levels run every pass, the *costs* differ (GCC -Os = -O2 with size costs).

| # | Pass (cxx1 name / GCC name) | -O1 | -O2 | Where it is today |
|---|---|---|---|---|
| **AST level** | | | | |
| A1 | inline decision with cost (`ipa_inline` + `early_inline`) | body ≤ call sequence | budget: callee size × sites ≤ growth cap per caller, unit cap | `inlineTarget`/`small`, no cost |
| **MIR, before pseudos** | | | | |
| M1 | build CFG with EH edges (`build_cfg` + `lower_eh`) | ✓ | ✓ | `FlowOf::build`, no EH edges |
| M2 | jump cleanup, unreachable, jump-to-next (`jump`) | ✓ | ✓ | `removeUnreachable`, `dropUnnamedLabels` |
| M3 | reader: webs → pseudos (`web`); pin occurrences by copy; push/pop temporaries → pseudo copies (`expand`/`ter`); unaddressed scalar locals and parameters → pseudos (`sra`, expand) | ✓ | ✓ | `buildWebs` (registers only); `promoteLocals` (locals only, post hoc) |
| **MIR scalar, repeated to a fixpoint (rounds)** | | | | |
| M4 | forward values within a block (`fwprop`/`cprop`) | ✓ | ✓ | `forwardValues` |
| M5 | dominator-tree value numbering (`fre`/`cse`; `cse-follow-jumps` at -O2 extends over the whole tree) | ✓ local + dominators | ✓ | partial (block-local) |
| M6 | copy coalescing into the defining instruction (`combine` half) | ✓ | ✓ | `coalesceCopies` |
| M7 | fold a load read once into its use; fold constant adds into displacements (`combine`, `fwprop_addr`, `fold_mem_offsets`) | ✓ | ✓ | `foldLoads`, `foldOffsets` |
| M8 | dead code (`dce`/`ext_dce`) | ✓ | ✓ | `removeDead` |
| M9 | dead frame stores (`dse`), including cut functions once M1 exists | ✓ | ✓ | `removeDeadStores` (whole only) |
| M10 | if-conversion to setcc/cmov (`ifcvt`) | ✓ when smaller | ✓ | none |
| M11 | tail calls (`tail_calls`, `-foptimize-sibling-calls`) | ✓ (smaller) | ✓ | none |
| **MIR loops** (after scalar has settled; -O2 first, -O1 if size-neutral) | | | | |
| M12 | loop invariant motion (`lim`/`move_loop_invariants`) | measure | ✓ | none |
| **Allocation** | | | | |
| M13 | build interference over pseudos; Chaitin-Briggs colouring with coalescing; costs = uses weighted by loop depth (speed) or by encoding bytes (size); a pseudo live across a call prefers callee-saved; spill to frame slots (`ira`, `lra`) | ✓ size costs | ✓ speed costs | `promoteLocals` (a special case) |
| **MIR, allocated** | | | | |
| M14 | post-allocation forward values and dead code (`postreload_cse`, `cprop_hardreg`, `fast_rtl_dce`) | ✓ | ✓ | rounds already rerun |
| M15 | drop unused saves; prologue rewritten with the saves and the final frame (`thread_prologue_and_epilogue`, `stack_adjustments`) | ✓ | ✓ | `dropUnusedSaves`, prologue event |
| M16 | shrink encodings (`peephole2`) | ✓ | ✓ | `shrink` |
| M17 | crossjumping of identical tails (`jump2`) | ✓ | ✓ | none |
| M18 | alignment (`compute_alignments`) | no | **no** (measured: nothing) | decided |

Deliberate differences #3-#5, and why:

- **#3 No scheduling** (GCC `sched1/sched2`, -O2). x86-64 out-of-order cores
  make it worthless for this code; cl does little of it. Saves a large pass.
- **#4 No PRE/GCSE at -O2** until value numbering (M5) is in and a measurement
  shows partial redundancy left. Full redundancy on the dominator tree is most
  of what the workload has.
- **#5 The levels share the pipeline and differ in costs**, exactly as GCC's
  -Os relates to -O2 and as cl's /O1 relates to /O2, rather than GCC's -O1 (a
  subset of passes). This keeps -O1 = "small" honest: a pass that grows code
  (inlining beyond the call sequence, cmov where a branch is shorter, LIM that
  raises pressure and spills) is gated by its size cost, not switched off.

### 3.4 The driver

`Optimizer::improve` becomes a list of (pass, gate, "what it invalidates")
entries run in the order above, with `-fdump-mir=<pass>` writing the stream
after any named pass. That is GCC's pass manager reduced to what cxx1 needs:
gates (the level, "function has funclets", "function has EH edges") and the one
TODO flag that matters (`rebuild flow`). Cheap, and the first miscompile in a
new pass pays for it.

---

## 4. Staged roadmap

Each step lands and verifies alone. Gate for every step: cxx1's cases suite
(`tests/run.sh`, plus `tests/emit.sh` for the other targets) green on the Mac,
Compiler++'s suites and its 258 outputs against cl /O2 green, then the box
measurement named. Measurements are on the Windows box (`tools/crossbox`): .text
of Compiler++ at -O1 and -O2, and the bench with interleaved rounds. Sizes are
rough lines of C++14.

| # | Step | Prerequisite | What it changes | Verify by | Size / payoff |
|---|---|---|---|---|---|
| **1** | **Inliner cost and budget (A1).** Give each callee a size estimate (instruction count of its own walk, cached in `bodies_`, or the AST node count as a proxy), inline at -O2 only while the caller's growth stays under a cap (start at cl's shape: callee ≤ ~40 instructions, caller growth ≤ 2x, unit growth ≤ 25%); at -O1 only where the body is no larger than the call sequence it replaces. Keep `small()`'s safety conditions. | none | `X86_64Linux::inlineTarget`, `small`, one cost table beside `levelFor` | suites; **-O2 .text** (target: 854,782 → under 700,000 with the bench not slower) | ~150 lines; **large, size** |
| **2** | **One opcode table** (doc item 3). `isMove`, `isRmw`, `takesImmediate`, `renamable`, `suffixWidth`, `isShift`, `controlOf`'s lists become one table of {mnemonic, roles, width, flags, pins} in `OptEffects.cpp`. | none | every `is(m, {...})` list in `Opt*.cpp`, `MirWebs.cpp` | suites; assembly of Compiler++ byte-identical before/after at -O0/-O1/-O2 | ~300 lines moved; enabling |
| **3** | **EH edges in the CFG (M1)** (doc item 1). The walker tells the optimizer each try range and its pad; `FlowOf::build` gives every call inside a range the pad as a second successor; liveness then flows into pads; `removeDeadStores` may run in cut functions. | 2 | `OptCore.h Block::next`, `Optimizer::stateLabel`/new events, `OptMemory` | suites (the exception cases especially); Compiler++ vs cl outputs; .text at -O1 (dead stores in `try` functions) | ~200 lines; correctness prerequisite, small size win |
| 4 | **Pin an occurrence, not a web** (doc item 2). Before `unite`, split a pinned use/def off with a copy into/out of a fresh pseudo; measure pseudo count and pinned count on the doc's case (5,142/6,412 → most webs free). | 2 | `MirWebs.cpp occurrencesOf/buildWebs` | suites; byte-identical output with `assign(home)` still (copies coalesce back) | ~120 lines; enabling |
| 5 | **Def-use chains and dominators** (3.2). Keep what `buildWebs` computes; add dominators over `Flow`. | 3, 4 | new `MirChains.{h,cpp}` | suites; no output change | ~250 lines; enabling |
| 6 | **Register allocator (M13)** replacing `promoteLocals`: interference from liveness over pseudos, Chaitin-Briggs with conservative coalescing, spill to new frame slots below the inline region, callee-saved preference across calls and EH edges, costs by level. Rule for funclet frames: a pseudo read or written in any funclet of the function is not a candidate (stays in its slot); everything else is. | 2, 3, 4, 5 | new `MirAlloc.cpp`; `Optimizer::improve` | suites; loops.cpp kernels; bench; both .text figures (must not grow at -O1) | ~600 lines; **large, speed**; the doc's stage 2 |
| 7 | **Temporaries as pseudos (M3b).** The reader rewrites a push whose pop pairs with it into `mov` to a pseudo and the pop into a use; pushes that escape (calls with stack args, the scratch-register carry) stay. | 6 | `MirWebs.cpp` or a new `MirRead.cpp` | suites; bench; .text | ~200 lines; **large, speed** on expression-heavy code |
| 8 | **Parameters as pseudos (M3c).** An unaddressed parameter's home-slot store becomes a copy into a pseudo; the slot is dropped when nothing addresses it (Windows: the shadow space stays, its stores go). | 6 | `X86_64Linux.cpp:1606` "Parameters into their slots" gives the optimizer the slots; reader | suites; bench (call-heavy: this is where cl's lead is); .text | ~150 lines; **large, speed** |
| 9 | **Dominator-tree value numbering (M5).** `forwardValues`' table inherited down the dominator tree; a hash of (opcode, value ids) marking a recomputation as a copy of the earlier result. | 5 | `OptValues.cpp` | suites; .text; bench | ~300 lines; medium |
| 10 | **Pass driver with dumps (3.4).** | 6 | `Optimizer.cpp` | no output change; `-fdump-mir` works | ~100 lines; tooling |
| 11 | **Volatile kept.** A qualifier bit on the type (`ParserType.cpp:1322`), carried to the load/store the walker emits, marked `opaque` in `Effects`; `Driver.cpp:783`'s refusal goes. | 2 | parser type, walker, `OptEffects` | a new case that writes volatile at -O2 and checks the count of accesses in the assembly | ~120 lines; correctness, lifts the -O0 fallback |
| 12 | **If-conversion (M10)**: a diamond whose arms are one move each becomes `cmov`; a `setcc` diamond likewise. | 9 | new `OptIfcvt.cpp` | suites; .text at -O1 (must not grow); bench | ~200 lines; small-medium |
| 13 | **Tail calls (M11)** at -O2: a call in tail position with no destructors pending and no stack-argument area larger than the caller's; epilogue then `jmp`. Windows: no `try` in the caller. | 6, 8 | walker marks the call; `Optimizer` | suites (exceptions, destructors); bench | ~200 lines; medium on call-heavy code |
| 14 | **Dead frame stores in cut functions and small-aggregate SRA (M9, sra)**: 16-byte-and-under aggregates whose address never escapes split into scalars the allocator can take. | 3, 6 | `OptMemory`, reader | suites; .text; bench | ~250 lines; medium |
| 15 | **Loop invariant motion (M12)** at -O2; then measured at -O1. | 5, 6 | new `OptLoop.cpp` (natural loops from dominators) | loops.cpp kernels; bench; .text | ~250 lines; small here, 2x on kernels |
| 16 | **Crossjumping (M17)** after allocation. | 6 | `OptDead.cpp` | .text at -O1 | ~150 lines; small size |
| 17 | **arm64 reader and description** (doc stage 5). | 2, 6 | `Arm64*` | arm64 suites | ~800 lines; opens the third target |

**Do next: steps 1, 2 and 3.** Step 1 is independent, fixes the number that is
most wrong (-O2 is 37% larger than cl's /O2 for a program cl's /O2 makes smaller
than cxx1's -O1), and needs nothing below it. Steps 2 and 3 are the two
prerequisites everything after them shares, and step 3 also closes a latent
unsoundness. The allocator (step 6) is the first *speed* step and sits right
after its four prerequisites; the inliner's cost (step 1) precedes it because
the allocator's gate says -O1's size may not grow, and an uncosted inliner
would make -O2's figures unreadable.

---

## 5. Risks and traps specific to cxx1

| Trap | Where | What to do |
|---|---|---|
| **FH3 exception state at return addresses.** On Windows a state label placed after a call gets a `nop` so the return address falls inside the right state (`Masm.cpp:285 stateLabel`, `afterCall_`). A pass that deletes or moves the instruction between a call and a state label, or moves a call across one, changes which state the runtime sees. | `Entry::state`, `Optimizer::stateLabel` | Treat a state label as a barrier for motion (calls never cross one in either direction); DCE may delete between call and label only because the spelling re-decides the `nop` at output. Add a case that throws from the last call before a state change. |
| **Funclets cut from the stream mid-function.** `beginFunclet`/`closeFunclet` call `settle()` (`X86_64Linux.cpp:1876,1905`): the function reaches the passes in pieces, `cut_` set, and whole-function passes (`promoteLocals`, `removeDeadStores`) are skipped. A whole-function allocator cannot see across the cut. | `Optimizer::settle`, `cut_` | Allocate *before* the first cut, with the rule in step 6 (pseudos a funclet touches stay in memory); or hold the pieces and allocate at `functionEnd` with the funclet's text re-cut afterwards. The first is simpler and is what the doc leaves open. |
| **A landing pad has no predecessors.** Today's flow makes each pad an entry block with `kAllRegs` live-in only where `leaves` is set; a pass in a `try` function would treat values live into the pad as dead. | `OptCore.h FlowOf::build` | Step 3 before any pass runs in such a function. |
| **The frame layout: outgoing area at the floor, shadow space once.** Stack arguments are stored rsp-relative into the outgoing area (`101426c`), pushes may be carried in a scratch register (`62ea925`), and `rep movsq` at -O1 clobbers rdi/rsi/rcx. | `X86_64Linux.cpp:1043-1075`, `Optimizer::copiesByString` | `rsp` and `rbp` stay `frameReg` (never renamed); the reader must not turn a push that feeds the outgoing area into a pseudo (step 7's escape rule); `rep movsq`'s implicit registers are pinned occurrences (step 4). |
| **Volatile dropped by the type system.** Any file writing `volatile` is compiled at -O0 (`Driver.cpp:783`). Stronger memory passes (steps 9, 14) make the fallback matter more, not less. | `ParserType.cpp:1322` | Step 11 before step 14. |
| **Pinned registers pin whole webs.** One `idiv`, `cqo`, shift-by-`%cl` or argument register pins its whole web; the allocator then has nothing to colour. | `MirWebs.cpp Sets::pin` | Step 4, measured by pseudo count. |
| **Partial writes and widths.** `movb`/`movw` keep the upper bytes (`keeps`); a 32-bit write zeroes the upper half; the x87 stack is not modelled (`Reg.width == -1`). | `OptIr.h`, `occurrencesOf` | The opcode table (step 2) makes width and keep explicit per mnemonic; x87 stays opaque. |
| **Flags as a resource.** `setcc`/`cmov` if-conversion (step 12) and any motion must respect `flagsRead`/`flagsWritten`; `sub`/`cmp` reordering changes flags. | `Effects.flags*` | Already modelled in liveness; keep every new pass asking `Effects` and nothing else. |
| **The inline region.** Inlined callees share one region below the caller's locals (`inlineBegin`), so no slot there is one variable's and none may be promoted (`f2d2bae`). | `Optimizer::inlineBegin`, `inlineTop_` | Step 1 should give each site its own slots (the doc's item 4) so steps 6-8 can take them; until then the region stays off limits. |
| **Measurement discipline.** Loop-head alignment measured at nothing; register promotion 2x on kernels but 3% on the program. | memory notes | Every step names its measurement in the table above; a step whose measurement says nothing is reverted, as alignment was. |
| **Byte-identity gates.** Stage 1's gate (assembly byte-identical through `assign(home)`) holds only while pseudos get their home register back; from step 6 on the gate is "suites green, neither size nor speed worse", per the doc. | `Optimizer::improve` | Keep the `-O0` path byte-identical throughout; it is the reference the other two are diffed against. |
| **Licence.** GCC is GPL-3; the doc already states the design was read and nothing copied. | `docs/OPTIMIZER-IR.md` | Keep to the manuals and `passes.def` for the scheme; write every pass from its description. |
