<!-- Copyright (C) 2026 Tenstorrent Inc.

     This file is part of GCC.

     GCC is free software; you can redistribute it and/or modify it under
     the terms of the GNU General Public License as published by the Free
     Software Foundation; either version 3, or (at your option) any later
     version.

     GCC is distributed in the hope that it will be useful, but WITHOUT ANY
     WARRANTY; without even the implied warranty of MERCHANTABILITY or
     FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
     for more details.

     You should have received a copy of the GNU General Public License
     along with GCC; see the file COPYING3.  If not see
     <http://www.gnu.org/licenses/>.  -->

# Getting this branch into reviewable shape

Measured 2026-09-19 against `origin/main`.  Every number here came from a
command, not an estimate; the command is named so it can be re-run.

## 1. Why the branch cannot be submitted as it stands

    git rev-list --count origin/main..HEAD        552 commits ahead
    git rev-list --count HEAD..origin/main         36 commits behind
    git diff --shortstat origin/main...HEAD      1700 files, +167232 -5524

Broken down:

| Area | Files | Insertions | New files |
|---|---|---|---|
| `gcc/config/riscv/tt` | 177 | 107,662 | 146 |
| `gcc/testsuite/g++.target/riscv/tt` | 1,491 | 49,566 | 1,481 |
| `gcc/docs` | 19 | 6,984 | 19 |
| `riscv.opt` / `riscv.cc` / `riscv-ftypes.def` | 3 | 491 | 0 |

No maintainer reviews 167k insertions in one request.  **Size is the blocker,
and nothing else on this page matters until it is fixed.**  The branch is also
36 commits behind, so it would not apply cleanly either.

## 2. The two mechanical gates GCC already ships

Both live in this tree and are what gcc.gnu.org runs.  Neither currently
passes.

**ChangeLog.** `contrib/gcc-changelog/git_check_commit.py <sha>` fails on every
commit on this branch, including the recent cleanup ones, with `changed file
not mentioned in a ChangeLog`.  Upstream this is an automatic bounce with no
human involved.  The accepted form is a trailer in the commit message itself;
this exact shape was validated against the checker and returns `OK`:

    rvtt: one-line summary

    Free prose explaining the change.

    gcc/ChangeLog:

    	* config/riscv/tt/rvtt-effects.cc (rvtt_call_int_arg): New function.
    	* config/riscv/tt/rvtt-effects.h (rvtt_call_int_arg): Declare.

Note the literal tab before `*`, the blank line after `gcc/ChangeLog:`, and
that every touched file must appear.  Test files go under a
`gcc/testsuite/ChangeLog:` heading.

**GNU style.** `contrib/check_GNU_style.py <patch>` reports 2,449 findings over
the 100 campaign-added `.cc`/`.h` files.  That number is badly inflated and
should not be quoted without the breakdown, because the checker treats comment
prose as code:

| Class | Raw | Assessment |
|---|---|---|
| dot, space, space, end of comment | 1,204 | real, trivial (`/* as given */` wants `/* As given.  */`) |
| trailing operator | 351 | mostly false: em-dashes ending comment lines |
| 8 spaces should be tabs | 357 | mostly false: comment ASCII diagrams |
| function name / paren spacing | 141 | **partly real** — see below |
| lines over 80 characters | 144 | real, but only 74 in the files themselves |
| space before `[` | 105 | mostly false: `[per-arch]`, `[region exit]` in comments |
| braces on a separate line | 50 | mostly false: `: gimple_opt_pass (...) {}` is GCC idiom |

A further 405 findings land in the 32 `tt/` files shared with `origin/main`;
those are upstream's and are not ours to change.

Counting only real code lines, the genuine work is small and concentrated:

    over-80-column lines, whole backend            74
      of which gimple-rvtt-expand.cc               34
      of which gimple-rvtt-dst-iteration.cc        17
    missing space before '(' on code lines        214
      of which gimple-rvtt-expand.cc              131

`gimple-rvtt-expand.cc` is the outlier: it was written in a non-GNU style and
accounts for most of both counts on its own.

## 3. Can each pass be its own submission?

Yes — as a stack, in dependency order.  Each pass ends up as one patch
containing the pass, its `riscv.opt` flag, its `rvtt-passes.def` registration
and its tests.  What follows is not a limit on how many passes can be
reviewed separately; it is only the order they have to go in.

First, a correction to an earlier count.  There are 42 files matching
`{gimple,rtl}-rvtt-*.cc`, but they are not 42 passes:

    registered-pass translation units (define make_pass_*)     28
    support translation units (share the prefix, register none) 14

The 14 support files -- `rtl-rvtt-replay-crf.cc`, `rtl-rvtt-sched-pairing.cc`,
`gimple-rvtt-prgm-residency.cc` and the rest -- are library code for a
subsystem and belong in that subsystem's patch, not in one of their own.

The core is unavoidably shared, so it cannot be deferred: `rvtt-refuse.h` is
included by 45 translation units, `rvtt-effects.h` by 40,
`rvtt-raw-boundary.h` by 29.  Ten of the 28 passes need nothing beyond it.
The rest each need one further subsystem header, which lands immediately
before the passes it unblocks.

### Readiness of the 28

Size, which decides whether a reviewer can hold the patch in their head:

    under 500 lines      3
    500 - 1000          8
    1000 - 2000        12
    over 2000           5

The 23 under 2000 lines are already a normal size for a new GCC pass carrying
its own tests and file essay.  Five are not, and should be split before they
are sent:

    rtl-rvtt-macro-planner.cc   3462
    gimple-rvtt-crosscall.cc    3186
    rtl-rvtt-lp-alloc.cc        3094
    rtl-rvtt-dst-autoincr.cc    2763
    rtl-rvtt-mop-form.cc        2208

Tests: 25 of the 28 carry dump-scan tests keyed to their dump name.  Three do
not and need them written before submission -- `gimple-rvtt-expand.cc`,
`rtl-rvtt-lreg-livein.cc`, `rtl-rvtt-spill-diag.cc`.

### Does any of this need re-measuring on silicon?

No -- and the reason matters, because it is cheaper as well as stronger.

Everything in this plan is reorganisation: splitting files, moving shared
helpers, restyling.  None of it is intended to change what the compiler emits.
For a change like that, byte-identity of the emitted code is a *stronger*
result than a timing run: if the bytes are identical the cycle count on the
device is identical by construction, whereas a perf sweep re-measures the same
binaries and adds measurement noise.  The identity gate takes minutes; the
sweep takes hours.

Silicon becomes necessary only when a reorganisation *does* move codegen -- and
the identity gate is precisely what reports that.  So the gate is the trigger
for a silicon run, not a substitute for one.  Board performance numbers are a
separate concern from upstreaming in any case: review turns on correctness and
structure, not on cycle counts.

## 4. Proposed order

**Stage 0 — reorganisation only, no new functionality.**  These change nothing
the compiler emits and can be justified on their own merits.

  0a. Split `rvtt.md`.  Upstream's is a 2,481-line monolith; this branch
      already splits it into 14 topic files (`+103 -2481`) with codegen
      unchanged.  This is pure "make it not messy" and is the cheapest thing
      to land first.
  0b. The backend map, `gcc/config/riscv/tt/README`: what the passes are for,
      where they run, which flags are on, how refusals work.
  0c. Style conformance on the files the later stages touch.

**Stage 1 — the shared core** (~4,650 lines of campaign additions):

  - `rvtt-effects.{h,cc}` — the typed effect-classification vocabulary, the
    single place instruction effects are decided.
  - `rvtt-refuse.{h,cc}` + `rvtt-refusals.def` — the named-refusal registry, so
    "the pass declined" is never silent.
  - `rvtt-raw-boundary.{h,cc}`.
  - The `riscv.opt` / `riscv.cc` / `riscv-ftypes.def` hooks.

This stage is large for one patch and should itself be split — effects first,
refusals second, since the second uses the first.

**Stage 2 — the ten foundation-only passes**, smallest first, one patch each.

**Stage 3 — the remaining 32 passes**, in dependency order.  The subsystem
clusters (macro planner, scheduling, replay formation, LREG pressure) should
land as their own series, each preceded by its shared header.

## 5. Per-patch checklist

    1.  Rebase onto current origin/main.
    2.  ChangeLog trailer in the commit message; verify:
          contrib/gcc-changelog/git_check_commit.py <sha>     ->  OK
    3.  Style:
          contrib/check_GNU_style.py <patch>
        Judge each finding; the classes in section 2 are frequently false on
        comment prose.  Do not bulk-apply a fix script — one was tried here and
        it turned comment separator rules into "------.  */", capitalised a
        filename, and appended a period to an #endif guard label.
    4.  Codegen unchanged, for any patch that claims to be a refactor:
        build the patched compiler and a baseline from the unmodified tree in
        the same build directory at the same stage, compile the in-tree Tensix
        corpus with both, and diff every emitted artifact.  Scripts on
        tt-quietbox-0: setup-sfpi-verify.sh, build-base-cc1plus.sh,
        corpus-identity.sh.  Normalise addresses before judging RTL dumps —
        they print heap pointers, so ASLR alone yields ~112 spurious diffs.
    5.  Tests accompany the pass in the same patch.

## 6. What is already in reasonable shape

- Per-file essays explaining what each pass does and why.
- The backend README: pass families, pipeline anchors and their ordering trap,
  the flag split, the refusal mechanism, a glossary.
- Duplicate helpers: 29 definitions across 20 passes reduced to 11, each step
  verified byte-identical on 1,543 sources.
- The named refusal registry, build-enforced against duplicate or unregistered
  names.
- Documentation claims checked against the build rather than asserted — see
  the README's section 7, which now states that two of the eleven proofs are
  mechanically verified rather than implying all of them are.

## 7. The stack, concretely

Generated from the include graph; re-runnable.  Each `Pnn` is one patch: the
pass, its `riscv.opt` flag, its `rvtt-passes.def` registration, its tests.
Each `+ header` line is an infrastructure patch that must land before the
passes under it.  Support translation units travel with their subsystem.

```
  --- after the core, passes needing nothing further ---
  P01  rtl-rvtt-spill-diag.cc               212 lines    0 tests  NEEDS TESTS
  P02  gimple-rvtt-int-not.cc               285 lines    7 tests
  P03  rtl-rvtt-lreg-livein.cc              349 lines    0 tests  NEEDS TESTS
  P04  gimple-rvtt-int-abs.cc               576 lines   11 tests
  P05  gimple-rvtt-reprprop.cc              603 lines    8 tests
  P06  gimple-rvtt-expand.cc                847 lines    0 tests  NEEDS TESTS
  P07  gimple-rvtt-dst-iteration.cc         947 lines    7 tests
  P08  rtl-rvtt-dst-ownership.cc           1015 lines   22 tests
  P09  rtl-rvtt-crosslane-window.cc        1019 lines    1 tests
  P10  rtl-rvtt-lp-alloc.cc                3094 lines   25 tests  SPLIT FIRST

  --- + rvtt-macro-ownership.h (512 lines) ---
  P11  gimple-rvtt-crossloop.cc             532 lines   22 tests
  P12  rtl-rvtt-dst-autoincr.cc            2763 lines  103 tests  SPLIT FIRST

  --- + rvtt-macro-tables.h (1675 lines) ---
  P13  gimple-rvtt-transp-involution.cc    1205 lines    3 tests
  P14  gimple-rvtt-crosslane.cc            1813 lines   15 tests

  --- + rvtt-cc-region.h (1532 lines) ---
  P15  gimple-rvtt-store-fold.cc           1183 lines   30 tests
  P16  rtl-rvtt-lreg-rename.cc             1963 lines   30 tests

  --- + rvtt-trips.h (671 lines) ---
  P17  gimple-rvtt-replay-unroll.cc        1781 lines   16 tests

  --- + rvtt-lut-tables.h (461 lines) ---
  P18  gimple-rvtt-lut-select.cc           1591 lines   52 tests

  --- + rtl-rvtt-sched-int.h (115 lines) ---
  P19  rtl-rvtt-lp-schedule-prera.cc       1349 lines   11 tests

  --- + rvtt-delivery-cost.h (216 lines) ---
  P20  gimple-rvtt-ccmask.cc                952 lines   22 tests
  P21  gimple-rvtt-reassoc.cc              1536 lines   24 tests
  P22  rtl-rvtt-mop-form.cc                2208 lines   14 tests  SPLIT FIRST
  P23  gimple-rvtt-crosscall.cc            3186 lines   30 tests  SPLIT FIRST
  P24  rtl-rvtt-macro-planner.cc           3462 lines  186 tests  SPLIT FIRST

  --- + rvtt-placement.h (438 lines) ---
  P25  gimple-rvtt-prgm-const.cc            948 lines  144 tests
  P26  gimple-rvtt-invariant.cc            1827 lines   89 tests

  --- + rvtt-schedule.h (245 lines) ---
  P27  gimple-rvtt-delivery-shape.cc        595 lines   17 tests
  P28  gimple-rvtt-lp-schedule.cc          1110 lines   37 tests

  28 of 28 registered passes placed
```

Five are flagged SPLIT FIRST at over 2000 lines, and three need tests written.
The other 20 are submission-shaped as they stand.
