/* One vector-register pressure/liveness engine for Tensix (item #10).
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

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_RVTT_PRESSURE_H
#define GCC_RVTT_PRESSURE_H

/* GIMPLE-side vector-register (LREG) pressure, in one translation
   unit (FABLE_GOES_BURR.md item #10).  Three formerly hand-kept
   mirrors of the same conservative counting live here now:

   - the function-wide may-live model with per-point peaks (previously
     compute_lreg_pressure, gimple-rvtt-prgm-const.cc -- the promoted
     seed: GCC bitmaps over SSA_NAME_VERSION, fail-closed width
     handling);
   - the single-block conservative peak (previously
     rvtt_reassoc_bb_vec_pressure_peak, gimple-rvtt-reassoc.cc);
   - the loop-scoped candidate-set legality proof (previously
     rvtt_loop_lreg_pressure_legal_p, gimple-rvtt-invariant.cc),
     plus an incremental profile form of it so greedy selectors stop
     re-running the full proof per candidate.

   The three counting semantics are deliberately NOT merged: each
   query kind reproduces its historical verdict exactly (CLASS-I).
   What is unified is the infrastructure: the capacity constant, the
   width table, the tracked-value predicate, the CC-transient per-insn
   charges, and the LUT table-slot operand-class fact are each defined
   exactly once, in rvtt-pressure.cc.

   Conservatism contract (stage A): for one pin (50) every query
   recomputed its verdict under flag_checking with a verbatim copy of
   the retired mirror and asserted equality, with zero disagreements;
   the copies and asserts were deleted at pin 51.  */

/* Capacity of the allocatable vector-register file.  THE one place
   the engine reads it; every pressure/budget comparison must route
   through this.  */
extern unsigned rvtt_pressure_capacity ();

/* True when NAME is a vector SSA value that will occupy an
   allocatable LREG (constant-register-file reads are excluded).  */
extern bool rvtt_pressure_tracked_p (tree name);

/* Declared per-insn CC-transient LREG charges (the RTL-only
   temporaries CC lowering materializes at STMT's position).  */
extern unsigned rvtt_pressure_cc_transient (gimple *stmt);

/* Function-wide LREG pressure model: standard backward SSA liveness
   of pressure-tracked vector values, plus a per-block point-pressure
   maximum.  */
struct rvtt_pressure_model
{
  unsigned peak = 0;
  /* Per-BB (by index) live-in SSA-version bitmaps and the set of
     blocks whose point pressure exceeds the capacity.  */
  vec<bitmap> live_in = vNULL;
  bitmap over_bbs = nullptr;
  bitmap_obstack obstack;

  ~rvtt_pressure_model ()
  {
    live_in.release ();
    bitmap_obstack_release (&obstack);
  }
};

extern void rvtt_pressure_compute (function *fn, unsigned capacity,
				   rvtt_pressure_model *m);

/* Residual capacity of FN: capacity minus the function-wide peak
   (negative when the model exceeds the file).  */
extern int rvtt_pressure_residual (function *fn);

/* Conservative peak simultaneously-live SFPU vector SSA count across
   one block -- the pressure budget every reassociation site checks
   before adding live ranges.  */
extern unsigned rvtt_pressure_bb_peak (basic_block bb);

/* Peak point pressure over the window of points immediately before
   each statement after FIRST through LAST (same block, FIRST before
   LAST), counted with the function-wide may-live model's exact
   semantics (backward may-live fixpoint, tracked values, lreg_width,
   dead-def transients) -- NEW windowed vocabulary (laneKO/R3), for
   budgets whose added live range spans only that window (the licensed
   mad-restructure's kept loadi: the +1 applies pointwise only between
   the pair members, so charging a whole-block conservative peak would
   refuse every candidate in any block that merely TOUCHES the file's
   capacity somewhere else).  */
extern unsigned rvtt_pressure_window_peak (gimple *first, gimple *last);

/* Keeping every load in LOADS live across LOOP holds the loop's peak
   vector pressure within the architectural LREG file (conservative
   liveness proof; refusal is all-or-nothing for the given candidate
   set).  */
extern bool rvtt_pressure_loop_legal_p (class loop *loop,
					const auto_vec<gcall *> &loads,
					bool report = true,
					bool cc_transients = false,
					bool exempt_creg_reads = false);

/* Incremental residual-capacity query over one loop: the base
   pressure profile is computed once, and each candidate-set verdict
   is answered from it, verdict-identical to
   rvtt_pressure_loop_legal_p (loop, candidates, /-*report=*-/false,
   cc_transients, /-*exempt_creg_reads=*-/false) -- asserted under
   flag_checking.  Candidates must be loop-body definitions with every
   non-debug use inside the loop (the rvtt_invariant_constant_load_p
   vetting both greedy selectors already apply); the profile stays
   valid only while the loop body is unchanged (selection precedes
   transformation in both consumers).  */
class rvtt_loop_pressure
{
public:
  rvtt_loop_pressure (class loop *loop, bool cc_transients);

  /* Verdict for keeping every load in CANDIDATES live across the
     loop, from the precomputed profile (no full-body re-walk).  */
  bool legal_with (const auto_vec<gcall *> &candidates);

private:
  class loop *m_loop;
  bool m_cc_transients;
  /* Per-sample conservative counts: sample 0 is loop entry, then one
     sample per walked statement in dominance order.  */
  auto_vec<unsigned> m_base;
  /* Half-open [start, end) sample interval during which each name
     was counted live in the base walk (single-def SSA names enter and
     leave the live set at most once -- asserted while recording).  */
  struct live_interval { unsigned start, end; };
  hash_map<tree, live_interval> m_interval;
  /* Scratch for legal_with; kept to avoid re-allocation.  */
  auto_vec<int> m_delta;
};

#endif /* GCC_RVTT_PRESSURE_H */
