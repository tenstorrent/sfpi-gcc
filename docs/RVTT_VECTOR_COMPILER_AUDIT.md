# RVTT vector compiler audit and architecture roadmap

Date: 2026-08-24

This note records the source audit at compiler commit
`1740e2ac312e693294d25ceed6098e9009089c68`.  It deliberately separates
source structure, correctness coverage, and performance parity.  Those are
different claims and must not be collapsed into one percentage.

## Scope and result

`gcc/config/riscv/tt/rvtt-passes.def` registers 54 pass entries: 53 RVTT
passes and one invocation of generic GCC DCE.  The 53 target passes are
implemented across 46 pass implementation translation units containing
55,191 lines.

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

The hardening commit closes the concrete audit findings in cross-call
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

## Remaining architectural debt

### P0: one transactional multi-candidate plan

Introduce an `RVTTPlan` that contains legality obligations, target-resource
schedule, predicted kernel delta, and a stable refusal trail.  Candidate
generation must include the unchanged program.  Build, cost, optimize, and
prune candidates without mutating the input; commit the single winner once.

This addresses the leading architectural hypothesis for composition losses:
individually sensible
passes can compose into a worse result, while a later pass cannot cheaply
undo an earlier locally profitable choice.  LLVM's VPlan high-level design
provides a useful aspirational shape: Legal, then Plan/cost/prune, then
Execute, with only Execute mutating IR.  Current LoopVectorize constructs
VPlans after most legality and cost decisions, so LLVM SandboxIR's explicit
save/accept/revert layer is the closer current precedent for transactional
implementation.

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
   or an explicit machine-readable skip.  This can and should be 100%.
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

A selector that compiles to identical OFF/ON bytes is a measured
non-engagement, not automatically a particular refusal.  Ordinary corpus
builds currently do not preserve every compiler dump/refusal trail, so exact
root-cause attribution requires a dedicated diagnostic rebuild.

## External references reviewed

- LLVM, "Vectorization Plan": https://llvm.org/docs/VectorizationPlan.html
- LLVM, "Sandbox IR": https://llvm.org/docs/SandboxIR.html
- LLVM RISC-V Vector Extension: https://llvm.org/docs/RISCV/RISCVVectorExtension.html
- LLVM RISC-V target cost model: https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/RISCV/RISCVTargetTransformInfo.cpp
- LLVM MachineScheduler: https://www.llvm.org/docs/doxygen/MachineScheduler_8cpp.html
- MLIR Vector dialect: https://mlir.llvm.org/docs/Dialects/Vector/
- GCC tree-SSA vectorization: https://gcc.gnu.org/projects/tree-ssa/vectorization.html
- Mendis and Amarasinghe, "goSLP": https://arxiv.org/abs/1804.08733
- Karrenberg and Hack, "Whole-Function Vectorization": https://compilers.cs.uni-saarland.de/papers/karrenberg_wfv.pdf
- Shi et al., "Closer in the Gap": https://arxiv.org/abs/2605.10860
- Peccia et al., "Tensor Program Optimization for RVV Using Probabilistic Programs": https://arxiv.org/abs/2507.01457

The 2026 RVV hardware study is a useful warning against trusting a static
instruction count alone: it reports predication and strided-access costs that
current compiler models miss.  The probabilistic TVM work likewise supports
measured candidate search for tensor mappings.  Neither result substitutes
for RVTT's semantic and provenance gates; they support adding calibrated
candidate selection behind those gates.
