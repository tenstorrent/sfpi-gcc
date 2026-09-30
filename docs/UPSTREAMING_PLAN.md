<!--
   Copyright (C) 2026 Tenstorrent Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the General Public License along
   with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.
-->

# Landing the 31 new passes on `tenstorrent/sfpi-gcc`

Revised 2026-09-26.  The previous revision planned a 28-patch *refactoring*
stack aimed at upstream GCC's contribution gates.  That was the wrong target
in two ways: the gates it optimised for are not the ones this fork runs, and a
refactoring series does not land a single pass.  The goal is 31 passes in
`main`.  This revision plans for that, and for nothing else.

The deliverable of each submission is **one pass, compiled in, defaulted off,
with its tests**.  Nothing else travels with it.

One framing point that the plan turns on: the branch registers 56 passes, but
**25 of those were already in the backend when we forked it** (Nathan
Sidwell's and Paul Keller's work).  Only **31** are ours.  We are not
proposing a backend; we are proposing 31 optional additions to one he already
maintains, 27 of which are inert until a flag is passed.  That is a much smaller thing to
ask for, and it is what the cover message should say.

Counted from `rvtt-passes.def` at both ends, anchored on the `rvtt_` prefix,
**against the rebase base `417704f4d22`, not the original merge-base**: 56
registered today, 25 at `417704f4d22`, all 25 still present, **31 new**.
Earlier revisions derived 31 as 54 − 23 and as 32 − 22; both were arithmetic
on stale counts that happened to land near the right answer.  Upstream itself
added `pass_rvtt_schedule_ssa` and performed the `expand` → `vif` rename
between `48ba20142` and `417704f4d22`, which is why counting against the old
merge-base gives 33 and counting against the new base gives 31.  31 is the
number to use.

An earlier revision counted `pass_rvtt_replay` as ours.  The replay *pass* is
Keller's and Sidwell's.  The replay *work* is overwhelmingly ours — the family
is 15,854 lines today against 834 at the rebase base, three of its four files
did not exist, and all seven replay flags in the FIRE-BREADTH census are
ours — but the pass was already registered, so it is not one of the 31 we are
asking him to accept.  It travels as a modification to a pass he owns, which
is a different and harder review; plan it late in the series, not early.


## 1. The two facts that set the whole strategy

**The maintainer has never heard from us.**  Zero pull requests have ever been
opened against `tenstorrent/sfpi-gcc` or `tenstorrent/sfpi` from this account
(`gh api search/issues?q=author:nkapreTT+repo:tenstorrent/sfpi-gcc` →
`total_count: 0`, same for `sfpi`).  `grep -ri 'sidwell\|nathan'` over every
campaign document and `WORKLOG.md` returns nothing.  176,497 insertions exist
that the one person who can merge them has not seen.

Everything in this plan is downstream of fixing that, and the fix is a
conversation, not a patch series.

**27 of the 31 new passes land inert.**  They are gated on a
`-mtt-tensix-*` option that is `Init(0)`, and the backend's standing
rule (`gcc/config/riscv/tt/README`) is that with the flag off the emitted
binary is bit-for-bit identical to the previous build.  That is the entire
argument for why a 31-pass series is acceptable at all: **each one is provably
a no-op until somebody asks for it.**  A reviewer is being asked to accept new
code, not new behaviour.

**The four exceptions must be in the cover message, not discovered.**  Read
off the pass gates, not off the option table — the two disagree, and the gate
is what runs:

- `pass_rvtt_lut_select` — gate requires `riscv_tt_opt_lut_select`, and
  `-mtt-tensix-optimize-lut-select` is `Init(1)`.  Genuinely on by default.
- `pass_rvtt_dst_ownership` — gate is bare `TARGET_XTT_TENSIX`, so the pass
  **always runs**; `-mtt-tensix-optimize-dst-ownership` (`Init(1)`) is read
  inside `execute` and only selects transform-vs-analyse.  On by default, but
  not for the reason the option table suggests.
- `pass_rvtt_lreg_livein` — no option at all, gate is bare
  `TARGET_XTT_TENSIX`.  A correctness/visibility pass whose absence is silent
  wrong code.
- `pass_rvtt_spill_diag` — no option at all, gate is bare `TARGET_XTT_TENSIX`.
  A diagnostic, a proven no-op on a clean stream.

The last two are defensible as always-on and their headers say why; the first
two need either a measured justification for the default or a flip to
`Init(0)` before submission.

`pass_rvtt_replay_reform` was listed here in an earlier revision as a third
`Init(1)` exception.  It is not: its gate is
`riscv_tt_opt_replay > 0 && riscv_tt_opt_post_autoincr_window > 0`, and
`-mtt-tensix-optimize-post-autoincr-window` is `Init(0)`.  The conjunction
makes it inert, and it is one of the 27.


## 2. What the maintainer actually gates on

Measured from his last 50 commits on `main` (2026-08-11 → 2026-09-24) and from
the 21 pull requests on the fork, 13 of which he merged.

| | his practice | our branch |
|---|---|---|
| subject | median 34 chars, max 60 | median 71, max 201 |
| body | **empty in 50 of 50** | median 26 lines |
| issue ref `#NNNNN:` | 37 of 50 | **0 of 490** |
| files per commit | median 3, p90 11 | median 6, p90 19, max 548 |
| insertions | median 97, p90 448 | p90 1210; 16 over 2000 |
| tests in the same commit | 39 of 50 | broadly yes |
| merge commits | **0 — `main` is linear** | 0 — *the rebase linearised us; not a gap* |
| comment style | `//` | `/* */` |
| ChangeLog | **none** | none — *we match; not a gap* |

Two corrections to the previous revision, both material:

- **ChangeLog entries are not a gate.**  The old plan treated
  `contrib/gcc-changelog/git_check_commit.py` as an automatic bounce.  All 50
  of his commits have empty bodies and would fail it.  The fork does not use
  ChangeLogs.  Stop spending effort here.
- **GNU style is not a gate either.**  His current `gimple-rvtt-combine.cc`
  carries 25 over-80-column lines.  `check_GNU_style.py` is not run.

**The gate he does run** is a full build and test of the superproject.  He
pushes `nsidwell/<topic>-<issue#>` to `tenstorrent/sfpi` and manually
dispatches the "Development" workflow — `scripts/build.sh --gdb --tt-built`,
`--dejagnu`, `--test-tt`.  He did this 15+ times in the window this branch ran
**zero** builds.  A submission that has not been through it is unreviewable on
its face.

**His test form is `dg-do compile` + `check-function-bodies "**" ""`**, which
pins the exact expected assembly of the whole function.  His `tt/` suite on
`main`: 166 `check-function-bodies`, 22 `scan-assembler`, **0 `scan-rtl-dump`,
0 `scan-tree-dump`**.  Ours leans the other way — 632 files using
`scan-rtl-dump`, 543 using `scan-tree-dump`, and 173 using
`check-function-bodies` of which only **7 are branch-new**.  An earlier
revision gave these as 839/423/6 and called our suite "the inverse"; the real
gap is narrower than that — we do use his form, we just almost never add to
it.  Per-pass dump scanning is
legitimate and we should keep it in our own tree, but **every pass we submit
needs at least one `check-function-bodies` test**, because that is the form he
reads.

He also does not watch the repo.  From PR #19, in his own words:

> "For the future, for some reason I don;t get emails about PRs here, and
> they're so rare I don't actively look.  Feel free to ping after, say, a week."


## 3. Prerequisites — none of §4 starts until all four are done

**P1 — Ask him.**  One message, before any code: here are 31 optional passes for an
SFPU backend you already maintain, all default-off, all with tests; would you rather
see them one PR per pass, or should we talk about the shape first?  His answer
reorders everything below and costs a day to get.

**P2 — Rebase onto `main`.**  ~~The branch is 49 commits behind and no longer
applies.~~  **DONE 2026-09-26.**  The whole branch was rebased onto
`417704f4d22`, not per-pass as the original text advised.  That call was right
in the end: the churn was not file renames, it was the **builtin ABI** —
`rvtt-insn.def` moved +38/-30 lines, and a per-pass rebase would have paid the
same reconciliation 31 times.  The rename the original text predicted
(`gimple-rvtt-expand.cc` -> `gimple-rvtt-pred.cc`, `pass_rvtt_expand` ->
`pass_rvtt_vif`) is carried — though upstream, not we, performed it.  The
branch is now 490 ahead, 1 behind, and linear.

What the rebase cost.  The two columns are **not the same suite**: rebasing
pulled in 26 upstream tests, so the suite grew 1449 -> 1475.  Totals are
therefore not comparable line-for-line; the set-difference below is: 

| | pass | compile-fail | scan-fail |
|---|---|---|---|
| pre-rebase | 1174 | 1 | 232 |
| now | 1072 | **0** | 361 |

Compile failures are at zero, better than the pre-rebase baseline.  Of the 361
scan failures, **233 also fail on the pre-rebase compiler** (the local runner
is a DejaGnu approximation, not DejaGnu); the number that matters is **128
tests failing that did not fail before**.

Corpus coverage is at parity on the corpus the sweep races — **48 of 48
kernels** — with 71 firing cells against the control's 88, of which 12 are
`reassoc-mad-restructure`, subsumed by upstream's own mad-formation and
measured as a 47-instruction *improvement*.  (The "134 raced kernels" that
orders §4's waves is the board's kernel population, a different and larger
set than the 48-kernel rebase-regression corpus.  Do not read 48/48 as
coverage of the 134.)

Upstream also moved immediate lowering ahead of our late passes, so a 32-bit
constant now arrives as an `SFPLOADI` + `SFPLOADI_LV` pair.  Six passes broke
on that alone.  `rvtt_build_loadimm32` and `rvtt_chained_loadi_root` exist for
exactly that.  Full record: `craq-sfpi/HANDOFF.md` and
`REBASE-OPEN-QUESTIONS.txt` items 14-19.

The 128 remaining failures are not evenly spread: crosscall-hoist 18,
macro-planner 17, const-residency 14, lut-select 11, store-source 9.  Note
that `store-source` and `const-residency` are both `rvtt_prgm_const` knobs —
they are **Wave B** debt, not Wave A.  Scored by wave, Wave A's four passes
carry essentially none of the 128 and Wave B carries about 30 on
`prgm-const` alone.

Two per-pass gates follow from the above.  They are **additions to §3's four
prerequisites**, made here rather than in the numbered list because they are
per-pass and not one-time:

- a pass that reads or builds a 32-bit constant is re-checked against the
  `SFPLOADI`/`SFPLOADI_LV` pair before it is sent;
- a pass whose test family still fails is not sent — its tests are the
  evidence the PR rests on.

**P3 — One green Development run.**  Push a topic branch to
`tenstorrent/sfpi`, dispatch the workflow, get a green `--test-tt`.  This
branch has never been built by CI: the only workflow it ever ran was
`pin-review-lint`, a few seconds per run, and that workflow has since been
deleted (the Actions API now reports zero runs for the fork, so the exact
count is no longer reproducible; the substantive claim — no build, no test,
ever — holds).  Until one green run exists, we do not know that the series
builds against current `main`.

Note the ordering tension with P2: `--test-tt` runs `check-gcc-tt`, which is
the suite carrying the 128 regressions.  P3 is therefore downstream of closing
them, not parallel to it.

**P4 — Strip the false attributions.  ~~PENDING~~ DONE — and it was never 27.**

The claim was that 27 branch-new files carried
`Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org)`
inherited from the file they were split out of.  Measured across the branch's
history, the high-water mark was **10 files, 6 of them branch-new**, at
`3595e316b07^` — just before `tt: correct authorship on the 14 files split out
of the inherited passes` fixed it.  **Today exactly four tracked files carry
the string, and all four exist at `417704f4d22`** — they are his files, where
his name is correct.  Branch-new files carrying it: **zero**.

The supporting detail in the earlier text described a tree that no longer
exists.  The rebase dropped both splits: there are no `rtl-rvtt-sched-*.cc`
files (`rtl-rvtt-schedule.cc` is one 8,644-line file again) and no fourteen
`rvtt.md` topic files (`rvtt.md` is 5,259 lines).

**Keep the fix that is already in the tree.**  Seven files carry the corrected
form — `Split out of <file>, rewritten by Nathan Sidwell` — which records the
derivation honestly without signing his name to code he has not seen.  The
earlier prescription ("the rest carry Tenstorrent copyright and no personal
credit") would *undo* that and strip credit from code genuinely derived from
his GPL-3 work.  Do not apply it.  The count of other branch-new files in
`tt/` is 134, not 128.


## 4. The series: one pass per pull request

Each PR contains exactly:

```
  gcc/config/riscv/tt/<pass>.cc            the pass
  gcc/config/riscv/tt/rvtt-passes.def      one INSERT_PASS line
  gcc/config/riscv/tt/rvtt-protos.h        one make_pass_* declaration
  gcc/config/riscv/tt/t-riscv-tt           one object in RVTT_OBJS
  gcc/config/riscv/riscv.opt               its Init(0) option(s) -- a pass
                                           may own several; pass_rvtt_replay
                                           owns twelve
  gcc/doc/invoke.texi                      one option paragraph
  gcc/testsuite/g++.target/riscv/tt/...    tests, >=1 check-function-bodies
```

Subject `#NNNNN: Add <pass-name> pass` if a ticket exists, else
`Add <pass-name> pass`.  Empty body, or two sentences if the pass needs a
sentence of motivation.

**This template does not survive contact with the tree, in two ways, and both
have to be settled before the first PR — see §7.**

*One `.cc` per pass is not true today.*  Six files register more than one
pass: `gimple-rvtt-immvar.cc` (3), `gimple-rvtt-replay-unroll.cc` (3),
`gimple-rvtt-synth.cc` (3), `gimple-rvtt-check.cc` (2),
`gimple-rvtt-dst-iteration.cc` (2), `rtl-rvtt-replay.cc` (2).  `launch-flatten`
in particular has no file of its own — its pass class lives inside
`gimple-rvtt-replay-unroll.cc`.  Either those files are split before
submission, or those passes travel in groups.

*No pass travels alone.*  Every Wave A pass depends on shared infrastructure
that exists nowhere upstream and is **not flag-gated** — it compiles into
`cc1plus` unconditionally:

```
  rvtt-effects          1480     rvtt-macro-tables     1675
  rvtt-cc-region        1528     rvtt-pressure         1241
  rvtt-raw-boundary      996     rvtt-trips             671
  rvtt-macro-ownership   512     rvtt-refuse            496
  rvtt-placement         438
                                 total  9,037 lines, 18 files (.cc + .h)
                                 upstream: 0 of 18
```

So the first PR is not 629 lines.  Whatever ships first drags some or all of
this with it, and §1's "provably a no-op until somebody asks for it" argument
covers pass *bodies* — it does not cover 9,037 lines the maintainer then owns
unconditionally.  The dependency closure per pass has not been computed; doing
that is a prerequisite to promising him any patch size at all.

### The ordering principle

Waves are ordered by **how many of the 134 raced kernels the pass actually
reaches**, not by how easy it is to review.  An earlier revision of this plan
sorted by line count and led with `int-not` — a pass whose entire measured
benefit is one kernel row.  That is the worst possible opening: it makes a
year of work look like a bag of one-off hacks, which is exactly the charge the
generality census exists to answer.

Reach, for all 31 (kernels touched of 134, source lines):

```
 85  1962  dst-autoincr          10     *  launch-flatten
 44  2053  invariant              8  1980  macro-planner
 40  1271  store-fold             7  1058  ccmask
 39   629  delivery-shape         6  1150  dst-iteration
 28  1673  reassoc                5  1023  prgm-const
                                  4   563  crossloop
                                  4  3580  crosscall
                                  3  1764  lut-select
   5 passes reach >= 20           8 passes reach 3-19      18 reach < 3

  * launch-flatten has no file of its own; see the template note above.
    LOC re-measured 2026-09-26; the previous column was pre-rebase.  crosscall
    nearly doubled (1902 -> 3580) closing rebase fallout.
```

A "0" in that census means one of three different things and they must not be
conflated: the pass has **no flag at all** (`spill-diag`, `lreg-livein`,
`lp-alloc`, `crosslane-window`, `lp-schedule-prera` — always-on, so the census
has no row by construction); the pass's flag is **named differently** from the
pass (`replay-reform` is measured as `post-autoincr-window`, 13 kernels;
`macro-planner-residency` appears in the census as `planner-residency`); or the
pass genuinely **fires nowhere on this board** (`mop-form`, `round-interleave`).
Resolve which before using a zero as an argument.

The three buckets are 5 + 8 + 18 = 31.  An earlier revision printed 19 in the
last bucket, which summed to 32.

### Wave A — the four that carry the result

| pass | kernels | loc | why here |
|---|---|---|---|
| `delivery-shape` | 39 | **629** | **Send this first.**  Real breadth, and the pass itself is inside his observed 843-line ceiling — but see the dependency-closure note above before promising that number.  It is also the project's thesis in one pass: choosing push vs launch vs record from a cost model rather than a heuristic. |
| `dst-autoincr` | **85** | 1962 | The widest pass on the board (76 test files).  Over the ceiling, but it already splits — `rtl-rvtt-dst-autoincr-scan.cc` is a separate file — so it can go as a two-patch series if he prefers. |
| `invariant` | 44 | 2053 | Second-widest.  Needs a contract written first — 2053 lines behind a title line today.  (Earlier revisions cited "§3, P5" for this; there is no P5.  It is a per-pass gate, not a prerequisite.)  Its flag is `-mtt-tensix-optimize-invariant-loadi`, not `-mtt-tensix-optimize-invariant`. |
| `store-fold` | 40 | 1271 | Exemplary proof discipline already — every fold tied to a `tt/proofs/` artifact with a named standing refusal. |

If Wave A merges, the pattern is proven and the rest is throughput.  If it does
not, we have learned that on the passes that matter rather than on trivia.

### Wave B — real reach, needs a size or design conversation

`reassoc` (28) — the licensed FP pass; argue the double key on its own, not
buried in a series.  `launch-flatten` (10).  `macro-planner` (8, 166 test
files, fronting a 26-file subsystem — it needs to reference
`docs/MACRO_PLANNER.md` from its header before it goes).  `ccmask` (7).
`dst-iteration` (6).  `prgm-const` (5).  `crossloop` (4).  `crosscall` (4).
`lut-select` (3 — and one of the four §1 exceptions, `Init(1)`).

Wave B carries most of the outstanding rebase debt: about 30 of the 128
failures sit on `prgm-const` (`store-source`, `const-residency`) and 17 on
`macro-planner`.

### Wave C — the narrow ones, sent as one batch and framed honestly

The 18 passes reaching fewer than three kernels, including `int-abs` and
`int-not` at exactly one each, both `birth_share 1.00`.  Their headers already
say it: *not claimed to generalise; claimed to be correct and free.*  Each is
exhaustively proven over 2^32 and each is inert with its flag off.  That is a
footnote to the story, not the opening — send them together, late, with the
census slide's own numbers attached so the narrowness is the project's
disclosure rather than a reviewer's discovery.

### Wave D — the two that are not flag-gated at all

Not a fifth bucket: these are a **carve-out from Wave C**, which is why the
waves must not be summed.  A + B + C = 4 + 9 + 18 = 31; Wave D is two of C's
18 named separately.  (An earlier revision listed nine here and summed the
waves to 41 for a 31-pass series.)

Only **`spill-diag` and `lreg-livein`** have no flag — gate is bare
`TARGET_XTT_TENSIX`.  The other seven previously listed here were checked
against their gates and are all `Init(0)`-gated, i.e. inert, i.e. ordinary
Wave C members: `crosslane-window` (`riscv_tt_opt_crosslane`),
`lp-schedule-prera` (`…_pressure_schedule_prera`), `lp-alloc`
(`…_pressure_schedule`), `reprprop` (`…_repr_prop`), `dst-interleave`
(`…_dst_iteration_fusion`), `replay-reform` (`…_replay && …_post_autoincr_window`,
the second `Init(0)`), `replay-unroll` (`…_replay_loop_unroll`).

`lreg-livein` is **482 lines** with **3 tests** in a 1,573-file suite
(`raw-lreg-livein-bh.C`, `raw-lreg-livein-cfg-wh.C`, `raw-lreg-livein-cfg-bh.C`).
It is also, per §1, **a correctness pass whose absence is silent wrong code in
the compiler he ships today** — which makes filing it last a question rather
than a conclusion.  See §7.

## 5. What is deliberately NOT in the series

**The `rvtt.md` split.**  ~~Keep the split in our tree only.~~  **Moot — the
rebase dropped it.**  `rvtt.md` is one 5,259-line file again and the fourteen
topic files are gone.  The reasoning stands as a rule for next time: a split
that moves 2378 of his lines into new files carrying his name, destroys
`git blame` on the file he edits most, and collides with in-flight work is the
change most likely to stop him reading.  Do not re-create it.

**The deletion of `rtl-rvtt-hll.cc`** (1,289 lines, "Originated by Paul
Keller", a GS memory-arbitration erratum workaround) and the five flags turned
into hard errors.  **Correction: this deletion is not on the branch.**  The
retirement was reverted — the pre-rebase tip commit is literally
`Revert "RISC-V: tt: Retire the high-latency-load scheduling pass"` — and
`pass_rvtt_hll` is registered at HEAD.  §1's earlier claim that we "retired"
`rvtt_hll` was wrong.  The flag removals were done through the `.opt`
`Removed` mechanism rather than an `error()` call.  Only two of the five pre-existed — `mtt-optimize-hll`
and `mtt-tensix-optimize-combine`, the latter `Init(1)`, i.e. **on by default**
at merge-base.  Retiring another engineer's erratum workaround and breaking an
on-by-default flag are product decisions the maintainer owns.  Raise both in
conversation; do not send them as patches.

**The three `Undocumented` deliberate-miscompile knobs** —
`-mtt-tensix-mve-expand-sabotage`, `-mtt-tensix-macro-planner-verify-corrupt-template`,
`-mtt-tensix-trips-oracle-skew=`.  Defensible as red/green harnesses, but
shipping intentional-miscompile switches in a production compiler is his call
and needs its own patch and its own argument.

**`gcc/system.h`** (+9/−0: `INCLUDE_UNORDERED_MAP`, `INCLUDE_UNORDERED_SET`,
`INCLUDE_TUPLE`).  This is generic
GCC, not the backend.  Send it to `gcc-patches@gcc.gnu.org`, or drop the
dependency and use `hash_map`/`hash_set`, which is what upstream steers to.

**`--with-lp-solve`.**  Default off, never searched in the target sysroot, and
the vendored branch-and-bound solver is the primary backend with lp_solve as a
cross-check only.  Low risk, but an LGPL-2.1 dependency in `cc1plus` is a
licensing question only he can answer.  Its own patch, after Wave A.


## 6. Honest sizing

- **Wave A is a week** once P1-P3 are done (P4 is already done), *if* the
  dependency-closure question in §4 resolves favourably.  If the 9,037 lines of
  shared infrastructure cannot be staged separately, Wave A is not a week.
- **Wave B and C are quarters, not weeks**, against a reviewer whose largest
  merged PR is 843 lines and who does not watch the repo.  That is not a
  criticism of him; it is the arithmetic of 31 passes averaging 1,200 lines.
- The realistic outcome of asking first is that he proposes a different
  shape — a single `config/riscv/tt` subdirectory drop, or a staged vendor
  branch, or review-by-subsystem.  Any of those is better than this plan, and
  we will not know until we ask.

Nothing here is gated on silicon.  The promotion question — production
compiles with **6** default-on passes while the board measured **39** flags,
and two of the largest measured wins can never be defaults because production
sets `-fno-associative-math` — is a separate and larger programme.  It does
not block landing an optional pass that is off by default, and it should not
be allowed to.


## 7. Open decisions — not resolvable from the tree

Three questions block the cover message.  Each was surfaced by measurement;
none can be answered by more measurement.

**7.1 — AI provenance.**  Of the 490 commits on this branch, **335 carry
`Co-Authored-By: Claude`** and **zero carry `Signed-off-by`**.  §4's
per-PR template said "No `Co-Authored-By` trailers — he uses none", which is
true of his practice and is also an instruction to remove the record of who
wrote the majority of ~176,000 lines of GPL-3 code before sending it to an
external maintainer who may forward it to `gcc-patches`.  This plan is
otherwise scrupulous about not signing his name to code he did not write (P4);
the same principle points the other way here.  Options: keep the trailers;
strip them but disclose in the cover message; strip them silently.  **This is
not a style question and should not be settled as one.**

**7.2 — What goes first.**  The plan opens with `delivery-shape` because it is
the project's thesis and fits his size ceiling.  But `lreg-livein` is a
**correctness pass whose absence is silent wrong code in the compiler he ships
today** (§1), and it is currently scheduled last, behind 30 optimisations.  A
first contact that says "we found a wrong-code bug in your backend, here is the
fix, and separately we have 30 optional passes" is a different and possibly
better opening than "here is optional pass 1 of 31".  It also costs the thesis
framing.  Decide deliberately.

**7.3 — Whether one-pass-per-PR survives.**  §4 shows it does not, as written:
six files hold multiple passes, `launch-flatten` has no file at all, and 9,037
lines of un-flag-gated shared infrastructure underlie Wave A.  Either we
compute the per-pass dependency closure and split the infrastructure into its
own preparatory PRs, or we stop promising one-pass-per-PR and ask him for a
different shape in P1.  P1 is the natural place to put this question — which
is another reason not to send anything before it.
