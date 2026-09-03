/* Pass to schedule tensix insns (insert nops)
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* ALGORITHM OVERVIEW

   Pipeline position
   -----------------
   pass_rvtt_schedule is registered in tt/rvtt-passes.def immediately
   before pass_postreload: register allocation has assigned hard LREGs,
   and the Tensix passes queued ahead of it there have already run --
   spill diagnosis, opcode synthesis, the generic macro planner (formed
   macro calendars appear here as effect-opaque barrier words), and the
   du-chain LREG rename (a broken storage collision widens this pass's
   fill candidate sets).  The replay former, the Dst auto-increment
   fold, and MOP formation run AFTER this pass and consume the
   scheduled stream, so every transform below preserves the shapes they
   recognize: unrolled row copies stay textually isomorphic, counted
   replay-capture shapes stay intact, and nothing reorders across an
   explicit replay owner.  The pass gates on TARGET_XTT_TENSIX, uses DF
   (hard-register reference sets, complete for allocatable registers
   after allocation), and never changes per-iteration semantics: every
   transform is a reorder, a proven register rename, or a NOP insertion
   required by a delay contract.

   What runs, in execute () order.  Each optimization is off by default
   behind its own -mtt-tensix-optimize-* flag; only the final NOP
   inserter is unconditional.

   1. Cross-row pairing (crossrow-pairing; sub-flags -stall-words,
      -seed, crossrow-shared-reload, mve-expand): pairs two consecutive
      iterations of an admitted capturable single-row Dst loop into one
      doubled row -- textual copy, Dst rebase by the typed row stride,
      doubled separator advance, halved countdown -- then interleaves
      the halves (CC atoms indivisible) so modeled dependency stalls
      fill while the counted replay-capture delivery shape survives.
      Acceptance: strict modeled steady-state II decrease plus pad-site
      and capture-budget belts; one transaction, exact restore.
   2. Region list scheduling (list_schedule_regions), four flags:
      . list-schedule: straight-line regions.  Maximal runs of audited
	pure-LREG words are collected between named barriers, a
	dependence DAG is built over DF hard-register references (RAW
	and WAW edges weighted by audited result latency, WAR by issue
	distance), and a deterministic critical-path list order is
	adopted only on a strict modeled makespan decrease with
	boundary terms from the entry producer and exit consumer.
      . round-interleave: lifts the self-loop and repeated-shape
	deferrals for two proven shapes -- a one-region self-loop row
	judged on the wrapped steady-state initiation interval, after a
	region-scoped storage-collision rename; and exactly two
	dataflow-isomorphic region copies scheduled under one shared
	permutation, transactionally as a pair.
      . cyclic-region-schedule: a self-loop row chopped into several
	regions by barrier words has each INTERIOR region rescheduled,
	judged on the whole row's cyclic II; rename-temporal routes
	collision roots through the shared rename service first.
      . ims: adds a Rau iterative-modulo-scheduling candidate order on
	both cyclic paths.  The engine is rvtt-timing.h's modulo tier:
	MII = max (ResMII, exact RecMII) over a dependence-distance
	graph produced by the same marshaller the acceptance simulator
	consumes, then budgeted-eviction placement in a single-issue
	modulo reservation table (-mtt-tensix-ims-budget=).  Modulo
	variable expansion is priced, never assumed (Lam, PLDI 1988):
	a kmin > 1 placement offers only its flat order unless the
	mve-expand arm inside the cross-row pairing realizes the
	kernel unroll with rotation renames.
   3. Latency scheduling (latency-schedule): fill_latency_bubbles
      moves the one instruction immediately behind an exposed one-slot
      result-latency bubble; fill_nop_shadows searches a bounded
      window for an independent filler for exactly the bubbles the NOP
      inserter would pad -- delay_nop_needed_p, the same probe,
      decides before and after, and the move is undone unless the
      bubble closed and no new pad site appeared.
   4. Interlock scheduling (interlock-schedule): fill_interlock_shadows
      fills the transparent scoreboard stalls modeled by the audited
      result-latency facts (no NOP word exists to remove), committing
      only on a strict modeled stall decrease over every adjacency the
      move changes; unaudited producers and targets refuse by name.
   5. Capture rotation (capture-rotation): a capturable self-loop row
      replays back-to-back, so its last word is issue-adjacent to the
      next playback's first.  Three movers close modeled stalls in that
      cycle: seam fill (plain reorder to the row tail or head),
      prologue rotation (an invariant-input filler moves forward past
      its own consumers, one prologue copy in the dedicated preheader
      covering the first trip), and interior gap fill.  The two
      plain-reorder movers use a widened filler pool (audited
      Dst-touching words, the typed row-step word) over provably inert
      crossings; the prologue mover keeps the pure-LREG pool.
   6. transform (): the always-on NOP inserter.  For every insn with a
      STATIC or DYNAMIC delay contract it walks the CFG forward to the
      next issued Tensix word and emits an SFPNOP when that word is
      dependent (DYNAMIC) or is not already a NOP (STATIC).
      Everything above exists to make this insert less often, or to
      hide the stalls it cannot express.

   The tail of the file is not part of execute (): it exports three
   emission services the macro planner calls.  Under drain-schedule,
   rvtt_macro_drain_boundary_elidable and
   rvtt_macro_drain_backedge_elidable prove a formed run's derived
   drain NOPs redundant at an intra-region run boundary or across a
   loop backedge; under window-pairing (with the -stride extension),
   rvtt_macro_interrow_drain_tuned derives the minimal inter-row drain
   from the descriptor's own decoded sequence delays with register-,
   Dst-row-, and sub-unit-exact conflict checks.

   Core data structures
   --------------------
   insn_regs	    DF-derived SFPU hard-register use/def sets.
   ls_node	    one schedulable word: insn, register sets, audited
		    latency, word count, original index, entry pin.
   ls_region	    a collected straight-line region: nodes, anchor,
		    entry producer, unaudited entry defs, signature.
   ls_rename	    one committed register-web rename, for exact undo.
   ls_ims_candidate an IMS order with its ResMII/RecMII/place-II/kmin.
   crp_loop	    an admitted pairable loop: row nodes, CC atoms,
		    load/store/separator/counter/jump, trip count, Dst
		    address and stride.
   crp_item	    an indivisible scheduling item (one CC atom or one
		    word) with aggregated register sets and latency.
   crp_shared_reload_info  the dedupe's value oracle: the shared
		    register and per-insn definition/consumer epochs.
   rotation_row     a capturable row: its issued words in order.
   drain_horizon    a run's decoded pending-event horizon: per-macro
		    carrier positions, event kinds and delays.
   wp_event	    one staged or follower event with its realized
		    footprint (LREGs, CC, config, typed Dst rows).

   Invariants and refusal discipline
   ---------------------------------
   Everything is fail-closed.  Unproven shapes refuse BY NAME through
   rvtt-refuse.h (rvtt_refuse, rvtt_refuse_by_name, and the composed
   form), leaving the stream byte-identical.  Commits are transactional
   against an exact-restore record (debug insns included) and are
   re-verified after the move: the NOP inserter's own pad-site probe
   must not count more sites, the entry producer's pad state must not
   flip on, pairing orders re-verify every dependence direction, and
   the shared-reload dedupe re-walks its value oracle independently.
   Every timing fact has one spelling: rvtt-timing.h owns
   audited_latency, adjacent_stall, classify_dependence, the makespan
   simulator, the cyclic-II model, and the modulo tier.  Register-file
   capacity is rvtt-pressure.h's rvtt_pressure_capacity; the capture
   and row-size budgets (XTT_DELIVERY_CAPTURE_SLOTS,
   XTT_CROSSROW_MIN_ROW_WORDS) come from the delivery-cost model
   (rvtt-cost.md); renames beyond the local region-scoped form route
   through the shared du-chain service (rvtt_lreg_rename_chain); and
   the drain services decode macro timing exclusively from the
   descriptor's own sequence words (rvtt-macro-sched.h,
   rvtt-macro-desc.h), never from hand constants.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "cfgrtl.h"
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "insn-constants.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rtl-rvtt-sched-int.h"

/* Perform instruction scheduling. We conditionally insert a nop after
   instructions.  */

static void
transform (function *fn)
{
  std::vector<basic_block> visited;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  if (GET_CODE (insn) != INSN)
	    continue;

	  if (recog_memoized (insn) < 0)
	    continue;

	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;

	  enum xtt_delay delay = get_attr_xtt_delay (insn);
	  if (delay == XTT_DELAY_NONE)
	    continue;

	  visited.reserve (n_basic_blocks_for_fn (fn));
	  bool insert = delay_nop_needed_p (visited, bb, insn, delay);

	  if (insert)
	    for (unsigned nops = rvtt_delay_bubbles (insn); nops; --nops)
	      emit_insn_after (gen_rvtt_sfpnop (), insn);
	  if (dump_file)
	    {
	      fprintf (dump_file, "%snserting %s nop after ",
		       insert ? "I" : "Not i",
		       delay == XTT_DELAY_STATIC ? "static" : "dynamic");
	      dump_insn_slim (dump_file, insn);
	      fprintf (dump_file, "\n");
	    }
       }
    }
}

namespace {

const pass_data pass_data_rvtt_schedule =
{
  RTL_PASS, /* type */
  "rvtt_schedule", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_schedule : public rtl_opt_pass
{
public:
  pass_rvtt_schedule (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_schedule, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    if (riscv_tt_opt_crossrow_pairing)
      /* Before the region schedulers: a committed pairing leaves a
	 doubled self-loop row whose pure spans the later phases may
	 still improve; a refusal leaves the stream byte-identical.  */
      crossrow_pair_rows (fn);
    if (riscv_tt_opt_list_schedule || riscv_tt_opt_round_interleave
	|| riscv_tt_opt_cyclic_region_schedule || riscv_tt_opt_ims)
      /* The round-interleave flag enables only the cyclic self-loop
	 and isomorphic-pair extensions inside; the cyclic-interior
	 flag only the multi-region self-loop extension; the ims flag
	 only its candidate orders on those two cyclic paths; single
	 straight-line regions still require the list-schedule flag.  */
      list_schedule_regions (fn);
    if (riscv_tt_opt_latency_schedule)
      {
	fill_latency_bubbles (fn);
	fill_nop_shadows (fn);
      }
    if (riscv_tt_opt_interlock_schedule)
      fill_interlock_shadows (fn);
    if (riscv_tt_opt_capture_rotation)
      rotate_capture_rows (fn);
    transform (fn);
    return 0;
  }
}; /* class pass_rvtt_schedule */

} /* anon namespace */

/* Instantiate the pass for CTXT.  Registered in tt/rvtt-passes.def
   immediately before pass_postreload; see the overview at the top of
   this file for the pipeline placement rationale.  */

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}
