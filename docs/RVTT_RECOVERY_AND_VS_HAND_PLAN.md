# RVTT recovery and versus-hand landing plan

Date: 2026-08-25

## Decision

The overnight `075e9f2` / ON-26 stack is rejected for promotion.  Its useful
measurement and harness evidence remains archived, but it is not a canonical
pin and its 193 semantic-ON versus semantic-OFF wins are not versus-hand wins.

The live harness is restored to the ceremonied pin-28 installation:

- cc1plus: `2a71feada1d944b4c5b8c114495a8e084c722dd1dbb4cea3ac617f0ae25d69af`
- driver: `5811b9eb1ad2db68bb825c5059ef93e3c4ec6d9b55f3311257662be0edf437be`
- reviewed ON set: 28 flags
- active R9 witnesses: 12
- R10 quarantine: empty

The source reconciliation forward-reverts the unratified entry-root axiom,
call-boundary and uniform Dst-auto-increment repricing, and the associated
constant-residency repricing.  It retains the separable cycle-safe unique-
predecessor proof and adds positive and near-miss coverage for that proof.
The harness reconciliation retains strict census, fail-closed evidence gates,
and band-consistent report adjudication while restoring the pin-28 ON set.

## Defensible performance result

The canonical pin-28 board contains 69 booked WIN, 33 PARITY, and 55 LOSS
versus-hand rows.  Two of the wins are default-off knob bookings
(`mulint32-fresh` stride and `unarybitwise-fresh` replay-loop-unroll), so the
defensible default ON-28 result is **67 wins versus hand**.

Restoring ON-28 also restores measured compiler mechanisms that the rejected
stack disabled or made unprofitable.  Examples include:

| Row | ON-28 versus hand | Relevant measured mechanism |
|---|---:|---|
| `minmax` | about -5.03% | drain-schedule drop-one: -6.19% |
| `typecast` | -2.55% | drain-schedule drop-one: -2.67% |
| `where` | -2.73% | drain-schedule drop-one: -2.79% |
| `sdpa` | -8.03% | crossloop drop-one: -1.49% |
| `exp` | -0.86% | record-hoist composition |

The authoritative board is
`/home/ttuser/sfpi-uplift/laneFM-evidence-20260822/FINAL-BOARD.tsv`; drop-one
attribution is in
`/home/ttuser/sfpi-uplift/laneGI-evidence-20260824/EM-DROPONE-RESULTS.tsv`.

## First new default win: window-pairing stride

Stride is the highest-confidence next promotion, but it remains an `on-plus`
knob until the promotion protocol is complete.  On `mulint32-fresh` it has
already measured:

| Metric | ON-28 | ON-28 + stride | Delta |
|---|---:|---:|---:|
| KERNEL cycles | 38,669 | 35,077.7 | -9.29% |
| versus hand | +5.11% LOSS | -4.65% WIN | LOSS to WIN |

The existing lane-GJ evidence records one changed corpus translation unit,
paired CRAQ and device correctness passes, and three-repetition silicon.  Its
corpus and silicon legs used the pre-install hybrid cc1plus `2e32e5c08211`, so
they are strong prior evidence but cannot be relabelled as the canonical
ceremony.  Separately, canonical pin 28 has a complete 5,905-PASS DejaGnu
record with the reviewed frozen failure set.  These facts make the
optimization a strong candidate; they do not substitute for an ON-set
promotion ceremony.

Required gate, in order:

1. Add the stride-specific R9 union fire witness, already verified directly
   on canonical pin 28:

   ```text
   -mtt-tensix-optimize-window-pairing-stride|perf_eltwise_binary_sfpu.py::test_perf_fresh_cpp_mul_int[formats:Int32->Int32-mathop:SfpuMulInt32-fresh_cpp_impl:2]|-fdump-rtl-rvtt_macro_planner|Macro-planner window-pairing: interrow-drain 2 -> 1 rows=[0-9]+ bound=window-pairing-lreg-overlap
   ```

2. Compile the strict full corpus at installed pin 28 under ON-28 and
   ON-28+stride; require exactly one changed TU and byte identity everywhere
   else.
3. Run paired Blackhole CRAQ and physical correctness for every changed row,
   adding Wormhole where the corpus mapping requires it.
4. Run three fresh KERNEL silicon processes per leg against the same hand
   anchor; require the booked LOSS-to-WIN transition and no regression.
5. Run the complete SFPI-populated DejaGnu universe and require universe
   equality plus the frozen failure set.
6. Obtain explicit owner acceptance and independent adjudication, then make
   the ON-28 to ON-29 change in a separate configuration commit.  This is an
   ON-set ceremony on the existing pin-28 compiler; it does not create or
   install a new compiler pin.

Primary evidence:

- `/home/ttuser/sfpi-uplift/laneGJ-evidence-20260824/RESULTS.md`
- `/home/ttuser/sfpi-uplift/laneGJ-evidence-20260824/AUTOPSY.md`
- `/home/ttuser/sfpi-uplift/dejagnu-pin28`

If ratified, stride raises the default versus-hand count from 67 to 68.

## Next candidates

Replay-loop-unroll is second.  Existing knob silicon moves
`unarybitwise-fresh` from +23.25% to -2.32% versus hand and moves
`unaryshift-fresh` and `castfp32tofp16a` to parity.  It still lacks a complete
global changed-TU census and all-mover CRAQ/silicon gate, so it is not ready
for default promotion.  Int-abs similarly moves `absint32` from +23.39% to
+0.23%, which is parity rather than a win.  The completion guard is research
only: its 48 changed rows divide into 11 WIN, 22 PARITY, and 15 LOSS, rejecting
unconditional promotion.

## Release gates for the reconciliation

The restored installed pin has passed configuration lint, fail-closed harness
self-tests, and all 12 R9 union witnesses.  The reconciled compiler source has
passed an incremental build, 929 restored-family checks, and 114 completion-
guard/downstream checks.

A fresh build configured with the canonical target assembler and linker paths
then ran the complete SFPI-populated universe.  It accounted for 1,162 test
files plus the ten intentionally excluded oracle inputs and reported **5,999
PASS / 16 FAIL / 2 XFAIL**.  The 16-line unexpected-failure set is byte-equal
to pin 28, with SHA-256
`4764064c2c6cfb54a3b68f85550c879ca8e58d15580c1dc5fa0ce915a4bb59f7`.
Evidence is archived in
`/home/ttuser/sfpi-uplift/gcc-reconcile-evidence-20260825/canonical-config`.

An earlier 5,993/22 run used a stale build directory configured without the
target assembler/linker paths.  That disabled ZAAMO feature detection and left
the build-tree assembler wrapper empty, mechanically causing the five ZAAMO
scan failures and one assemble-only failure.  The canonical-config rerun
removes all six; they were not source regressions.

No result in this document authorizes a symlink repoint, pin number, or ON-set
change by itself.
