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

No — but close, and the shape is worth knowing.

Of the 42 pass translation units, **10 depend on nothing but the core**
(`rvtt.h`, `rvtt-protos.h`, `rvtt-insn.def`, `rvtt-effects.h`, `rvtt-refuse.h`,
`rvtt-raw-boundary.h`).  The other 32 pull in at least one further campaign
subsystem.  The core itself is unavoidably shared: `rvtt-refuse.h` is included
by 45 translation units, `rvtt-effects.h` by 40, `rvtt-raw-boundary.h` by 29.

So the unit of submission is **one foundation series, then one pass per
patch**, not one pass in isolation.

The ten foundation-only passes, with their dump-scan test counts:

| Pass | Lines | Tests |
|---|---|---|
| `rtl-rvtt-spill-diag.cc` | 212 | 2 |
| `gimple-rvtt-int-not.cc` | 285 | 7 |
| `rtl-rvtt-lreg-livein.cc` | 349 | 3 |
| `gimple-rvtt-int-abs.cc` | 576 | 11 |
| `gimple-rvtt-reprprop.cc` | 603 | 9 |
| `gimple-rvtt-expand.cc` | 834 | — |
| `gimple-rvtt-dst-iteration.cc` | 947 | 6 |
| `rtl-rvtt-dst-ownership.cc` | 1,015 | 13 |
| `rtl-rvtt-crosslane-window.cc` | 1,019 | 15 |
| `rtl-rvtt-lp-alloc.cc` | 3,094 | 25 |

Each of these, plus its `riscv.opt` flag, its `rvtt-passes.def` registration
and its tests, is a self-contained patch in the 300–1,000 line range.  That is
a reviewable size.  `rtl-rvtt-lp-alloc.cc` at 3,094 lines should be split
first.

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
