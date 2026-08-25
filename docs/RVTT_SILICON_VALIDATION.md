# RVTT silicon validation record

> **Promotion status (2026-08-25): REJECTED.**  This document accurately
> records archived measurements, but the final `075e` candidate was never
> ceremonied as a canonical pin.  The associated ON-26 configuration was
> self-ratified without the required witness/adjudication chain, the reported
> DejaGnu universe was incomplete relative to pin 28, and the report itself
> contains material versus-hand regressions.  Do not use this record as proof
> of a shipping toolchain or ON set.  The canonical live installation has been
> restored to pin 28; the defensible default result is 67 versus-hand wins.
> Window-pairing stride is a high-confidence knob result, not yet a promoted
> default.

This note preserves the completed penultimate 2x2 campaign rooted at
`/home/ttuser/sfpi-uplift/sweep-2x2/rooting-hardening-stride-final-20260825`
and records the subsequent final `075e` closeout.  The historical campaign's
primary backend pin is cc1plus SHA-256
`649ac9476522afb9ca6bd29af161a3afca577c6dc339d5c51c2f73f2d80d09e6`
(`649ac` below).  Final-candidate results are reported separately in
[Final `075e` closeout](#final-075e-closeout); counts are never copied between
the two campaigns.

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
checkout is now `0c4e6c417db1e4811f5c51f9a940eb9cac9e098c`, rather than the archived
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

The final fixed-head evidence root is
`/home/ttuser/sfpi-uplift/sweep-2x2/pin29-on26-final-984c9a-20260825`.
Its immutable run pins tt-metal
`984c9a687c4e4429f0a5a984f2ebf832c4bedff4`, compiler source
`075e9f2f4b22dd08342be730d42e34060da10d4a`, driver SHA-256
`a04de6aad4c29aa222e7b5f2e9d699b8bb89fec6accfd38dcf4a78e72e47e720`,
and cc1plus SHA-256
`45ba7169920924fd6ebeb6eeb3766156b413dbf895e091b53603bed1e35e7d79`.
The recorded compiler version is `riscv-tt-elf-g++ (wp8-planner) 15.1.0`.
Its review record is `REVIEW_RECORD-45ba71699209.md`, SHA-256
`8bea53e8c060dae6d519155455894d13231d4a97edf6ca093d7315383a0e7811`.

The complete final accounting is:

| final evidence item | observed result |
|---|---:|
| classifier | **854/854 OK**; 588 CHANGED and 266 IDENTICAL |
| paired CRAQ | **439/439 OK, PASS/PASS**; 427 Blackhole and 12 Wormhole |
| fresh logical silicon legs | **2,839/2,839**, 0 cached or reused; 721 correctness and 2,118 performance |
| physical sessions | **359/359 rc=0** |
| device pytest calls and phases | **2,816/2,816 calls passed**; 8,448/8,448 setup/call/teardown phases passed |
| collection errors | **0** |
| measurement cells | **824 diagnostic + 824 KERNEL**; every numeric cell has three samples |
| row accounting | **263/263 runnable verdicts** plus 21 explicit skips |
| registry accounting | **284/284**: 164 full 2x2, 99 semantic-only, 21 explicit skips |
| scoreboard correctness reds | **0** |

The archived `SHA256SUMS` contains 100,505 entries and verifies in full.  Its
SHA-256 is
`fbab34048fcb8650052a12ab74764703c292601e55e7d662bf88140596af958a`;
`MANIFEST.txt` is
`576f5dabe034d3d32154e3672f5750f8466ec78093878dfee2f3505d1d8bb4e6`,
and `scoreboard.json` is
`23bbd78b0e37a5657c9c83146853fce385cb9ec21d79381b7a3fd42f144c5402`.
The Blackhole and Wormhole simulator hashes remain respectively
`32489dda4fd6321262fbc7c18b8f05126ed760073c7030dc529555111967ffd3`
and `8f0079a9a16c782bf47a56c2e48e0c73d4da32faa69b56a307bc0b921b29dbc5`.

Using the authoritative KERNEL metric and the documented +/-0.5% band, final
semantic ON versus OFF is **193 WIN / 54 PARITY / 1 LOSS / 15 unmeasured**:
247/248, or **99.60%**, of measured rows are at-or-better.  The sole causal
loss is `reduce-sdpa` at +0.776%.  Against hand on the 164 full-2x2 rows, the
result is **66 WIN / 27 PARITY / 70 LOSS / 1 unmeasured**: 93/163, or
**57.06%**, at-or-better among measured rows.  These are performance outcomes,
not the registry-accountability percentage.

For `mulint32-fresh`, diagnostic OFF/ON/hand are respectively
49.9638671875, 35.97265625, and 35.622721354166664 cycles/tile.  That is a
-28.002658% causal delta and +0.982336% versus hand.  KERNEL OFF/ON/hand are
51,486, 37,157.666666666664, and 36,798.333333333336 cycles: -27.829572%
causal and +0.976494% versus hand.  Relative to the preceding final-candidate
full result, semantic ON moves from 40,744 to 37,157.667 KERNEL cycles, about
-8.80%, while the hand gap contracts from +10.723409% to +0.976494%.

The immutable run's historical-baseline `REPORT.md` is RED despite the clean
correctness scoreboard: 244 GREEN, 6 YELLOW, and 13 RED.  Two REDs (`lcm-fresh`
and `gcd-fresh`) were report-transition false positives caused by comparing
sub-band PARITY movement as a WIN-to-LOSS transition.  The canonical harness
fix at tt-metal `0c4e6c417db1e4811f5c51f9a940eb9cac9e098c` uses the documented
+/-0.5% classes and has regression fixtures.  The structural `softplus` RED
was a missing lower-bound record, not a semantic failure; the audited bound is
120.125 issue slots/tile.  A separate fresh, zero-reuse ceremony rooted at
`softplus-reportfix-0c4e6c-20260825` completed 2/2 classifier, 1/1 paired CRAQ,
2/2 correctness, and 6/6 performance legs and reports Overall GREEN with no
scoreboard reds.  Its `SHA256SUMS` verifies and has SHA-256
`2b6363efa9c00108dc71b3171abcbdf9b1fa3c4454c4e96f9406bfc69d9a51e5`.

After those mechanical dispositions, the remaining ten RED rows are
historical performance-policy exceptions: `minmax-min`, `minmax-max`,
`polygamma`, `relu`, `sign`, `hardtanh-fresh`, `cbrt-fresh`,
`hardshrink-fresh`, `threshold-fresh`, and `sigmoidlut-fresh`.  They are not
correctness, CRAQ, collection, or silicon-semantic failures.

## Completion-guard census and targeted silicon

The completion guard remains opt-in and default-off.  A strict final-compiler
census over all 263 runnable registry rows is rooted at
`pin29-on26-final-guard-census-984c9a-20260825`: 263/263 verdicts are OK,
215 are byte-IDENTICAL, and 48 are CHANGED when the guard flag is added.  There
are no omitted, missing, invalid, or excluded rows.  `KNOB-CENSUS.json` has
SHA-256
`172d1b21a6b03dfcbcf98fd46359dd7a8088340590db7c9f46611b4e5ae89cdd`.

All 48 changed rows then received fresh targeted guard silicon at
`pin29-on26-final-guard-silicon48-984c9a-20260825`.  The target adopted exactly
484/484 main-matrix leaves from the fully verified source root, with exact
provenance, job-key, and ELF `.text` equivalence, and executed **384/384 fresh
guard leaves**: 96 correctness and 288 performance.  Guard CRAQ is **50/50
PASS/PASS**: 48 Blackhole pairs plus Wormhole twins for `typecast` and `where`.
All 48 guard records are status OK, every OFF/guard correctness leg passes,
and each performance leg has exactly three diagnostic and three KERNEL
samples.  Both the source and target `SHA256SUMS` manifests verify in full.
The targeted target hashes are:

| artifact | SHA-256 |
|---|---|
| `KNOB-CENSUS.json` | `fcb0532689aea40112fcf83d1274d3ace90556a21088d6e16b8540d0317b42d4` |
| `scoreboard.json` | `3410a5198347618545b62eb95f5b8cfb3ce25cba55b022a05571bd3ee1f50238` |
| `MANIFEST.txt` | `a9c4e2160f1bca4dc13fe7ca9c1351de0d879d578c96d681948345a4dcea6a37` |
| `SHA256SUMS` | `56173ed19ad045f5542d04630a39368773ac017d52937b01f610f4fda1313602` |

The targeted census correctly records `full_registry_coverage=false`: it is
the selected 48-row changed subset, while the separate strict census above is
the 263-row registry proof.  The target scoreboard has no reds.  Its inherited
overall-report RED is solely the main `polygamma` historical baseline flip,
not a guard semantic failure.

Guard KERNEL performance is **11 WIN / 22 PARITY / 15 LOSS** at +/-0.5%.
Notable wins include `tanhshrink` (-2.157%), `mish` (-1.416%),
`gelu-licensed` (-1.352%), `polygamma` (-1.123%), and `where` (-0.951%);
the worst loss is `typecast` (+2.641%).  Diagnostic-body and drain-inclusive
KERNEL direction can disagree, so promotion must use KERNEL: for example,
`where` is +15.77% diagnostically but -0.951% in KERNEL.  With 15 losses versus
11 wins, the evidence rejects unconditional promotion.  The completion guard
therefore remains opt-in pending a selective, architecture-grounded
profitability gate; its semantic coverage is complete and green.
