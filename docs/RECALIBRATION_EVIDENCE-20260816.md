# Replay-hoist profitability recalibration — evidence record (2026-08-16)

Compact, hash-anchored summary of the silicon evidence behind commit
bb56f1d7797 ("riscv: recalibrate the replay-hoist profitability model
from silicon").  The raw evidence tree (compile legs, CRAQ legs, silicon
CSVs, logs, per-job ELF .text hashes) lives on tt-quietbox-0 at
`~/sfpi-uplift/gatefix-evidence-20260816/` and is anchored here by the
SHA-256 of its `SHA256SUMS` manifest; this file is the in-repo record so
the calibration is auditable without that machine.  (PULL_ANALYSIS
2026-08-17 §4 item 10 / HANDOFF §6b pending (c).)

## Device-class note (disclosed, not incidental)

The recalibration A/B ran on a **Blackhole p150-class** device.  The
archived D1 baseline pair (834/840) was recorded on the retired
p100a-class box; the p150 promotion run reproduced that pair's absolutes
exactly (see below), but no cross-device control exists, and the device
class change p100a -> p150 was not separately flagged in the commit.
tt-metal's baseline data is chip-class-separated (p100a file immutable);
p150 is the authoritative class going forward.

## The four silicon calibration points

Same-source A/Bs per HANDOFF §1 (identical source/input/golden, only
compiler flags differ, both flocks, 3 fresh processes per selector):

| shape (trips, len)            | old benefit | new benefit | silicon |
|-------------------------------|------------:|------------:|---------|
| SDPA-exp counted loop (8, 24) |         148 |       +2325 | WIN  -9.83% |
| Reduce-SDPA (4, 8) x2         |          16 |        +121 | WIN  855.5 -> 834 (832.75 at step 1) cyc/body |
| Log repeated-seq (4, 17)      |          34 |        -158 | LOSS +1.81% |
| Log1p repeated-seq (4, 31)    |          62 |        -592 | LOSS +2.30% |

The old model ordered the Reduce-SDPA WINNER (16) strictly below both
LOSERS (34, 62): no threshold could separate them.  The new model
(`benefit = trips*(deliver - max(123, execute)) - deliver`, centislots,
`deliver = (1+len)*123`, `execute = 100*len`, MIN_BENEFIT = 60) is
sign-correct on all four points using only the pre-existing 1.23x
delivery ratio; 76 unseen (trips,len) probes later confirmed the
decision surface is a plane, not a curve-fit notch (independent
adversarial review, PULL_ANALYSIS §2a).

## A/B numbers

- **Step 1 (target confirmation, stock compiler, documented override
  `-mtt-tensix-replay-hoist-min-benefit=0`)**: REDUCE_SDPA_BODY
  mean(PACK_ISOLATE), 3 fresh procs/selector alternating:
  gen 832.75 / 832.75 / 832.75 vs hand 839.00 / 839.00 / 839.00
  = -0.745% (gen perf pack `.text` 9ca4a98c..., hand db3295d2...).
- **Promotion (post-review, recalibrated compiler, DEFAULT gate, no
  override)**: gen **834.00 / 834.00 / 834.00** (pack 9ca4a98c...) vs
  hand **840.00 / 840.00 / 840.00** (pack db3295d2...) = **-0.714% WIN
  at the default gate**, reproducing the archived p100a D1 record pair
  exactly (paired delta -6 cycles in both runs).
- Log/Log1p control: hoist-ON-default and full-ON builds byte-identical
  patched vs same-build baseline (dumps -158/-592 < 60).
- SnakeBeta (4,32) "gate reversal" closed: old model fired at exactly 64
  on the corr kernel (math f5a2a344), new model refuses (-623, math
  f603b4aa), paired CRAQ PASS/PASS, device correctness PASS for both;
  every measurable perf ELF byte-identical old vs new (the old fire was
  followed by a downstream fixed-encoding refusal), so no perf pair
  existed to measure.

## Compiler provenance

- Toolchain driver `riscv-tt-elf-g++` sha256:
  `4633999c170e1a8932798c0c4acc321ff1720baa4f7fb1510c28da23761cce68`
  (unchanged across the cd0af49be5c -> bb56f1d7797 toolchain rebuilds;
  the backend lives in cc1plus, so the driver hash alone does NOT
  distinguish compiler eras — the root of the PULL_ANALYSIS D6
  pinned-sha skew).
- Recalibrated (bb56f1d7797-pin) toolchain `cc1plus` sha256 (the
  promotion-run compiler, PRIMARY pin for sweep keying):
  `33221397ebb22eefeecb91e9c579bdfe6336c86a97287b0205facdbcb68f5ce1`
- Lane-F development-build cc1plus recorded inside the evidence tree
  (same source, separate build dir; since superseded on-disk):
  `97e94261abd2f2d1928a66e81dee2b926e88e49348c060696e55f1489c0b77b6`

## Evidence-tree anchor (tt-quietbox-0)

- `~/sfpi-uplift/gatefix-evidence-20260816/SHA256SUMS` (494 entries,
  covering compile/, craq/, silicon/reducesdpa-{step1,promo},
  silicon/snakebeta-promo, tools/, logs) — sha256 of the manifest
  itself:
  `6cec1a4e9d332f31aaa25b4018c8f14e78d10c1b9b426d7c837e8c717d30617c`
- Review record: the recalibration landed only after an independent
  adversarial review reproduced the model surface and the byte-identity
  gates from a from-scratch build (PULL_ANALYSIS-20260817 §2a: CONFIRMED
  "calibration, not fingerprinting"); the promotion silicon ran
  post-review with orchestrator approval.

Raw CSVs are deliberately not committed (HANDOFF work item: summarize +
hash-anchor); the manifest hash above is the integrity root for anyone
with access to the original tree.
