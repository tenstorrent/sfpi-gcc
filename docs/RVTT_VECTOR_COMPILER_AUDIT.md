# RVTT vector compiler audit and architecture roadmap

Date: 2026-08-25

This note records the source and behavior audit at compiler commit
`075e9f2f4b22dd08342be730d42e34060da10d4a`.  It deliberately separates
source structure, compiler-test coverage, registry accountability, silicon
correctness, and performance parity.  Those are different claims and must not
be collapsed into one percentage.

## Scope and result

`gcc/config/riscv/tt/rvtt-passes.def` registers 54 pass entries: 53 RVTT
passes and one invocation of generic GCC DCE.  The 53 target passes are
implemented across 46 pass implementation translation units containing
55,334 physical lines (`wc -l` over the sorted set of `.cc` files containing
an RVTT pass factory).  A mechanical name join finds exactly 53 distinct
`pass_rvtt_*` registrations and exactly 53 corresponding
`make_pass_rvtt_*` factories, with no missing or unregistered target factory.

Every registered RVTT pass was reviewed in pipeline order.  The audit found
no transformation whose decision is keyed to a corpus selector, source
filename, benchmark function, or coefficient fingerprint.  Distinctive
selector names in production files occur primarily in comments that cite
evidence; `signbit` also denotes a typed semantic/descriptor shape rather than
benchmark identity.  Exact typed descriptor programs and capability labels
such as `minmax-binary`, `signbit`, `cast-round`, and `select-u16` are valid
target vocabulary, not captured workload identity.  Full instruction words
predominantly live in architectural encoding/model homes, comments,
enumerators, and tests.  Small pass-local exceptions represent architectural
sentinel or identity words such as NOP; transformation decisions otherwise
consume decoded fields, typed events, or explicit capability data.  Symbol
and spelling checks cover ABI/runtime facts
(`main`, `__instrn_buffer`, `__init_array_start`, init/preinit/constructor
sections), builtin/op prefixes, and recognized inline-assembly forms.  None
names a corpus row.

The hardening stack closes the concrete audit findings in cross-call
placement, replay/MOP preheader discovery, and outward MOP ownership:

- externally visible and ABI-rooted definitions now refuse unsafe cross-call
  placement;
- replay and MOP unique-predecessor proofs use cycle-safe walks rather than
  an arbitrary depth-four cutoff;
- externally callable nodes in the MOP owner chain refuse formation;
- only a real file-scope `main` receives the crt0 ownership exemption.

The current system is sensible and fail-closed, but it is not yet a single
global optimizer.  Its principal remaining risk is interaction among many
locally profitable sequential transforms, each with partially duplicated
resource and cost reasoning.

## Re-audit method and structural-generality result

The complete 46-translation-unit set was re-audited at the pinned tip, not
sampled.  The mechanical review joined registrations to factories and searched
all implementation TUs for source/file identity APIs, declaration and
assembler-name comparisons, string comparisons, benchmark/corpus vocabulary,
long literal words, exact floating-point bit patterns, and source-like function
names.  Each hit was then read in its decision context.

The result remains clean: no pass decision is selected by a corpus row,
benchmark or kernel function name, source filename, coefficient fingerprint,
or captured whole-opcode sequence.  The apparently distinctive production
hits have structural reasons:

- `main`, `__instrn_buffer`, init-array symbols and constructor/init section
  names are entry, ABI, or runtime ownership facts;
- `fence`, `ebreak`, `.ttinsn`, load/store mnemonics, builtin prefixes, and
  `ttmop` names are recognized assembly or target-RTL grammar;
- full-width masks, sign boundaries, NOP, MMIO addresses, fixed constant-
  register values, and immediate-representability bounds are ISA facts;
- names such as `signbit`, `minmax-binary`, `cast-round`, and `select-u16` are
  typed capabilities or dump labels; and
- workload names and evidence-lane identifiers occur in comments that record
  counterexamples or calibration provenance, not in executable selectors.

Several matchers necessarily recognize an opcode *class* or a typed canonical
idiom.  That is not an opcode fingerprint: decisions consume decoded fields,
SSA/RTL dataflow, effects, ranges, ownership, or declared capability records,
and the test suite contains renamed, varied-immediate, swapped-role and
near-miss refusals.  The only pass-local complete Tensix word used as a value is
the architectural NOP sentinel; other complete words found by the scan are
comments or ordinary masks/typed architectural constants.

## Exact compiler candidate provenance

The installed candidate record
`toolchain-candidate-final-075e9f2/CANDIDATE-PROVENANCE.md` binds source and
behavior to `075e9f2f4b22dd08342be730d42e34060da10d4a`, built from
`/home/ttuser/sfpi-uplift/gcc-build-planner` and installed at
`2026-08-25T05:29:37Z`.  Its recorded hashes, independently re-read from the
installed files, are:

| Artifact | SHA-256 |
|---|---|
| `cc1plus` | `45ba7169920924fd6ebeb6eeb3766156b413dbf895e091b53603bed1e35e7d79` |
| `cc1` | `f27181b8f726c2055a98f88d90125bc3b450587dbcd8452b08b4dd97bee3f4ba` |
| `lto1` | `b37deac999366f2170b5eb1532886b142c6dd16f0b855337cfd0aad57c3ac378` |
| `riscv-tt-elf-g++` | `a04de6aad4c29aa222e7b5f2e9d699b8bb89fec6accfd38dcf4a78e72e47e720` |
| `riscv-tt-elf-gcc` | `cfb97ae9bdb30226e8fa7dec36dc458732b2f6afc80a1bba196352b08cd0fbd5` |

The full `rvtt.exp` compiler gate records 4,925 PASS, 16 frozen FAIL, and 2
XFAIL.  The sorted failure-set SHA-256 is
`41346b4760b0faebd9b0b040a882f2d87ec46065c334c853ff2661daaac07182`;
the `g++.sum` and `g++.log` hashes are respectively
`0e5255dda256bf8e154dbf92dd8c12be320e9444d5713061106d6a5cebf4d5ab`
and `a4e2266ed3f49765a26372dc0659070fb660a519cb4f3648137e67d1ec7fff99`.
An exact combined-flag acceptance smoke also passed const residency, Dst
auto-increment, replay hoist, replay record-hoist, and the replay completion
guard.  These are compiler gates; they do not by themselves claim silicon
performance for this candidate.

## Pass census

The following table accounts for every target-specific registration.  A
"canonical idiom" is acceptable here only because the matcher is expressed
in typed operations/SSA or RTL dataflow, has a semantic proof, and refuses
near misses.  It does not mean a named application pattern.

| Pipeline area | Registered passes | Audit disposition |
|---|---|---|
| Front-end validation | `check_early`, `check_late` | Typed-language and architecture guards; mechanical, not corpus-specific. |
| Structured semantic folds | `ccmask`, `int_abs`, `int_not`, `store_fold`, `reprprop` | Canonical idioms backed by bit/exhaustive proofs and dataflow; near misses fail closed. |
| Constant and synthetic-value shaping | `invariant`, `immvar_expand`, `synth_split`, `immload_shorten`, `noval_elide`, `synth_cse`, `rvtt_dce`, `synth_renumber`, `immload_combine`, `prgm_const` | Operates on typed definitions, uses and representability; architectural constants are explicit capability data. |
| Loop/Dst shaping | `dst_iteration`, `dst_interleave`, `lp_schedule`, `delivery_shape`, `replay_unroll`, `round_interleave` | Structural loop/dataflow proofs.  Cost terms are calibrated, but currently local and partly duplicated. |
| LUT and placement | `lut_select`, `crosscall`, `crossloop` | Typed range/CFG/ownership proofs.  LUT coefficients are arbitrary SSA program values; architectural tables describe bucket boundaries, modes, and certified lowering capabilities.  Entry and predecessor walks were hardened in this pin. |
| Structured lowering and CC | `expand`, `live`, `cc`, `reassoc`, `combine`, `unspec_prop_ssa`, `attrib` | Target lowering/dataflow.  Reassociation is license-gated and default-off; semantic flags are part of legality. |
| Cross-lane and transpose | `crosslane`, `transp_involution` | Typed permutation algebra and all-lanes/dataflow proof; architecture capability tables are the remaining independent-reference obligation. |
| Pre-IRA RTL | `unspec_prop_rtl`, `fix_ebreak`, `rmext`, `dst_ownership`, `lp_schedule_prera`, `lreg_livein`, `lp_alloc` | Mechanical propagation plus resource-aware scheduling/allocation.  `sfpencc` operand-role tests include swapped-role negatives and a varied CC-enable case; generated differential testing should broaden this high-risk seam. |
| Post-RA formation and scheduling | `spill_diag`, `synth_opcode`, `macro_planner`, `lreg_rename`, `schedule`, `replay`, `dst_autoincr`, `mop_form` | Effect-, hazard-, dependence-, and ownership-driven; transaction/rollback or fail-closed paths are present.  Replay/MOP CFG and entry ownership were hardened in this pin. |
| Final stream lowering and enforcement | `hll`, `fix_raw`, `crosslane_window` | Late lowering/fixup plus final architectural-window enforcement; operates on final stream semantics. |

The generic `pass_dce` entry is intentionally excluded from the 53-pass
target count.

## Strengths observed in this source audit

- Architectural effects and delivery hazards are explicit and refusals are
  named instead of silently guessing.
- Macro formation derives schedules and all established descriptor fields
  from the stream.  Narrowly scoped provenance-pinned whole-word programs
  remain for typed event signatures whose field semantics are not yet fully
  established.  Formation commits only after the applicable proof succeeds.
- Several mutating passes are transactional or reconstruct output only after
  legality, preserving byte identity on refusal.
- Generic-renaming, varied-immediate, adversarial-entry, deep-CFG, and
  near-miss tests make captured-kernel matching substantially harder to hide.
- The corpus measurement contract binds compiler, cc1plus, harness, simulator,
  binary hashes, semantic/hand source, OFF/ON flags, and device evidence.

## Profitability and completion-contract audit at this tip

The profitability hardening from `eee92f639c1` through `075e9f2f4b2` is
structural and internally consistent.  The four relevant controls remain
individually default-off in `gcc/config/riscv/riscv.opt`:
`-mtt-tensix-optimize-dst-autoincr`,
`-mtt-tensix-optimize-const-residency`,
`-mtt-tensix-optimize-replay-record-hoist`, and
`-mtt-tensix-replay-hoist-completion-guard`.  Bare target defaults therefore
cannot acquire one of these experimental transformations by accident.

### Dst auto-increment

Every emitted scratch-slot program now pays the same configuration resource
price, independent of placement or callgraph shape:

```text
config_cost = 3 SETC16 words * programmed_slots * 2 issue units + 2
```

That is eight removed-row issue units for the current one-slot Blackhole and
Wormhole programs.  The charge applies to straight roots, externally visible
or address-taken functions, direct callees, shared placements, and loop
preheaders.  A shared emission is charged once and aggregates all rows it
serves; it is not charged once per consumer.  Only a proven loop trip count
multiplies dynamic rows for preheader amortization, and unknown trips refuse.
This closes the former unit/accounting asymmetry without introducing a
function-name or body-shape exception.  Entry/callgraph guards remain
conservative where an externally reachable entry could bypass the proposed
state ownership.  Focused fixtures cover straight roots, externally callable
callees, shared and face-shared placements, the eight-unit configuration
boundary, loop amortization, and renamed/varied Blackhole and Wormhole shapes.

### Constant residency

Let `W` be the original materialization width in delivered words and `R` the
per-iteration standalone read-back width after residency.  Ordinary in-loop
residency now first requires `W - R > 0`, then requires a strictly positive
net issue saving over the `W + 1`-word programming cost.  The exact minimum
proven trip count is:

```text
floor((W + 1) / (W - R)) + 1
```

Thus `W=2,R=0` first fires at two trips; `W=2,R=1` first fires at four;
three trips is equality and refuses.  A shortened `W=1,R=1` load has zero
recurring saving and refuses as `no-net-loop-issue-saving`; an unknown trip
count refuses as `loop-profitability-unproven`.

For the CC-canonical first-iteration peel, the model prices the changed
delivery class as well as every read-back word:

```text
save = SLOT * sum(W - R) per remaining iteration
cost = PUSH * (sum(W) + n_SFPCONFIG)
     + (PUSH - SLOT) * body_words
fire when (trips - 1) * save >= cost
```

`body_words` is deliberately an overestimating delivered-word proxy; an
overestimate can only increase the required trip proof.  The peel therefore
does not borrow the ordinary formula or silently price an absorbable read when
the consumer actually needs `SFPMOV`.  Dedicated fixtures pin the single-word
zero-saving refusal, both sides of the strict-positive ordinary boundary, and
peel read-back pricing.

### Replay record-hoist completion guard

The record-hoist flag's ordinary measurement contract cancels identical
payload execution and prices delivery-side savings.  Its audited runtime-trip
exception is valid only for that monotone delivery model.  The opt-in
completion guard instead retains the audited interlocked-execution term and
uses the shared binding-resource model, charges the complete preheader record
exactly once, keeps the replay reissue-latency audit, and refuses an
effect-opaque payload.  A runtime/unknown trip count cannot borrow the
delivery-only two-trip proof and now refuses by the truthful reason
`record-hoist-completion-runtime-trips-unproven`; the printed zero-trip benefit
is diagnostic only.

The guard is provably a monotone restriction over the *legal* record-hoist
candidate domain.  With `W` delivered payload words and `T` proven trips, the
ordinary and guarded execution-bound per-trip benefits are respectively
`123W - 70` and `230`.  Therefore:

```text
guarded benefit - ordinary benefit
  = T * (230 - (123W - 70))
  = T * (300 - 123W)
  <= -192T, because replay MIN_SEQUENCE requires W >= 4
```

The exact `W=4,T=4` Blackhole/Wormhole boundary fixture pins the result:
ordinary benefit `773` fires against the 60-centislot minimum, guarded benefit
`5` refuses, and the difference is `-768`.  A two-word arithmetic reversal is
outside the replay former's legal domain.  This proof is derived from shared
cost constants and `MIN_SEQUENCE`; it is not the semantic definition of the
guard and contains no target, kernel, opcode, coefficient, payload-length, or
trip-count special case.  Paired Blackhole/Wormhole fixtures cover ordinary
fire versus guarded refusal, delivery-bound routing, long-payload refusal, and
the truthful runtime-trip refusal.

## Remaining architectural debt

The following comparison is deliberately bounded to current official compiler
documentation/source and original papers.  It identifies reusable design
ideas; it does not claim that a general-purpose SIMD vectorizer directly solves
RVTT's replay, configuration-state, delivery, or tile-ownership problems.

| Primary source | What it establishes | Sensible RVTT comparison |
|---|---|---|
| LLVM VPlan | An explicit candidate can carry cost and code-generation intent; the high-level workflow keeps input IR unchanged until Execute.  Current LoopVectorize uses VPlan to drive code generation, although VPlans are still built after all cost-based and most legality decisions. | `RVTTPlan` should make the unchanged program a candidate and align cost with the exact stream it would emit.  This is a roadmap, not a claim that today's 53 sequential passes already have VPlan-like global choice. |
| LLVM SandboxIR | `save`, `accept`, and `revert` provide a transactional layer over LLVM IR. | RVTT already has local reconstruct/rollback disciplines, but lacks one transaction spanning competing cross-pass candidates. |
| LLVM RISC-V vector support, target cost source, and MachineScheduler | Scalable-vector representation, queryable target costs, dependency DAGs, and scheduling-resource models are separated target mechanisms. | RVTT's fixed tile/lane machine is different, but its duplicated issue, execution, drain, replay, LREG, Dst, CC and config facts should become one queryable resource model used by every plan and scheduler. |
| GCC tree-SSA vectorizer | GCC integrates legality, dependence and target cost reasoning for loop and basic-block vectorization. | RVTT starts from an already vector-semantic SFPI program and optimizes accelerator state/delivery; reuse GCC analyses where applicable, but do not equate ordinary SLP success with replay/config legality. |
| MLIR Vector dialect | Structured n-D vector values preserve transformation intent and lower progressively toward hardware operations. | A small SFPI tile/face/lane plan IR would avoid reconstructing high-level transfer, transpose, predication and delivery intent for the first time in late RTL. |
| goSLP | Whole-function ILP searches statement packs, with dynamic programming for permutation selection and an explicit compilation-time tradeoff. | It supports bounded global pack/shape selection as a direction; it does not validate RVTT cost constants or state ownership. |
| Whole-Function Vectorization | SSA CFG analysis distinguishes uniform and varying values/control across a function. | It supports a shared uniformity analysis boundary.  External-entry and accelerator-state ownership remain separate RVTT obligations. |

### P0: one transactional multi-candidate plan

Introduce an `RVTTPlan` that contains legality obligations, target-resource
schedule, predicted kernel delta, and a stable refusal trail.  Candidate
generation must include the unchanged program.  Build, cost, optimize, and
prune candidates without mutating the input; commit the single winner once.

This addresses the leading architectural hypothesis for composition losses:
individually sensible passes can compose into a worse result, while a later
pass cannot cheaply undo an earlier locally profitable choice.  LLVM's VPlan
high-level design provides a useful aspirational shape: Legal, then
Plan/cost/prune, then Execute, with only Execute mutating IR.  Current
LoopVectorize constructs VPlans after all cost-based and most legality
decisions, so LLVM SandboxIR's explicit save/accept/revert layer is the closer
current precedent for transactional implementation.

### P0: one queryable target/resource model

Unify issue slots, execution occupancy, drains, LREG and Dst banks, CC/config
state, replay residency, architectural capabilities, and delivery costs.
Every pass and every `RVTTPlan` candidate must query that model.  Existing
`rvtt-effects`, macro descriptor/calendar tables, list schedulers, B&B search,
and allocation machinery are strong seeds; duplicated effect/cost logic is
the problem to remove.

### P1: shared whole-CFG ownership and uniformity

Compute once which values, predicates, regions, calls, and exits are uniform,
divergent, externally enterable, or owned.  LUT selection, cross-call and
cross-loop placement, replay, MOP formation, and CC/config placement should
consume that shared analysis instead of recognizing related CFG facts
independently.  Whole-function vectorization research demonstrates
multiple-instance SIMD conversion of uniform/divergent SSA control flow.  It
is inspiration for the representation and analysis boundary, not a direct
solution to RVTT entry ownership.

### P1: global packing, scheduling, allocation, and delivery selection

Move beyond region-local choices.  A bounded global solver could jointly
select packs, permutations, macro/replay shapes, schedule, LREG/bank
assignment, and delivery tier.  goSLP establishes whole-function ILP packing
and dynamic-programming permutation selection; extending that idea across
RVTT's GIMPLE/RTL choices is a proposed design, not something goSLP itself
demonstrates.  RVTT already contains bounded exact-search attempts, an
optional MILP cross-check, and deterministic list schedulers.

### P1: a structured SFPI vector/tile plan IR

Represent tile/face/lane values, predication, transfer, LUT, reductions,
transpose, RWC/config tokens, and delivery regions above RTL.  Lower it
progressively to target forms, following MLIR Vector's virtual n-D vector to
hardware-vector approach.  RTL remains the final verifier and scheduler, not
the first place where global intent becomes reconstructible.

### P2: make decision-surface coverage mechanical

Generate a machine-readable census first, then a matrix of pass x shape x
architecture x semantic mode x fire/refusal/near-miss.  Stable plan dumps
should include obligations, cost components, chosen/no-op candidate, and
refusal names.  Add a verifier that rejects a transform lacking a tested
positive and a tested refusal for each materially different legality edge.
The present tree does not contain a reproducible checked-in census that maps
every named refusal to a direct oracle; producing that census is the next
concrete coverage task, not a reason to mislabel operation-row coverage as
100%.

## Coverage and parity definitions

Three targets must be reported independently:

1. **Registry accountability:** at tt-metal
   `ea54ec3a331e6d697f560437231d0761b52c185b`, whose registry was last changed
   by `1611185df0b31ca701a85770d7d0b81ef474d145`, every row in the 284-row
   `sweep_2x2_ops.tsv` registry is either a full 2x2, semantic-only comparison,
   or an explicit machine-readable skip.  The booked campaign accounts for
   all 284 rows: 164 full 2x2, 99 semantic-only, and 21 explicit skips.  This is
   **100% registry accountability**, including the declared skips; it is not a
   claim that every row has a handwritten arm or that generated code matches
   hand performance.
2. **Runnable correctness coverage:** every applicable runnable cell has
   pinned OFF/ON binaries and a CRAQ verdict on its applicable architecture;
   semantic/hand pairing applies to the 164 full2x2 rows, while the 99
   semantic-only rows have no hand arm.  Hardware-required cells additionally
   require exact silicon provenance.  This can reach 100% for the declared
   registry contract.
3. **Hand-code performance parity:** generated code is within the declared
   +/-0.5% kernel band of the handwritten reference.  This is an outcome, not
   coverage.  It cannot honestly be promised for all kernels while algorithmic
   or ISA ceilings remain (for example LUT accuracy floors, fixed entry
   surfaces, recurrence initiation intervals, and bivariate algorithms).

The booked `rooting-hardening-whfix-final-20260824` campaign makes the
distinction concrete.  Across its 263 runnable rows, semantic ON versus OFF is
193 WIN, 53 PARITY, 3 LOSS, and 14 unmeasured byte-identical non-engagements.
Among the 164 rows with a handwritten arm, the hand comparison is 65 WIN, 27
PARITY, 71 LOSS, and one unmeasured row.  The measured hand at-or-better rate is
therefore:

```text
(65 WIN + 27 PARITY) / 163 measured = 92 / 163 = 56.44%
```

That campaign binds tt-metal
`ea54ec3a331e6d697f560437231d0761b52c185b` and `cc1plus`
`649ac9476522afb9ca6bd29af161a3afca577c6dc339d5c51c2f73f2d80d09e6`.
It is the prior booked silicon baseline, not a silicon result for the newer
`075e9f2` candidate whose compiler hashes are recorded above.  A final-candidate
silicon report must bind the `075e9f2` candidate hashes, harness and tt-metal
commit, exact OFF/ON flag vectors, simulators, device identity, binaries, and
raw measurements before replacing or extending these figures.

A selector that compiles to identical OFF/ON bytes is a measured
non-engagement, not automatically a particular refusal.  Ordinary corpus
builds currently do not preserve every compiler dump/refusal trail, so exact
root-cause attribution requires a dedicated diagnostic rebuild.

## External references reviewed

- LLVM, ["Vectorization Plan"](https://llvm.org/docs/VectorizationPlan.html)
- LLVM, ["Sandbox IR"](https://llvm.org/docs/SandboxIR.html)
- LLVM, ["RISC-V Vector Extension"](https://llvm.org/docs/RISCV/RISCVVectorExtension.html)
- LLVM, [RISC-V target cost-model source](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/RISCV/RISCVTargetTransformInfo.cpp)
- LLVM, [MachineScheduler source documentation](https://www.llvm.org/docs/doxygen/MachineScheduler_8cpp.html)
- MLIR, [Vector dialect](https://mlir.llvm.org/docs/Dialects/Vector/)
- GCC, [tree-SSA vectorization](https://gcc.gnu.org/projects/tree-ssa/vectorization.html)
- Mendis and Amarasinghe, ["goSLP"](https://arxiv.org/abs/1804.08733)
- Karrenberg and Hack, ["Whole-Function Vectorization"](https://compilers.cs.uni-saarland.de/papers/karrenberg_wfv.pdf)
