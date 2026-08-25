# RVTT silicon validation record

This note records the completed 2x2 validation campaign rooted at
`/home/ttuser/sfpi-uplift/sweep-2x2/rooting-hardening-stride-final-20260825`.
It is a result for the **penultimate rooting compiler**, whose primary backend
pin is cc1plus SHA-256
`649ac9476522afb9ca6bd29af161a3afca577c6dc339d5c51c2f73f2d80d09e6`
(`649ac` below). It is not a validation result for the subsequent `075e`
compiler. The final-compiler and completion-guard work remain explicitly open
in [Final `075e` closeout](#final-075e-closeout).

## Evidence and provenance

The campaign's `PIN_STAMP`, `preflight.json`, and `MANIFEST.txt` agree on the
following provenance:

| component | archived value |
|---|---|
| evidence root | `/home/ttuser/sfpi-uplift/sweep-2x2/rooting-hardening-stride-final-20260825` |
| cc1plus (primary compiler pin) | `649ac9476522afb9ca6bd29af161a3afca577c6dc339d5c51c2f73f2d80d09e6` |
| compiler driver (secondary pin) | `a38b9ec01649498d57a5d1f673261779a1058639744b479f1b6be4810a7d2715` |
| compiler version | `riscv-tt-elf-g++ (wp8-planner) 15.1.0` |
| tt-metal revision used by the run | `e7961c5933ddce7dc954236086ef5419021fde43` |
| Blackhole CRAQ simulator | `32489dda4fd6321262fbc7c18b8f05126ed760073c7030dc529555111967ffd3` |
| Wormhole CRAQ simulator | `8f0079a9a16c782bf47a56c2e48e0c73d4da32faa69b56a307bc0b921b29dbc5` |
| reviewed pin record | `REVIEW_RECORD-649ac9476522.md`, SHA-256 `13e6e207d87730b49026bf7e1cc6b7f2b014cefba221818647e83f68c4b64cab` |
| evidence manifest | 101,309 entries; `SHA256SUMS` SHA-256 `fb7e9983c7cc69a81570e0986fa492cfa30db141ef698859853e43a563d72750` |

The CRAQ gate was active, not waived. The archived ON axis enables the
reviewed RVTT optimization set, including window pairing, window-pairing
stride, replay-record hoist, and LREG allocation. The OFF axis explicitly
disables the corresponding optimization family. The complete flag strings
are preserved in `preflight.json` and `MANIFEST.txt`; this note does not
substitute an abbreviated flag list for those records.

### Integrity check and mutable-workspace caveat

At the time this note was prepared, the referenced driver, cc1plus, both
simulator libraries, and review record existed and reproduced every hash in
the table. The archived checksums for `KERNEL-DELTA.md`, `MANIFEST.txt`,
`PIN_STAMP`, `REPORT.md`, `SCOREBOARD.md`, `preflight.json`, `scoreboard.json`,
`scoreboard.tsv`, and `silicon-batches/PLAN.txt` also matched `SHA256SUMS`.
These checks validate the cited provenance and summary layer; they are not a
claim that all 101,309 manifest entries were re-hashed while drafting this
note.

Two live aliases have advanced since the campaign. The preflight recorded the
harness toolchain realpath as `/home/ttuser/sfpi-uplift/sfpi-candidate-rooting`,
whereas it now resolves to
`/home/ttuser/sfpi-uplift/sfpi-candidate-final-075e9f2`. The live tt-metal
checkout is now `23f96f8c74d031050ab45686562317815cf0a4bb`, rather than the archived
`e7961c5933ddce7dc954236086ef5419021fde43`. These are mutable-workspace drift,
not discrepancies inside the evidence tree. Reproduction must therefore use
the archived hashes and revisions, not the current symlink or checkout HEAD.

## Validation accounting

The file-level census reproduces the campaign totals without consulting a
dashboard export:

| stage or artifact | observed result | direct evidence |
|---|---:|---|
| classifier verdicts | **854/854 OK** | 854 `*/classify/*/verdict.json` files; one compiler and tt-metal pin throughout |
| paired CRAQ verdicts | **439/439 OK, PASS/PASS** | 439 `*/craq/*/verdict.json` files: 427 Blackhole and 12 Wormhole |
| planned fresh silicon legs | **2,872/2,872** | `silicon-batches/PLAN.txt`: 2,872 total, 0 cached, 2,872 to run |
| logical silicon records | **2,872** | per-leg `rc.txt` and `session.txt` records |
| physical hardware sessions | **359/359 rc=0** | 359 unique session references and 359 batch-session `rc.txt` files, all zero |
| pytest calls completed | **2,849 passed** | sum of terminal pytest summaries in the 359 physical session logs |
| collection errors | **0** | no collection-error or non-passing terminal summary in those logs |
| reused scoreboard cells | **0** | `scoreboard.json` has `reused_cells: []` |
| materialized row verdicts | **263** | 263 `ROW-VERDICT.json` files and 263 `scoreboard.json` results |
| explicit corpus skips | **21** | 21 machine-readable `scoreboard.json` skip records |

Logical legs, pytest calls, and hardware sessions are deliberately distinct
units. The batch runner may satisfy several logical records in one physical
session, and byte-identical legs may share a physical observation within this
fresh run. That coalescing does not constitute reuse from an earlier campaign:
the plan reports zero cached legs and the final scoreboard reports zero reused
cells.

## Correctness and report policy

The correctness scoreboard is clean: `scoreboard.json` records `reds: []`.
All materialized CRAQ verdicts are paired OFF/ON passes, and all physical
silicon sessions completed successfully. The 21 omitted corpus entries are
not silent holes; each has an explicit skip record and reason.

`REPORT.md` nevertheless ends in **Overall: RED**. This is not a contradictory
correctness result. `REPORT.md` applies historical baseline policy to absolute
cycles, drift, and WIN/PARITY/LOSS class changes against the v1 diagnostic and
v2 KERNEL anchors. Its RED rows are baseline-policy regressions or class flips;
they are not entries in the correctness scoreboard's `reds` list. Consequently
the precise conclusion is:

- no archived correctness red was observed for the 649ac campaign; and
- the 649ac campaign did not satisfy every historical performance-baseline
  policy gate.

The second statement must not be rewritten as a silicon or semantic failure,
nor may the first be rewritten as an all-green performance report.

## Performance scoreboard

The authoritative verdict metric is drain-inclusive, end-to-end KERNEL time.
The row-local marker remains a diagnostic attribution metric. Using the
scoreboard's 0.5% band (`WIN < -0.5%`, `PARITY` within the band, and
`LOSS > +0.5%`) gives:

| comparison | population | WIN | PARITY | LOSS | unmeasured | summary |
|---|---:|---:|---:|---:|---:|---|
| compiler ON versus compiler OFF (causal) | 263 rows | 193 | 53 | 3 | 14 | **246/249 = 98.80% non-loss** among measured rows |
| compiler ON versus hand, `full2x2` population | 164 rows | 65 | 27 | 71 | 1 | **92/163 = 56.44% at-or-better** among measured rows |

The 99 `semantic` rows outside the `full2x2` population have no distinct hand
comparison and are therefore not counted as 99 additional hand failures. For
the causal result, the 14 unmeasured rows are excluded from the 249-row rate.
Neither percentage is a coverage percentage: coverage is reported separately
in the preceding section.

## `mulint32-fresh` stride result

The relevant result is `mulint32-fresh`, corpus id
`metal__ckernel_sfpu_mul_int32`. The separate legacy `mulint32` row is a
byte-identical refusal and must not be substituted for it. The immediately
preceding full campaign is
`/home/ttuser/sfpi-uplift/sweep-2x2/rooting-hardening-whfix-final-20260824`;
its `mulint32-fresh/ROW-VERDICT.json` is the prior column below. The current
column is from the corresponding verdict in
`rooting-hardening-stride-final-20260825`. Both full campaigns used the same
649ac cc1plus pin, so this is the relevant prior-to-stride campaign comparison.
The tt-metal revisions differ (`ea54ec3a331e6d697f560437231d0761b52c185b`
prior and `e7961c5933ddce7dc954236086ef5419021fde43` current), which is another
reason to treat the result as a full-campaign comparison rather than an
isolated single-pass experiment.

| metric | prior full campaign | current stride campaign | prior to current |
|---|---:|---:|---:|
| diagnostic sem OFF, cycles/tile | 49.9619140625 | 49.9619140625 | 0.000000% |
| diagnostic sem ON, cycles/tile | 39.474609375 | 35.97265625 | **-8.871407%** |
| diagnostic hand, cycles/tile | 35.6220703125 | 35.622721354166664 | +0.001828% |
| diagnostic causal | -20.990598% | -27.999844% | **-7.009245 percentage points** |
| diagnostic versus hand | +10.815034% | +0.982336% | **-9.832698 percentage points** |
| KERNEL sem OFF, cycles | 51,484 | 51,484 | 0.000000% |
| KERNEL sem ON, cycles | 40,744 | 37,158 | **-8.801296%** |
| KERNEL hand, cycles | 36,798 | 36,798.333333333336 | +0.000906% |
| KERNEL causal | -20.860850% | -27.826121% | **-6.965271 percentage points** |
| KERNEL versus hand | +10.723409% | +0.977399% | **-9.746010 percentage points** |

The stride campaign therefore holds KERNEL sem OFF constant, reduces sem ON
from 40,744 to 37,158 cycles, and narrows the end-to-end hand gap from +10.72%
to +0.98%. Under the 0.5% class band it remains a LOSS versus hand, exactly as
`KERNEL-DELTA.md` records. Because the ON axis contains the full reviewed
optimization set, this full-campaign comparison demonstrates the stride-era
movement but is not by itself an isolated single-pass A/B.

Separately, the current `REPORT.md` evaluates baseline policy against the
checked-in v1/v2 anchors. Its KERNEL anchor has sem ON 38,669 cycles, causal
-24.91%, and versus-hand +5.11%. That checked-in anchor is neither the
immediately prior full campaign nor an entry in the preceding table; it is
retained here only to make the report-policy comparison unambiguous.

## Final `075e` closeout

No `075e` classifier, CRAQ, or silicon result is present in the 649ac evidence
tree. Populate this section only from a new, hash-pinned final evidence root.
Do not copy the penultimate counts forward.

| final evidence item | required record | status |
|---|---|---|
| compiler provenance | full driver and cc1plus SHA-256, version, source revision, flags, `PIN_STAMP` | **TBD** |
| classifier | total expected, total completed, status/hash census | **TBD** |
| paired CRAQ | total expected/completed, OFF/ON outcome, BH/WH split, simulator pins | **TBD** |
| fresh silicon | logical legs, cached legs, physical sessions and rc census, pytest calls, collection errors | **TBD** |
| row accounting | materialized verdicts, explicit skips, reused cells | **TBD** |
| correctness | scoreboard red list and disposition | **TBD** |
| performance policy | report status and every RED/YELLOW disposition | **TBD** |
| causal scoreboard | WIN/PARITY/LOSS/unmeasured counts and denominator | **TBD** |
| hand scoreboard | eligible population, WIN/PARITY/LOSS/unmeasured counts and denominator | **TBD** |
| `mulint32-fresh` | final diagnostic and KERNEL raw cells, causal and hand deltas | **TBD** |

## Completion-guard census and targeted silicon

The 649ac broad sweep predates the final completion-guard closeout. It cannot
establish guard coverage merely because its broad correctness result is clean.
The final record must add both a compiler census and targeted silicon evidence:

| guard evidence item | required record | status |
|---|---|---|
| census provenance | exact `075e` compiler pin, target architecture, options, corpus/config revision | **TBD** |
| decision census | eligible sites; ordinary record-hoist fires/refusals; guarded fires/refusals; named refusal reasons | **TBD** |
| architecture coverage | separate Blackhole and Wormhole counts, including zero-count categories | **TBD** |
| default identity | default-off byte identity and proof that unrelated flags/passes are unchanged | **TBD** |
| targeted semantic matrix | guard OFF/ON correctness for audited forming and refusal shapes, BH and WH | **TBD** |
| targeted performance matrix | guard OFF/ON raw KERNEL samples, causal delta, and hand comparison where a hand arm exists | **TBD** |
| campaign linkage | exact targeted artifacts, session logs, ELF `.text` hashes, CRAQ verdicts, and final dashboard references | **TBD** |

Until those fields are populated, the defensible statement is that 649ac has
a complete and clean archived correctness campaign with strong aggregate
causal performance, while final `075e` and completion-guard validation remain
open.
