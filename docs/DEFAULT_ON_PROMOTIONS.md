# Default-ON promotions (2026-08-17)

Three silicon-proven Tensix optimization flags are promoted from `Init(0)`
to `Init(1)` in `gcc/config/riscv/riscv.opt`.  Each remains fully
disableable; the revert spell for any of them is the `-mno-` spelling:

| flag | revert spell |
|---|---|
| `-mtt-tensix-optimize-dst-ownership` | `-mno-tt-tensix-optimize-dst-ownership` |
| `-mtt-tensix-optimize-lut-select` | `-mno-tt-tensix-optimize-lut-select` |
| `-mtt-tensix-optimize-setexp-fold` | `-mno-tt-tensix-optimize-setexp-fold` |

All three variables are single-read booleans in their passes
(`rtl-rvtt-dst-ownership.cc` transform gate, `gimple-rvtt-lut-select.cc`
pass gate, `gimple-rvtt-combine.cc` SETEXP_FOLD shape enable), so
`Init(1)` is exactly equivalent to the explicit positive spelling and
`-mno-` is exactly the old default.  QSR is unaffected: dst-ownership
hard-refuses on QSR (`dst-rwc-effect-unproved`/`qsr-unmodeled`) and
lut-select refuses (`lut-no-target-capability`); both refusals are gates
inside the passes, not flag defaults.

## Silicon evidence basis (Blackhole p150, cycles/tile mean(MATH_ISOLATE),
## deterministic x3, correctness-gated; research-pin nightly-20260817e, exit 0)

- **dst-ownership** (Track B pass_rvtt_dst_ownership B1-B4): addcmul
  flipped parity -> **-10.9% vs hand** (32.62 vs 36.62) — Track B's first
  silicon win, attributed by the weekly per-knob table as an interaction
  effect (single-knob legs byte-identical, full-ON-minus-one decisive).
  erfinv proven fold is bit-exact through CRAQ on BH+WH (BH static loads
  2->1); gelu correctly refuses on LREG pressure (OFF==ON).
- **lut-select** (pass_rvtt_lut_select + LUT increment 2): sigmoid-tree
  **+194% -> +8.9% vs hand** (30.35 vs 27.86; 17->4 slots/row via LUT
  formation + transactional coefficient preheader + SGN_RETAIN fold).
- **setexp-fold** (SETEXP exponent-copy combine): exp capture 17->16
  (hand's width) — exp improved +37.6% -> **+26.2%** and the R2
  prediction of ~91.6 was hit at **91.5** measured; also fires on the
  SDPA semantic kernel (16->15; sdpa causal improved -19.6% -> -22.1%).

Evidence dirs (tt-quietbox-0, each SHA256SUMS-manifested):
`~/sfpi-uplift/sweep-2x2/nightly-20260817` and `nightly-20260817e`
(wave-pin and research-pin nightlies),
`~/sfpi-uplift/laneR2-evidence-20260817` (setexp-fold),
`~/sfpi-uplift/lut-increment2-evidence-20260817` (LUT increment 2),
`~/sfpi-uplift/trackb-dst-ownership-evidence-20260816` (Track B B1-B4),
the weekly knob-attribution table in
`~/sfpi-uplift/sweep-2x2/weekly-20260817d`, and the addcmul attribution
record `~/sfpi-uplift/mechanism-scout-20260817/ADDCMUL-ATTRIBUTION.md`.

## Promotion gates run at this commit

- Full `rvtt.exp` FAIL set == the frozen environmental set (21 lines,
  set-identical to the pristine a96a9cd678d twin build run in the same
  environment); every focused family green.
- Testsuite-wide 4-leg assembly A/B (537 tests): new-default build ==
  pristine build with the three flags explicitly ON, and new build with
  the three `-mno-` spellings == pristine default (the off path is
  byte-preserved).  The only non-code deltas are DWARF `DW_AT_producer`
  strings on `-g` tests (explicit flags appear in the producer string;
  defaults do not).
- Full BH corpus compile A/B through the tt-llk harness: default build
  byte-identical (ELF `.text` per path) to the pristine compiler with
  the three flags explicitly ON; `-mno-` build byte-identical to the
  pristine default.
- WP8 byte-parity oracles re-minted: identical to the committed
  manifest on both builds.  cc-enable refusal-identity oracles: OFF==ON
  identity holds and the promoted build is byte-identical to the
  pristine twin.
- CRAQ smoke on the signature kernels (addcmul, sigmoid-tree, exp)
  built with pure default flags: PASS through libttsim.

## Test-expectation changes carried by this promotion

Five tests relied on the ambient default being off and now pin the
`-mno-` spelling (each carries a comment citing this promotion):
`tensix/setexp-fold-off-bh.C`, `sfpi/lut-select-default-off-bh.C`,
`sfpi/dst-ownership-inert-bh.C`,
`tensix/macro-planner-select-form-renamed-bh.C`, and
`tensix/macro-planner-where-raw-bh.C`.  The last two pin
`-mno-tt-tensix-optimize-dst-ownership` because the (now default-on)
ownership fold removes a provable-identity Dst reload from their raw
bodies before the planner runs — a pre-existing interaction, reproduced
on the pristine compiler with the explicit flag; those tests' subject
is the planner on the unfolded shape.  After the 2026-08-17 Where
silicon adjudication rewrote both tests into
cc-separator-kept-silicon-unproven refusal-trail pins (nkapre/sfpi
7f4caae659a), the pins were re-verified at the merged tip: the default
fold makes the folded shape refuse earlier (cc-template-unproved)
without ever reaching the adjudicated separator-kept refusal, so the
`-mno-` pins remain required for the tests to exercise their subject.
