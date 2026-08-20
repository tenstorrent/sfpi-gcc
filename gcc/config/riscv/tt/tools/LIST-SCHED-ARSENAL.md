# List-scheduler acceptance arsenal (lane DT, 2026-08-20)

Independent acceptance tests + a makespan oracle for the DAG list
scheduler (`-mtt-tensix-optimize-list-schedule`, lane DQ,
rtl-rvtt-schedule.cc).  Tests/tools only: this branch changes no pass
source.  Validated against DQ's commit 4ff35db16628 stacked on
nkapre/sfpi 429976b94ca0 (a local integration build; this branch itself
carries only the arsenal).  Every number below is measured on that
build and hand-verified where stated.

## The oracle

`tools/tensix-makespan-oracle.py` judges ANY schedule against the
modeled optimum, not against another pass:

    tensix-makespan-oracle.py --arch bh [--audit-table tools/dl-latency-audit-20260820.tsv] \
        --compare base.s cand.s

Inputs are .s files compiled with `-mtt-tensix-dump-effects`.  It
rebuilds the dependence DAG from the per-insn typed-effect annotations
(the audited rvtt-cost.md facts, post-RA), computes the critical-path
lower bound (with word-count + sink-drain floor), and counts the
as-emitted makespan (BH modeled scoreboard stalls; WH explicit-NOP
verification with violation reporting; SFPSWAP next-slot acceptance
stalls).  Unaudited latencies are never guessed: exact counts are
REFUSED BY NAME and only the lower bound stands.  The optional audit
table applies lane DL's not-yet-merged audited rows (store/incrwc/lut)
ONLY where the dump says unaudited; delete it after
agent/latency-audit-widening merges.

Hand validations (arithmetic in laneDT-evidence-20260820/EVIDENCE.md):
- pressure_fit_7: baseline 22 = 14 words + 7 serial chain stalls +
  1 block-end drain; optimum = 15; scheduler fires 22 -> 15, oracle
  gap-to-lower-bound = 0.
- pq_rational_d3: whole-function baseline 27 = 17 words + 10 chain
  stalls; optimum 17; scheduler fires (region 24 -> 14), oracle
  27 -> 17, gap 0.

## Kernels and expected verdicts

| test | expected verdict (measured) |
|---|---|
| sfpi/list-sched-arsenal-pq-d3-bh.C | FIRE nodes=14 makespan 24->14 peak=3; oracle 27->17 == lower bound |
| sfpi/list-sched-arsenal-pq-d5-bh.C | FIRE nodes=22 40->22; oracle 43->25 == bound |
| sfpi/list-sched-arsenal-pq-d8-bh.C | FIRE nodes=34 64->34; oracle 67->37 == bound |
| sfpi/list-sched-arsenal-pq-d5-wh.C | FIRE 40->22 target=wh; required SFPNOPs 18 -> 0; oracle 43->25 == bound |
| tensix/list-sched-arsenal-cc-bait-bh.C | NO MOTION: 2 CC-word barriers, 3 no-decrease refusals, no fire |
| tensix/list-sched-arsenal-dst-pair-bait-bh.C | NO MOTION: 4 Dst-word barriers (2 print dst-access, 2 scalar-or-defless), 2 refusals, no fire |
| tensix/list-sched-arsenal-replay-owner-bait-bh.C | NO MOTION: replay-owner barrier ends block eligibility; .ttinsn opaque word bounds silently; 3 refusals, no fire |
| tensix/list-sched-arsenal-lutfp32-bait-bh.C | NO MOTION: SFPLUTFP32 refuses by name (effect-opaque), no fire |
| tensix/list-sched-arsenal-pressure-bh.C | FIRE both fns 22->15 pressure-peak=8 (the architectural boundary); no pressure refusal |

The sfpi/ tests run only when DejaGnu has `SFPI` set (the sfpi suite is
skipped in lane gcc-build runs, which have no target libstdc++ — the
full toolchain gate runs them).

CRAQ-golden: the three P/Q kernels (as r = P(x)*Q(x) fresh-body-style
harness tests, same chains/coefficients) run bit-golden on the pinned
craq-sim 9f324140 (libttsim 32489dda4fd6, clone-built) with the
scheduler ON and OFF — evidence in
~/sfpi-uplift/laneDT-evidence-20260820/craq/.

## Findings for the scheduler lane (DQ)

1. BARRIER NAMING: defless Tensix words are classified before the
   effect checks, so the canonical CC words (SFPSETCC/SFPENCC) print
   "scalar-or-defless", never "cc-write"; SFPSTORE prints
   "scalar-or-defless", never "dst-access".  Sound but misleading, and
   DQ's own list-schedule-cc-barrier-bh.C scans for "cc-write" cannot
   pass as committed.  The arsenal scans accept either name.
2. DQ's list-schedule-cc-barrier-bh.C kernel is also rejected by the
   frontend before scheduling: `sfpsetcc_v outside of pushc/popc`
   (malformed program).  The committed twin set appears unvalidated.
3. SFPLUTFP32 refuses as "effect-opaque" (whole effect record
   unaudited, per lane DL's deferral), not "unaudited-latency" -- and
   measured, SFPSHFT2 does too, so on this base the "unaudited-latency"
   barrier name is DEAD (unreachable): every latency-unaudited class is
   also effect-unaudited today.  DQ's list-schedule-unaudited-bh.C scan
   for the name FAILs.  The name becomes reachable only when an effect
   audit lands without a latency entry.
4. Regions need >= 3 nodes: 2-node chains are silently dropped (no
   refusal line).  Acceptance scans must not count refusals for them.
5. MODEL BOUNDARY (benign today, watch on WH): the entry-producer scan
   looks one insn back only; a zero-length ghost (readlreg/writelreg
   marker) hides the real audited producer, so a cross-region
   WAW/RAW latency edge into the region's first node is unmodeled
   (pressure_reuse_8: the oracle counts 2 boundary stalls the region
   model does not see; correctness is preserved by the nop inserter and
   its pad-site guard, but the modeled decrease can over-claim).
