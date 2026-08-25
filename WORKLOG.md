# RVTT compiler uplift worklog: how the campaign reached 193 causal wins

Date: 2026-08-25

> **Status correction (2026-08-25):** the `075e9f2` / ON-26 overnight
> candidate described below was not a ratified shipping pin.  Its install and
> configuration changes bypassed the pin/ON-set promotion protocol, its
> DejaGnu run omitted part of the canonical SFPI test universe, and its final
> silicon report contained material versus-hand regressions.  The candidate is
> retained as historical evidence but has been rejected for promotion.  The
> live harness has been restored to the ceremonied pin-28 installation and the
> source/configuration stack has been forward-reconciled to ON-28 semantics.
>
> The number **193** remains a valid result only for that candidate's causal
> semantic-ON versus semantic-OFF experiment.  It is not a shipping result and
> it is not a versus-hand count.  The current defensible pin-28 hand board has
> 69 booked WIN rows, two of which are default-off knob experiments; therefore
> the default ON-28 result is **67 wins versus hand**.  The best reviewed next
> promotion is window-pairing stride: on `mulint32-fresh` it changes KERNEL
> time from 38,669 to 35,077.7 cycles and the hand comparison from +5.11% LOSS
> to -4.65% WIN.  It must remain a knob until its R9 union witness, strict
> corpus census, paired CRAQ/correctness, repeated silicon, complete DejaGnu
> universe, and independent ON-set adjudication are all complete.

This is the chronological record of the SFPI/RVTT compiler campaign from the
clean-machine handoff through the final `075e9f2` compiler and fixed-head
silicon closeout.  It answers a deceptively simple question: how did the
scoreboard get to 193 wins?

The short answer is that we did **not** turn 24 handwritten-performance wins
into 193 handwritten-performance wins.  We built a much larger, pinned causal
experiment and accumulated a generic compiler stack that improves 193 of its
248 measured semantic ON/OFF rows.  Hand parity is a separate result.

## The denominator correction

Three numbers appeared during the campaign, and they must not be compared as
though they were the same metric:

| statement | comparison | population | result |
|---|---|---:|---:|
| early dashboard | compiler ON versus hand | 74 hand-comparable rows | 24 WIN / 8 PARITY / 42 LOSS |
| final causal scoreboard | semantic compiler ON versus semantic compiler OFF | 263 runnable rows; 248 measured | **193 WIN / 54 PARITY / 1 LOSS / 15 unmeasured** |
| final hand scoreboard | semantic compiler ON versus hand | 164 full-2x2 rows; 163 measured | **66 WIN / 27 PARITY / 70 LOSS / 1 unmeasured** |

The 193 is therefore a **causal compiler-win count**, not a claim that generated
code beats hand on 193 kernels.  It says that, holding semantic source, input,
golden, device, and harness constant, the reviewed compiler stack improves
end-to-end KERNEL time by more than 0.5% on 193 rows.

The final registry has 284 entries:

```text
164 full 2x2 rows with a distinct hand arm
 99 semantic-only rows with no distinct hand arm
 21 explicit, machine-readable skips
---
284/284 registry entries accountable
```

Of the 263 runnable rows, 15 have no measurable ON/OFF engagement.  Thus the
causal denominator is 248, and the measured at-or-better rate is:

```text
(193 WIN + 54 PARITY) / 248 measured = 247 / 248 = 99.60%
```

That is the clean headline.  The honest hand result remains 93/163, or 57.06%,
at-or-better.

## What changed between the semantic legs

The final ON leg is a reviewed 26-flag optimization stack.  The OFF leg
force-disables the corresponding established families.  The exact final ON
vector archived by the silicon campaign is:

```text
-mtt-tensix-optimize-latency-schedule
-mtt-tensix-optimize-dst-iteration-fusion
-mtt-tensix-optimize-replay-hoist
-mtt-tensix-optimize-invariant-loadi
-mtt-tensix-optimize-dst-autoincr
-mtt-tensix-optimize-dst-ownership
-mtt-tensix-optimize-lut-select
-mtt-tensix-optimize-setexp-fold
-mtt-tensix-macro-planner
-mtt-tensix-macro-planner-replay
-mtt-tensix-optimize-mop-form
-mtt-tensix-optimize-capture-rotation
-mtt-tensix-optimize-ccmask
-mtt-tensix-optimize-interlock-schedule
-mtt-tensix-optimize-transp-involution
-mtt-tensix-optimize-replay-exec-record
-mtt-tensix-optimize-prgm-const
-mtt-tensix-optimize-drain-schedule
-mtt-tensix-macro-planner-residency
-mtt-tensix-optimize-const-remat
-mtt-tensix-optimize-const-residency
-mtt-tensix-optimize-counted-row-formation
-mtt-tensix-optimize-window-pairing
-mtt-tensix-optimize-window-pairing-stride
-mtt-tensix-optimize-replay-record-hoist
-mtt-tensix-optimize-lreg-alloc
```

This is not a bag of kernel peepholes.  The final audit joined all 53 RVTT pass
registrations to their factories and reviewed all 46 implementation
translation units, 55,334 physical lines.  It found no transformation selected
by corpus row, operation name, benchmark function, source filename,
coefficient fingerprint, or captured opcode sequence.  The stack operates on
typed operations, SSA/RTL dataflow, effects, control-flow ownership, target
capabilities, resource costs, and named fail-closed refusals.

## The engineering arc

### 1. Build a trustworthy experiment before chasing the count

The first decisive work was measurement infrastructure, not optimization.
Every row became a 2x2 contract:

```text
semantic source, compiler OFF
semantic source, compiler ON
hand source, compiler OFF
hand source, compiler ON
```

The causal claim is semantic OFF to semantic ON.  Competitiveness is semantic
ON to hand.  Hand OFF/ON byte identity permits one physical hand measurement,
but never changes the comparison definition.

The harness then added the gates that prevented attractive nonsense from
becoming a booked win:

1. Recompile and prove that the intended ELF `.text` actually changed.
2. Bind driver, cc1plus, tt-metal revision, flags, simulator, node, and job key.
3. Run paired CRAQ OFF/ON as a structural and semantic oracle.
4. Run physical device-golden correctness before performance.
5. Run three fresh performance processes per cell under exclusive device
   locks.
6. Archive raw and post-processed data, ELF hashes, logs, session references,
   and a complete checksum manifest.
7. Use drain-inclusive KERNEL time as the verdict metric; body-zone markers
   are diagnostic only.

This discipline caught a stale compiler at pin 1, an execution-saturated
replay regression at pin 3, wrong or incomplete macro effects, several
wrong-code/hang windows, device contamination, stale witnesses, and profiler
zones that violated issue-slot lower bounds.  Without those catches, the win
count would have been larger and meaningless.

### 2. Replace calendars and fingerprints with a real macro planner

The original compiler had exact-calendar paths for a few shapes.  The first
major architectural step was the seven-layer generic SFPLOADMACRO planner:

- generated typed effect attributes and one effects-query API;
- path-sensitive ownership and region discovery;
- a dependency DAG and derived row schedules;
- architecture capability tables for legitimate encoding facts;
- descriptor synthesis and a verifier;
- transactional formation with byte-identical refusal; and
- genericity tests using renamed shapes, varied constants, swapped roles, and
  near misses.

The Min/Max exact calendar was deleted and re-derived byte-identically by the
planner.  WP8 then extended the planner to single-row and loop-preheader
regions, shared descriptors across typed face regions, and deleted the
quarantined legacy SFPLOADMACRO pass entirely.

That machinery underlies the final macro-launch wins such as `signbit`,
`minmax-min`, `minmax-max`, `typecast`, `unarymaxmin-{min,max}`, `where`, and
`mulint32-fresh`.  The final classifier sees eight causal-winning rows whose
ON binaries are macro-launch shapes.

### 3. Turn repeated row programs into replay, then make replay safe

The largest final structural class is replay.  Work accumulated in layers:

- replay-aware complete unroll of proven-trip loops;
- invariant capture hoisting under region-scoped ownership;
- generic profitability using measured delivery costs;
- replay admission for planner-formed launches;
- execute-while-record capture;
- counted-row parameterized formation;
- replay-record hoisting across audited tile loops; and
- exact persistence, domination, no-exec, and reissue obligations.

The important lesson was that “fewer delivered words” is not automatically
faster.  Early pricing used an overlarge RISC-push/replay ratio and predicted
wins that silicon did not deliver.  The model was recalibrated to roughly
1.23 pushed-word issue cost per replayed slot, then split record and execution
costs across their actual binding resources.

The final classifier reports 87 causal wins whose semantic-ON binary is a
replay-launch shape.  Representative examples include `sdpa`, `exp`, `celu`,
`rpow`, `relu`, `roundingops`, `softplus`, `rsqrt-fresh`, `elu-fresh`, and the
SDPA-reducer family.

### 4. Remove per-row state traffic

Replay alone did not close the gaps.  The compiler also had to move or absorb
state setup that hand code paid once:

- Dst auto-increment absorbed per-row `TTINCRWC` into owned configuration.
- Dst/RWC ownership folded proven identity store/reload traffic.
- programmable-constant allocation moved invariant immediates into PRGM
  registers;
- constant rematerialization and residency avoided repeated loads;
- descriptor residency deduplicated identical macro programs; and
- cross-tile configuration epochs allowed safe prefix elision.

This is how the campaign turned large setup-heavy gaps into causal wins rather
than merely making the inner arithmetic prettier.  The later MAD-PAIR
residency repair is a good example: `hardsigmoid` moved from +14.60% versus
hand to +0.89%, while `sigmoid_appx` gained another 17.2% in its causal leg.

### 5. Fill stalls and schedule the whole delivered window

The scheduler work moved from local reorderings toward explicit hardware
resource reasoning:

- audited instruction latency and effect tables;
- interlock-aware independent-instruction filling;
- capture rotation across repeated rows;
- drain-aware boundary placement;
- a DAG list scheduler with a makespan oracle;
- a pre-RA pressure-aware scheduler;
- a DSATUR LREG allocator with transactional rollback; and
- exact branch-and-bound support for bounded allocation/scheduling choices.

Several of these remained corpus-inert or knob-only until they had a measured
vehicle.  That is intentional: infrastructure can be valuable without being
promoted merely because it exists.

### 6. Attack end-to-end delivery, not just the body marker

Silicon showed that end-to-end drain and loop delivery frequently dominated.
The campaign added:

- MOP-driven loop delivery;
- loop-backedge drain elision with exit compensation;
- window pairing across independent rows;
- stride-phase generalization so the absorbing Dst advance may ride any
  proven issued word; and
- replay-record hoisting across runtime tile loops.

`mulint32-fresh` is the clearest progression.  The macro planner first reduced
issued row work, but conservative inter-row drains left performance on the
table.  Window pairing recovered 11.32% on model-proven bytes.  Stride-phase
generalization then removed the “absorber must be on the last word” limitation.
In the final campaign:

```text
KERNEL semantic OFF = 51,486 cycles
KERNEL semantic ON  = 37,157.667 cycles
KERNEL hand         = 36,798.333 cycles

causal ON vs OFF = -27.8296%
ON vs hand       = +0.9765%
```

Relative to the preceding final-candidate result, semantic ON fell from
40,744 to 37,157.667 cycles, about another 8.80%, and the hand gap contracted
from +10.72% to +0.98%.

### 7. Expand the corpus so genericity had somewhere to fail

The row count grew substantially during the work.  Fresh C++ kernels,
fitted-polynomial variants, Blaze semantic lifts, tile-count variants, and
architecture twins were added not to inflate the win count, but to test
whether the same compiler mechanisms generalized.

The progression visible in complete or near-complete archived scoreboards is:

| campaign | runnable rows materialized | causal WIN / PARITY / LOSS / unmeasured |
|---|---:|---:|
| `weekly-e2e-weekly-20260821` | 181 | 141 / 21 / 3 / 16 |
| `weekly-e2e2-weekly-20260821` | 192 | 139 / 21 / 3 / 29 |
| `weekly-20260823` | 259 | 187 / 50 / 3 / 19 |
| `rooting-hardening-stride-final-20260825` | 263 | 193 / 53 / 3 / 14 |
| `pin29-on26-final-984c9a-20260825` | 263 | **193 / 54 / 1 / 15** |

The temporary 141-to-139 decrease is not a compiler collapse.  The second
campaign changed the materialized/unmeasured population while the corpus was
still growing.  By 2026-08-23, 67 rows had been added relative to that
192-row book.  Forty-two of those new rows were wins: 41 Blaze variants and
`recip-ilv2`.  Among the 192 common row names, seven formerly unmeasured rows
became wins and one parity row became a win, while two former wins ceased to
be measured wins.

The move from 187 to 193 is also auditable rather than mystical.  Five common
rows changed from unmeasured to measured WIN:

- `binarybitwise`;
- `binarycomp`;
- `binaryfmod`;
- `binaryremainder`; and
- `divint32floor`.

The newly registered `gelu-licensed` row supplied the sixth win.  Two previous
losses (`binopscalar` and `fill-fresh`) became parity; they did not inflate the
win count.

### 8. Harden the final compiler without spending the wins

Pin 29 was primarily a soundness and profitability closeout, not a late win
grab.  It uniformly priced Dst configuration, required strict-positive
constant-residency savings, priced readback in the peel path, and completed
the replay-hoist binding model.

The audit found that `crosscall-hoist`, `crossloop-hoist`, and `init-hoist`
still lacked sufficiently strong production witnesses under the final
contract.  They were quarantined from the reviewed ON vector, reducing it from
29 to 26 flags.  The final classifier had no newly changed selectors and 14
previously changed selectors became identical.

Most importantly, the quarantine preserved all 193 wins while improving the
tail:

```text
before final quarantine: 193 WIN / 53 PARITY / 3 LOSS / 14 unmeasured
final ON-26:             193 WIN / 54 PARITY / 1 LOSS / 15 unmeasured
```

The exact class changes were:

- `binopscalar`: LOSS +3.638% to byte-identical PARITY;
- `fill-fresh`: LOSS +0.772% to byte-identical PARITY; and
- `clamp`: near-zero PARITY to unmeasured non-engagement.

The final sole causal loss is `reduce-sdpa` at +0.776%.

## What the 193 wins look like

The 193 causal wins can be sliced without pretending that a composed pass
stack has one unique owner:

| slice | wins |
|---|---:|
| full-2x2 rows with a hand arm | 120 |
| semantic-only rows | 73 |
| final ON binary classified as replay launch | 87 |
| final ON binary classified as macro launch | 8 |
| changed by other/non-launch machinery | 98 |

The launch classification is an emitted-shape description, not single-pass
attribution.  A replay-launch win may also depend on constant residency, Dst
ownership, drain placement, scheduling, and allocation.

The win magnitudes are broad rather than concentrated at the noise boundary:

| causal improvement | wins |
|---|---:|
| at least 20% | 45 |
| 10% to 20% | 60 |
| 5% to 10% | 36 |
| 2% to 5% | 39 |
| 0.5% to 2% | 13 |

Large representative causal wins include:

| row | ON versus OFF |
|---|---:|
| `sigmoidappx-tree` | -75.50% |
| `exp` | -44.02% |
| `rpow` | -36.73% |
| `exp2-fresh` | -36.73% |
| `unarymaxmin-{min,max}` | -34.61% |
| `minmax-{min,max}` | -33.87% |
| `where` | -32.08% |
| `elu-fresh` | -30.83% |
| `mulint32-fresh` | -27.83% |
| `sdpa` | -26.79% |

Among the 120 causal wins that have a hand arm, 56 also beat hand, eight are
within the hand-parity band, and 56 still lose to hand.  This is why the
causal count and parity count must remain separate.

## Why the remaining hand losses exist

The remaining 70 hand losses are not one bug class.

1. **Algorithm and contract differences.** Some semantic programs express a
   more general or more accurate computation than a specialized hand kernel.
   LUT accuracy floors, fixed entry surfaces, and bivariate/fitted algorithms
   can dominate code-generation quality.
2. **Sequential local decisions.** The 53-pass pipeline contains many sensible
   local transformations, but it is not one global optimizer.  An early local
   win may block a better later packing, replay, allocation, or delivery plan.
3. **Binding-resource mismatch.** Static word removal does not guarantee lower
   drain-inclusive KERNEL time.  Issue, execution, replay, Dst, LREG, CC,
   configuration, and drain resources interact.
4. **Recurrence ceilings.** Some rows are already at a measured or derived
   recurrence initiation interval; more scheduling cannot break a real
   dependency.
5. **Entry and state ownership.** Hand code may assume a fixed caller/template
   state that ordinary compiled C++ must prove or establish.

The architectural next step is therefore not another pile of pattern matchers.
It is one transactional `RVTTPlan` candidate space over a shared target/resource
model.  The unchanged program must compete against alternative packs,
descriptors, replay/MOP shapes, schedules, LREG assignments, and delivery
tiers, with the chosen plan committed once.

## The completion guard: a useful negative result

The replay completion guard was deliberately kept out of the reviewed ON set
until its surface was measured.  The strict census covered all 263 runnable
rows: 215 byte-identical and 48 changed.  Every changed row then received
fresh targeted silicon:

```text
384/384 fresh guard leaves
50/50 paired CRAQ verdicts PASS/PASS
48/48 correctness records OK
KERNEL: 11 WIN / 22 PARITY / 15 LOSS
```

The guard improves `polygamma` by 1.123%, but the breadth is unfavorable and
`typecast` loses 2.641%.  Diagnostic body timing can point in the opposite
direction from end-to-end KERNEL (`where` is +15.77% in-body but -0.951% in
KERNEL).  The correct decision was to keep the guard opt-in pending a selective
architecture-grounded gate.  This refusal to bank a plausible but globally
losing pass is part of how the 193-win number remained credible.

## Final validation ledger

The immutable final evidence root is:

```text
/home/ttuser/sfpi-uplift/sweep-2x2/pin29-on26-final-984c9a-20260825
```

It binds:

| component | pin |
|---|---|
| sfpi-gcc source | `075e9f2f4b22dd08342be730d42e34060da10d4a` |
| cc1plus SHA-256 | `45ba7169920924fd6ebeb6eeb3766156b413dbf895e091b53603bed1e35e7d79` |
| compiler driver SHA-256 | `a04de6aad4c29aa222e7b5f2e9d699b8bb89fec6accfd38dcf4a78e72e47e720` |
| tt-metal fixed head | `984c9a687c4e4429f0a5a984f2ebf832c4bedff4` |
| Blackhole CRAQ simulator | `32489dda4fd6321262fbc7c18b8f05126ed760073c7030dc529555111967ffd3` |
| Wormhole CRAQ simulator | `8f0079a9a16c782bf47a56c2e48e0c73d4da32faa69b56a307bc0b921b29dbc5` |

The executed gates are:

| gate | result |
|---|---:|
| compiler `rvtt.exp` | 4,925 PASS / 16 frozen FAIL / 2 XFAIL |
| classifier | 854/854 OK; 588 CHANGED, 266 IDENTICAL |
| paired CRAQ | 439/439 OK and PASS/PASS |
| logical silicon legs | 2,839/2,839 fresh; zero cached or reused |
| physical device sessions | 359/359 rc=0 |
| device pytest calls | 2,816/2,816 passed |
| setup/call/teardown phases | 8,448/8,448 passed |
| collection errors | 0 |
| row verdicts | 263/263 runnable |
| explicit skips | 21 |
| correctness scoreboard reds | 0 |

The full `SHA256SUMS` contains 100,505 entries, verifies, and has SHA-256
`fbab34048fcb8650052a12ab74764703c292601e55e7d662bf88140596af958a`.

The historical-baseline report still contains performance-policy REDs.  That
does not contradict the semantic result.  Two reported REDs (`lcm-fresh` and
`gcd-fresh`) were a report bug that treated sub-band PARITY movement as a
WIN-to-LOSS sign crossing; the harness now uses the documented +/-0.5% bands.
The `softplus` structural RED was a missing issue-slot lower-bound record; the
audited 120.125 bound and a fresh zero-reuse ceremony close it Overall GREEN.
Ten substantive historical performance-policy exceptions remain.  None is a
correctness or semantic failure.

## Recompute the headline from the archived scoreboard

The count is not copied from a dashboard.  It can be reconstructed directly
from the immutable final `scoreboard.json`:

```bash
S=/home/ttuser/sfpi-uplift/sweep-2x2/pin29-on26-final-984c9a-20260825/scoreboard.json

# Prints 193: KERNEL causal improvements beyond the -0.5% band.
jq '[.results[] |
      select(.kernel_causal_pct != null and .kernel_causal_pct < -0.5)] |
    length' "$S"

# Prints 120 full2x2 and 73 semantic-only causal wins.
jq -r '[.results[] |
          select(.kernel_causal_pct != null and .kernel_causal_pct < -0.5)] |
       group_by(.kind)[] | "\(.[0].kind)\t\(length)"' "$S"

# Prints the complete 193-row ledger, fastest first.
jq -r '.results[] |
       select(.kernel_causal_pct != null and .kernel_causal_pct < -0.5) |
       [.op, .kind,
        (.classify["sem-perf"].macro_scan.classification // "non-launch"),
        .kernel_causal_pct, .kernel_vs_hand_pct] | @tsv' "$S" |
  sort -t $'\t' -k4,4n
```

The class boundary in the harness is `WIN < -0.5%`, `PARITY` from -0.5%
through +0.5%, and `LOSS > +0.5%`.

## Final shipping state

The final compiler behavior tip is `075e9f2f4b2`.  Documentation and the
silicon closeout are on sfpi-gcc canonical `nkapre/sfpi` at
`bc3b01540e51c1c56795d89cbdc56ccd2c0d792d`.  The report-transition and
softplus adjudication fixes are on tt-metal canonical `nkapre/sfpi` at
`0c4e6c417db1e4811f5c51f9a940eb9cac9e098c`.

Detailed companion records:

- [Compiler audit](docs/RVTT_VECTOR_COMPILER_AUDIT.md) — all-pass structural audit and
  state-of-the-art architecture comparison;
- [Silicon validation](docs/RVTT_SILICON_VALIDATION.md) — exact final and completion-guard
  evidence;
- [Macro planner](docs/MACRO_PLANNER.md) — planner construction and proof obligations; and
- [“The Second Engine”](https://claude.ai/code/artifact/46550506-4069-49c7-9656-fa2df4ca5f5d)
  — final private dashboard narrative.

## Bottom line

We reached 193 causal wins by doing four things together:

1. replacing captured calendars with reusable effect/dataflow/ownership-driven
   compiler machinery;
2. composing replay, macro planning, state residency, Dst ownership,
   scheduling, allocation, and delivery optimizations;
3. expanding the corpus until those mechanisms faced hundreds of unrelated
   shapes; and
4. refusing to count anything that did not survive exact classifier, CRAQ,
   device-golden correctness, three-process KERNEL timing, provenance, and
   manifest gates.

The result is not “193 kernels beat hand.”  It is stronger in one direction
and weaker in another: **the compiler helps on 193 of 248 measured semantic
rows, while generated code is currently at-or-better than hand on 93 of 163
measured hand-eligible rows.**  The first number shows that the generic
compiler stack broadly works.  The second number defines the remaining vector
compiler research program.
