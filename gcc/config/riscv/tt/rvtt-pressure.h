/* One vector-register pressure/liveness engine for Tensix.
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

/* GIMPLE-side vector-register (LREG) pressure queries:

   - the function-wide may-live model with per-point peaks;
   - the single-block conservative peak;
   - the windowed peak between two statements of one block;
   - the loop-scoped candidate-set legality check, plus an incremental
     form of it so greedy selectors need not re-run the full check per
     candidate.

   The counting semantics of these queries differ on purpose: each is
   conservative in the way its callers need.  The capacity, the width
   table, the tracked-value predicate and the CC-transient charges are
   shared and defined in rvtt-pressure.cc.  */

/* Capacity of the allocatable vector-register file.  All pressure and
   budget comparisons use this.  */
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

/* Conservative peak count of simultaneously live SFPU vector SSA names
   in block BB.  */
extern unsigned rvtt_pressure_bb_peak (basic_block bb);

/* Peak point pressure over the window of points immediately before each
   statement after FIRST through LAST (same block, FIRST before LAST),
   counted with the function-wide may-live model's semantics (backward
   may-live fixpoint, tracked values, lreg_width, dead-def transients).  Used
   by transformations whose added live range spans only that window, where
   the whole-block peak would be needlessly conservative.  */
extern unsigned rvtt_pressure_window_peak (gimple *first, gimple *last);

/* Return true if keeping every load in LOADS live across LOOP keeps the
   loop's peak vector pressure within the LREG file.  The answer is for
   the whole candidate set.  */
extern bool rvtt_pressure_loop_legal_p (class loop *loop,
					const auto_vec<gcall *> &loads,
					bool report = true,
					bool cc_transients = false,
					bool exempt_creg_reads = false);

/* Incremental residual-capacity query over one loop.  The base pressure
   profile is computed once and each candidate set is answered from it,
   with the same result as rvtt_pressure_loop_legal_p (loop, candidates,
   /-*report=*-/false, cc_transients, /-*exempt_creg_reads=*-/false); this
   is checked under flag_checking.  Candidates must be loop-body
   definitions with every non-debug use inside the loop (as checked by
   rvtt_invariant_constant_load_p).  The profile is valid only while the
   loop body is unchanged.  */
class rvtt_loop_pressure
{
public:
  rvtt_loop_pressure (class loop *loop, bool cc_transients);

  /* Return true if every load in CANDIDATES can stay live across the
     loop, using the precomputed profile.  */
  bool legal_with (const auto_vec<gcall *> &candidates);

private:
  class loop *m_loop;
  bool m_cc_transients;
  /* Per-sample conservative counts: sample 0 is loop entry, then one
     sample per walked statement in dominance order.  */
  auto_vec<unsigned> m_base;
  /* Half-open [start, end) sample interval during which each name
     was counted live in the base walk (a single-def SSA name enters
     and leaves the live set at most once).  */
  struct live_interval { unsigned start, end; };
  hash_map<tree, live_interval> m_interval;
  /* Scratch for legal_with; kept to avoid re-allocation.  */
  auto_vec<int> m_delta;
};

#endif /* GCC_RVTT_PRESSURE_H */
