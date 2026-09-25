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

# Landing the 32 new passes on `tenstorrent/sfpi-gcc`

Revised 2026-09-25.  The previous revision planned a 28-patch *refactoring*
stack aimed at upstream GCC's contribution gates.  That was the wrong target
in two ways: the gates it optimised for are not the ones this fork runs, and a
refactoring series does not land a single pass.  The goal is 32 passes in
`main`.  This revision plans for that, and for nothing else.

The deliverable of each submission is **one pass, compiled in, defaulted off,
with its tests**.  Nothing else travels with it.

One framing point that the plan turns on: the branch registers 54 passes, but
**22 of those were already in the backend when we forked it** (merge-base
`48ba20142`, 2026-08-11 — Nathan Sidwell's and Paul Keller's work).  Only 32
are ours.  We are not proposing a backend; we are proposing 32 optional
additions to one he already maintains, 23 of which are inert until a flag is
passed.  That is a much smaller thing to ask for, and it is what the cover
message should say.


## 1. The two facts that set the whole strategy

**The maintainer has never heard from us.**  Zero pull requests have ever been
opened against `tenstorrent/sfpi-gcc` or `tenstorrent/sfpi` from this account
(`gh api search/issues?q=author:nkapreTT+repo:tenstorrent/sfpi-gcc` →
`total_count: 0`, same for `sfpi`).  `grep -ri 'sidwell\|nathan'` over every
campaign document and `WORKLOG.md` returns nothing.  168,691 insertions exist
that the one person who can merge them has not seen.

Everything in this plan is downstream of fixing that, and the fix is a
conversation, not a patch series.

**23 of the 32 new passes land inert.**  They are gated on a
`-mtt-tensix-optimize-*` option that is `Init(0)`, and the backend's standing
rule (`gcc/config/riscv/tt/README`) is that with the flag off the emitted
binary is bit-for-bit identical to the previous build.  That is the entire
argument for why a 32-pass series is acceptable at all: **each one is provably
a no-op until somebody asks for it.**  A reviewer is being asked to accept new
code, not new behaviour.

The remaining 9 are a different conversation and are deliberately last (§5).


## 2. What the maintainer actually gates on

Measured from his last 50 commits on `main` (2026-08-11 → 2026-09-24) and from
the 21 pull requests on the fork, 18 of which he merged.

| | his practice | our branch |
|---|---|---|
| subject | median 34 chars, max 60 | median 73, max 203 |
| body | **empty in 50 of 50** | median 26 lines |
| issue ref `#NNNNN:` | 38 of 50 | **0 of 442** |
| files per commit | median 3, p90 11 | median 6, p90 19, max 548 |
| insertions | median 97, p90 448 | p90 1210; 14 over 2000 |
| tests in the same commit | 39 of 50 | broadly yes |
| merge commits | **0 — `main` is linear** | 129 |
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
0 `scan-tree-dump`**.  Ours is the inverse — 839 `scan-rtl-dump`, 423
`scan-tree-dump`, 6 `check-function-bodies`.  Per-pass dump scanning is
legitimate and we should keep it in our own tree, but **every pass we submit
needs at least one `check-function-bodies` test**, because that is the form he
reads.

He also does not watch the repo.  From PR #19, in his own words:

> "For the future, for some reason I don;t get emails about PRs here, and
> they're so rare I don't actively look.  Feel free to ping after, say, a week."


## 3. Prerequisites — none of §4 starts until all four are done

**P1 — Ask him.**  One message, before any code: here are 32 optional passes for an
SFPU backend you already maintain, all default-off, all with tests; would you rather
see them one PR per pass, or should we talk about the shape first?  His answer
reorders everything below and costs a day to get.

**P2 — Rebase onto `main`.**  The branch is 49 commits behind and **no longer
applies**.  `gimple-rvtt-expand.cc` was renamed to `gimple-rvtt-pred.cc` on
2026-09-08 (`ca890f5e6f6`, "Rename confusingly-named expand pass to vif"), so
the old plan's patch P06 targeted a file that does not exist.  `rvtt.md` has
*grown* to 2816 lines since the merge-base.  Rebase per pass as it is
submitted, not the whole branch at once.

**P3 — One green Development run.**  Push a topic branch to
`tenstorrent/sfpi`, dispatch the workflow, get a green `--test-tt`.  This
branch has never been built by CI: 36 runs ever, all `pin-review-lint`, each
8-12 seconds, the most recent 2026-09-18 from a workflow deleted 38 minutes
later.  Until one green run exists, we do not know that the series builds
against current `main`.

**P4 — Strip the false attributions.**  27 branch-new files carry
`Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org)`,
inherited from the file they were split out of.  His `rtl-rvtt-schedule.cc`
was 305 lines; the seven `rtl-rvtt-sched-*.cc` files now bearing his name
total 8,515.  `rvtt.md` went 2703 → 325 lines with fourteen topic files
carrying his header.  **Do not send him a patch signing his name to code he
has not seen.**  Retain the attribution only on the lines genuinely his; the
rest carry Tenstorrent copyright and no personal credit, which is already the
convention for all 128 other new files.


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
sentence of motivation.  No `Co-Authored-By` trailers — he uses none.

### The ordering principle

Waves are ordered by **how many of the 134 raced kernels the pass actually
reaches**, not by how easy it is to review.  An earlier revision of this plan
sorted by line count and led with `int-not` — a pass whose entire measured
benefit is one kernel row.  That is the worst possible opening: it makes a
year of work look like a bag of one-off hacks, which is exactly the charge the
generality census exists to answer.

Reach, for all 32 (kernels touched of 134, source lines):

```
 85  1860  dst-autoincr          10  1781  launch-flatten
 44  1828  invariant              8  1912  macro-planner
 40  1201  store-fold             7  1007  ccmask
 39   631  delivery-shape         6   954  dst-iteration
 28  1536  reassoc                5   948  prgm-const
                                  4   563  crossloop
                                  4  1902  crosscall
                                  3  1592  lut-select
   5 passes reach >= 20           8 passes reach 3-19      19 reach < 3
```

A "0" in that census means one of three different things and they must not be
conflated: the pass has **no flag at all** (`spill-diag`, `lreg-livein`,
`lp-alloc`, `crosslane-window`, `lp-schedule-prera` — always-on, so the census
has no row by construction); the pass's flag is **named differently** from the
pass (`replay-reform` is measured as `post-autoincr-window`, 13 kernels;
`macro-planner-residency` appears in the census as `planner-residency`); or the
pass genuinely **fires nowhere on this board** (`mop-form`, `round-interleave`).
Resolve which before using a zero as an argument.

### Wave A — the four that carry the result

| pass | kernels | loc | why here |
|---|---|---|---|
| `delivery-shape` | 39 | **631** | **Send this first.**  Real breadth *and* inside his observed 843-line ceiling.  It is also the project's thesis in one pass: choosing push vs launch vs record from a cost model rather than a heuristic. |
| `dst-autoincr` | **85** | 1860 | The widest pass on the board, 124 tests.  Over the ceiling, but it already splits — `rtl-rvtt-dst-autoincr-scan.cc` is a separate file — so it can go as a two-patch series if he prefers. |
| `invariant` | 44 | 1828 | Second-widest.  Needs a contract written first (§3, P5): 1828 lines behind a title line today. |
| `store-fold` | 40 | 1201 | Exemplary proof discipline already — every fold tied to a `tt/proofs/` artifact with a named standing refusal. |

If Wave A merges, the pattern is proven and the rest is throughput.  If it does
not, we have learned that on the passes that matter rather than on trivia.

### Wave B — real reach, needs a size or design conversation

`reassoc` (28) — the licensed FP pass; argue the double key on its own, not
buried in a series.  `launch-flatten` (10).  `macro-planner` (8, 245 tests,
fronting a 24-file subsystem — it needs to reference `docs/MACRO_PLANNER.md`
from its header before it goes).  `ccmask` (7).  `dst-iteration` (6).
`prgm-const` (5).  `crossloop` (4).  `crosscall` (4).  `lut-select` (3).

### Wave C — the narrow ones, sent as one batch and framed honestly

The 19 passes reaching fewer than three kernels, including `int-abs` and
`int-not` at exactly one each, both `birth_share 1.00`.  Their headers already
say it: *not claimed to generalise; claimed to be correct and free.*  Each is
exhaustively proven over 2^32 and each is inert with its flag off.  That is a
footnote to the story, not the opening — send them together, late, with the
census slide's own numbers attached so the narrowness is the project's
disclosure rather than a reviewer's discovery.

### Wave D — the nine that are not purely flag-gated

`spill-diag`, `lreg-livein`, `crosslane-window`, `lp-schedule-prera`,
`lp-alloc`, `reprprop`, `dst-interleave`, `replay-reform`, `replay-unroll`.
These change codegen without a flag to turn off, so the Wave A argument does
not cover them and they go last.  Three have 0 or 1 tests; write the tests
before proposing them.  `lreg-livein` in particular is 349 lines gated on
`TARGET_XTT_TENSIX` alone with one test in a 1530-test suite.

## 5. What is deliberately NOT in the series

**The `rvtt.md` split.**  The previous revision made this Stage 0a, "the
cheapest thing to land first".  It is the opposite: it moves 2378 of his lines
into 14 new files that then carry his name, destroys `git blame` on the file he
edits most, and collides with in-flight work.  It is the change most likely to
stop him reading.  Delete it from the plan; keep the split in our tree only.

**The deletion of `rtl-rvtt-hll.cc`** (1291 lines, "Originated by Paul
Keller", a GS memory-arbitration erratum workaround) and the five flags turned
into hard `error()`s.  Only two of the five pre-existed — `mtt-optimize-hll`
and `mtt-tensix-optimize-combine`, the latter `Init(1)`, i.e. **on by default**
at merge-base.  Retiring another engineer's erratum workaround and breaking an
on-by-default flag are product decisions the maintainer owns.  Raise both in
conversation; do not send them as patches.

**The three `Undocumented` deliberate-miscompile knobs** —
`-mtt-tensix-mve-expand-sabotage`, `-mtt-tensix-macro-planner-verify-corrupt-template`,
`-mtt-tensix-trips-oracle-skew=`.  Defensible as red/green harnesses, but
shipping intentional-miscompile switches in a production compiler is his call
and needs its own patch and its own argument.

**`gcc/system.h`** (+9: `INCLUDE_UNORDERED_MAP/SET/TUPLE`).  This is generic
GCC, not the backend.  Send it to `gcc-patches@gcc.gnu.org`, or drop the
dependency and use `hash_map`/`hash_set`, which is what upstream steers to.

**`--with-lp-solve`.**  Default off, never searched in the target sysroot, and
the vendored branch-and-bound solver is the primary backend with lp_solve as a
cross-check only.  Low risk, but an LGPL-2.1 dependency in `cc1plus` is a
licensing question only he can answer.  Its own patch, after Wave A.


## 6. Honest sizing

- **Wave A is a week** once P1-P4 are done, and P1-P4 are a few days.
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
