/* PRGM constant programming: constant residency
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

/* The constant-residency transform of the PRGM constant pass
   (-mtt-tensix-optimize-const-residency): loop-invariant constant
   images move from per-iteration materialization into claimed PRGM
   registers, with first-iteration peeling, the CC-block
   classification, and the merge-rename admission.  Split from
   gimple-rvtt-prgm-const.cc; the algorithm essay lives there.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "fold-const.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-into-ssa.h"
#include "tree-ssa-operands.h"
#include "tree-ssanames.h"
#include "cfghooks.h"
#include "cfgloop.h"
#include "tree-ssa-loop-niter.h"
#include "tree-scalar-evolution.h"
#include "cfganal.h"
#include "tree-cfg.h"
#include "dominance.h"
#include "cgraph.h"
#include "stringpool.h"
#include "attribs.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-placement.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-mop-tables.h"
#include "rvtt-mop-derive.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-ipa-summary.h"
#include "gimple-rvtt-prgm-int.h"

/* ------------------------------------------------------------------ */
/* Residency allocation: park proven-constant values in free
   PRGM registers by priced selection.  Class LOOP: an in-loop
   invariant constant materialization is programmed once on the loop
   entry edge (saves two pushed SFPLOADI words per proven iteration for
   a one-time three-word cost, refusing only a proven single trip:
   rvtt-cost.md delivery model).  Class PRESSURE: under LREG
   over-pressure, an out-of-loop proven-constant value is reprogrammed
   in place -- the constant register read occupies no allocatable LREG,
   which is the cheapest relief tier (ahead of rematerialization).  */

struct residency_candidate
{
  gcall *load;			/* the single-issue materialization */
  unsigned value;		/* full 32-bit lane image */
  class loop *loop;		/* LOOP class: the enclosing loop */
  edge entry;			/* LOOP class: its entry edge */
  unsigned uses;		/* non-debug uses (the ranking key) */
  bool peel = false;		/* LOOP class: CC-canonical body; the
				   programming point is created by a
				   first-iteration peel at placement */
  bool cc_lifted = false;	/* LOOP class under
				   -mtt-tensix-optimize-crossloop-cc-peel:
				   a peel-class candidate whose
				   programming lifted across the
				   enclosing loops as a programming-only
				   placement -- ENTRY is the lifted edge,
				   proven at discovery (cc-immaterial
				   region walk + no CC write reaches the
				   lifted preheader), and no peel is
				   created */
  bool inplace = false;		/* MAD-PAIR class: the programming point
				   is the hoisted materialization's own
				   position (outside the loop), exactly
				   the pressure class's in-place
				   discipline */
  unsigned group = 0;		/* MAD-PAIR class: pair-atomic admission
				   key -- every fold-vulnerable constant
				   of one mul+add pair claims together
				   or not at all (a half-claimed pair
				   leaves one immediate fold live and
				   the mad rule still blocked: a pure
				   loss) */
  bool hoisted_reuse = false;	/* HOISTED-REUSE class under
				   -mtt-tensix-optimize-hoisted-prgm-reuse:
				   a preheader-hoisted loop-invariant
				   materialization re-claims a PRGM slot
				   (free, or TU value-identical) to
				   release its loop-wide LREG live
				   range; in-place programming point,
				   the pressure class's discipline */
  bool call_free_window = false; /* HOISTED-REUSE: the materialization
				   sits in the loop's entry block with
				   no foreign call or asm between it
				   and the loop's in-loop readers --
				   the window a DEAD-claim reclaim
				   (heterogeneous value into an unread
				   claimed slot) needs so no callee can
				   reprogram the slot between this
				   function's own programming and its
				   reads.  Value-identical reuse never
				   needs it (idempotence).  */
};

/* Fold VAL through the in-loop constant chain from a header PHI to OP:
   single-use assign statements whose other operands are invariant.
   Returns the folded value of OP on the first iteration, or NULL_TREE.
   (The same bounded-evaluation idea as the invariant pass's
   short-constant-loop proof, needed here because neither
   scalar-evolution nor loop_niter_by_eval is usable at this pipeline
   position: both assert canonical loop state -- LOOPS_NORMAL
   preheaders -- that AVOID_CFG_MODIFICATIONS deliberately does not
   establish.)  */

static tree
first_iteration_value (class loop *loop, edge entry, tree op)
{
  if (is_gimple_min_invariant (op))
    return op;
  /* Walk the definition chain backwards to a header PHI, then fold
     forward from the entry value.  The depth bound is proof work, not
     semantics (an unproven chain refuses; same discipline as the
     invariant pass's bounded exit-test evaluation): the rotated
     counted-loop exit tests this proof targets are one or two
     statements deep.  */
  auto_vec<gimple *, 8> chain;
  tree cur = op;
  for (unsigned depth = 0; depth != 8; ++depth)
    {
      if (TREE_CODE (cur) != SSA_NAME)
	return NULL_TREE;
      gimple *def = SSA_NAME_DEF_STMT (cur);
      if (gphi *phi = dyn_cast <gphi *> (def))
	{
	  if (gimple_bb (phi) != loop->header)
	    return NULL_TREE;
	  tree value = PHI_ARG_DEF_FROM_EDGE (phi, entry);
	  if (!is_gimple_min_invariant (value))
	    return NULL_TREE;
	  /* Fold the chain forward, substituting VALUE for each
	     statement's single non-invariant operand.  */
	  for (int ix = chain.length () - 1; ix >= 0; --ix)
	    {
	      gassign *a = as_a <gassign *> (chain[ix]);
	      tree_code code = gimple_assign_rhs_code (a);
	      tree type = TREE_TYPE (gimple_assign_lhs (a));
	      tree op1 = gimple_assign_rhs1 (a);
	      tree op2 = gimple_num_ops (a) > 2
		? gimple_assign_rhs2 (a) : NULL_TREE;
	      if (!is_gimple_min_invariant (op1))
		op1 = value;
	      if (op2 && !is_gimple_min_invariant (op2))
		op2 = value;
	      switch (get_gimple_rhs_class (code))
		{
		case GIMPLE_SINGLE_RHS:
		  value = op1;
		  break;
		case GIMPLE_UNARY_RHS:
		  value = fold_unary (code, type, op1);
		  break;
		case GIMPLE_BINARY_RHS:
		  value = fold_binary (code, type, op1, op2);
		  break;
		default:
		  return NULL_TREE;
		}
	      if (!value || !is_gimple_min_invariant (value))
		return NULL_TREE;
	    }
	  return value;
	}
      gassign *assign = dyn_cast <gassign *> (def);
      if (!assign || !gimple_bb (assign)
	  || !flow_bb_inside_loop_p (loop, gimple_bb (assign)))
	return NULL_TREE;
      /* Exactly one non-invariant operand continues the chain.  */
      tree next = NULL_TREE;
      for (unsigned i = 1; i < gimple_num_ops (assign); ++i)
	{
	  tree o = gimple_op (assign, i);
	  if (!o || is_gimple_min_invariant (o))
	    continue;
	  if (next)
	    return NULL_TREE;
	  next = o;
	}
      if (!next)
	return NULL_TREE;
      chain.safe_push (assign);
      cur = next;
    }
  return NULL_TREE;
}

/* Classify the loop's trip count for the LOOP-class break-even by
   evaluating the single exit test on the first iteration.

   The classification prices; it never licenses.  Correctness of the
   LOOP class is trip-independent: the programming point sits on the
   never-speculated entry edge of the rotated loop (control reaching it
   executes the header at least once), the programmed register is
   established there before any replaced use, and nothing in the
   admitted loop body clobbers it (the sfpu-barrier/opaque gates refuse
   bodies with foreign effects) -- so residency holds on every entered
   iteration whatever the trip count.  What the bounded first-iteration
   evaluation decides is only which side of the two-trip break-even the
   loop is on:

   TRIPS_AT_LEAST_2 -- the exit test provably stays in the loop after
   the first trip: the two-trip break-even is proven and the programming
   strictly pays.
   TRIPS_PROVEN_SINGLE -- the exit test provably leaves the loop after
   the first trip: the one-time programming can never recover its cost;
   the candidate refuses by name (a proven loss).  Defensive: this
   evaluator folds a strict subset of what scalar evolution folds, so a
   constant single-trip loop has normally been flattened by complete
   unrolling long before this pass; the branch keeps the pricing
   fail-closed rather than relying on that pipeline fact.
   TRIPS_UNKNOWN -- the test does not fold (runtime trip counts, the
   dominant LLK loop shape): admitted.  The worst case is a single-trip
   entry costing one extra pushed word per candidate (programming is
   W+1 words against the W it saves on the executed iteration,
   rvtt-cost.md delivery model); every second trip onward is pure
   saving.  (The CC-canonical peel class is different: its peel
   DUPLICATES a body unconditionally, so it genuinely needs proven
   trips and keeps its own named refusal.)  */

enum loop_trip_class
{
  TRIPS_AT_LEAST_2,
  TRIPS_PROVEN_SINGLE,
  TRIPS_UNKNOWN
};

/* Classify LOOP against the two-trip break-even (see the comment
   above the enum): fold its single exit test with every operand
   replaced by its first-iteration value as seen through ENTRY.  A
   loop with multiple exits, a non-GIMPLE_COND exit, or an unfoldable
   test answers TRIPS_UNKNOWN.  */

static loop_trip_class
classify_second_trip (class loop *loop, edge entry)
{
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1)
    return TRIPS_UNKNOWN;
  edge exit = exits[0];
  gimple_stmt_iterator last = gsi_last_bb (exit->src);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond)
    return TRIPS_UNKNOWN;
  tree lhs = first_iteration_value (loop, entry, gimple_cond_lhs (cond));
  tree rhs = first_iteration_value (loop, entry, gimple_cond_rhs (cond));
  if (!lhs || !rhs)
    return TRIPS_UNKNOWN;
  tree test = fold_binary (gimple_cond_code (cond), boolean_type_node,
			   lhs, rhs);
  if (!test || TREE_CODE (test) != INTEGER_CST)
    return TRIPS_UNKNOWN;
  edge true_edge, false_edge;
  extract_true_false_edges_from_block (exit->src, &true_edge, &false_edge);
  edge taken = integer_zerop (test) ? false_edge : true_edge;
  if (!taken)
    return TRIPS_UNKNOWN;
  return taken == exit ? TRIPS_PROVEN_SINGLE : TRIPS_AT_LEAST_2;
}

/* Number of non-debug statements using the SSA name NAME.  Counts
   statements, not operands: a statement reading NAME twice counts
   once.  */

static unsigned
count_nondebug_uses (tree name)
{
  unsigned n = 0;
  imm_use_iterator iter;
  gimple *use;
  FOR_EACH_IMM_USE_STMT (use, iter, name)
    if (!is_gimple_debug (use))
      ++n;
  return n;
}

/* ------------------------------------------------------------------ */
/* CC-canonical loops: first-iteration peel.

   The LOOP class above requires a CC-write-free loop (sfpu-barrier)
   and a CC-write-free function (cc-region-unproven), because its
   entry-edge programming executes under the loop-entry lane state and
   every replaced in-loop materialization must have executed under that
   SAME state.  Freshly authored kernel bodies routinely violate
   both: their row loop carries a lowered v_if region
   (SFPSETCC/SFPXFCMP* ... all-lanes SFPENCC) and re-materializes the
   loop-invariant paired-SFPLOADI constants every row -- the exact
   structural gap a replay-slot census established (log 23 vs 17 replay slots,
   sqrt 27 vs 21, rsqrt 33 vs 25 crossing the 32-slot replay cliff).

   For the CC-canonical single-block body (rvtt_loop_cc_canonical_body:
   the LAST CC writer on the unique linear path is the word-exact
   all-lanes SFPENCC), a first-iteration PEEL makes the residency
   transformation exact without any ambient lane-state assumption:

   - iteration one is duplicated statement for statement onto the entry
     edge (same statements, same order, same operand values), so its
     behavior -- including under an arbitrary unknown ambient CC mask --
     is reproduced bit for bit, and its trailing all-lanes SFPENCC
     leaves the machine in the architectural all-lanes state;
   - the staging SFPLOADI + SFPCONFIG programming is appended AFTER the
     peeled copy: it executes exactly when the loop continues past
     iteration one, in the proven all-lanes state (satisfying the
     architectural all-lanes requirement on SFPCONFIG: the reference
     simulator's TENSIX_EXECUTE_SFPCONFIG verifies every lane enabled,
     and its lanewise LReg[0][lane & 7] copy needs the staged constant
     present in ALL of L0's lanes);
   - every remaining iteration k >= 2 begins in that same all-lanes
     state (the body's last CC writer is the all-lanes SFPENCC), so a
     candidate materialization placed BEFORE the body's first CC writer
     executed all-lanes there -- writing every lane with the constant --
     and the constant-register read that replaces it yields the
     identical value in every lane.  Iterations 2..N are therefore
     bit-exact as well.

   Profitability (rvtt-cost.md, residency-peel model): the peel
   re-delivers one body as RISC-pushed words (a delivery-class change
   worth (PUSH - SLOT) per word against the replayed loop it came from)
   and the programming costs PUSH per staged word and per SFPCONFIG;
   the loop saves the candidates' materialization words every remaining
   iteration.  The required trip count is proven by bounded forward
   evaluation of the rotated loop's own scalar control -- never assumed
   from profile data.  */

/* Post-shortening issue words of one admitted materialization: the
   shared exported encoding model (gimple-rvtt-invariant.cc, also the
   crossloop pass's ranking key).  This was a verbatim local copy until
   the FH audit (FHI-T2) -- one vocabulary, one drift surface.  */

static unsigned
loadi_issue_words (gcall *call)
{
  return rvtt_sfpxloadi_materialization_cost (call);
}

/* Prove LOOP's body executes at least NEED times, by bounded forward
   evaluation of the single-block body's scalar control from the entry
   values (the same discipline as loop_second_trip_proven_p and the
   invariant pass's short-constant-loop proof; scalar evolution is
   unusable at this pipeline position).  Statements that do not fold
   simply leave their results unknown; the proof fails -- refusing --
   only when the exit test itself does not fold to a constant, when a
   header PHI's next value is unknown, or when the loop provably exits
   before NEED iterations.  */

static bool
loop_trips_at_least_p (class loop *loop, edge entry, unsigned need)
{
  if (need <= 1)
    return true;

  basic_block bb = loop->header;
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  if (exits.length () != 1 || exits[0]->src != bb)
    return false;
  edge exit = exits[0];
  edge latch_e = loop_latch_edge (loop);
  gimple_stmt_iterator last = gsi_last_bb (bb);
  gcond *cond = gsi_end_p (last) ? nullptr
    : dyn_cast <gcond *> (gsi_stmt (last));
  if (!cond || !latch_e)
    return false;

  edge true_edge, false_edge;
  extract_true_false_edges_from_block (bb, &true_edge, &false_edge);
  if (!true_edge || !false_edge)
    return false;

  /* Current values of the header PHIs (and, within an iteration, of
     folded body definitions).  A PHI whose entry value does not fold
     (e.g. a loop-carried vector) simply stays unknown; the proof fails
     only when the exit test itself needs an unknown value.  */
  hash_map<tree, tree> vals;
  auto_vec<gphi *, 4> phis;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (virtual_operand_p (res))
	continue;
      tree init = PHI_ARG_DEF_FROM_EDGE (phi, entry);
      if (!is_gimple_min_invariant (init))
	continue;
      phis.safe_push (phi);
      vals.put (res, init);
    }

  auto lookup = [&vals] (tree op) -> tree
    {
      if (!op || is_gimple_min_invariant (op))
	return op;
      if (TREE_CODE (op) != SSA_NAME)
	return NULL_TREE;
      tree *v = vals.get (op);
      return v ? *v : NULL_TREE;
    };

  bool proven = true;
  fold_defer_overflow_warnings ();
  for (unsigned trips = 1; trips < need; ++trips)
    {
      /* One pass over the body: fold what folds.  */
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gassign *a = dyn_cast <gassign *> (gsi_stmt (gsi));
	  if (!a)
	    continue;
	  tree lhs = gimple_assign_lhs (a);
	  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
	    continue;
	  tree_code code = gimple_assign_rhs_code (a);
	  tree type = TREE_TYPE (lhs);
	  tree op1 = lookup (gimple_assign_rhs1 (a));
	  tree op2 = gimple_num_ops (a) > 2
	    ? lookup (gimple_assign_rhs2 (a)) : NULL_TREE;
	  tree value = NULL_TREE;
	  switch (get_gimple_rhs_class (code))
	    {
	    case GIMPLE_SINGLE_RHS:
	      value = op1;
	      break;
	    case GIMPLE_UNARY_RHS:
	      value = op1 ? fold_unary (code, type, op1) : NULL_TREE;
	      break;
	    case GIMPLE_BINARY_RHS:
	      value = op1 && op2
		? fold_binary (code, type, op1, op2) : NULL_TREE;
	      break;
	    default:
	      value = NULL_TREE;
	      break;
	    }
	  if (value && is_gimple_min_invariant (value))
	    vals.put (lhs, value);
	  else
	    vals.remove (lhs);
	}

      tree lhs = lookup (gimple_cond_lhs (cond));
      tree rhs = lookup (gimple_cond_rhs (cond));
      tree test = lhs && rhs
	? fold_binary (gimple_cond_code (cond), boolean_type_node, lhs, rhs)
	: NULL_TREE;
      if (!test || TREE_CODE (test) != INTEGER_CST)
	{
	  proven = false;
	  break;
	}
      edge taken = integer_zerop (test) ? false_edge : true_edge;
      if (taken == exit)
	{
	  proven = false;	/* provably exits before NEED trips */
	  break;
	}

      /* Advance the tracked header PHIs through the latch; one whose
	 next value does not fold merely stops being tracked.  */
      auto_vec<tree, 4> next;
      for (gphi *phi : phis)
	next.safe_push (lookup (PHI_ARG_DEF_FROM_EDGE (phi, latch_e)));
      /* Body definitions do not survive the backedge.  */
      vals.empty ();
      unsigned kept = 0;
      for (unsigned ix = 0; ix != phis.length (); ++ix)
	if (next[ix])
	  {
	    vals.put (gimple_phi_result (phis[ix]), next[ix]);
	    phis[kept++] = phis[ix];
	  }
      phis.truncate (kept);
    }
  fold_undefer_and_ignore_overflow_warnings ();
  return proven;
}

/* Duplicate LOOP's single-block body once onto its entry edge and
   return the loop's new entry edge (peeled block -> header), on which
   the caller places the constant programming.  Every proof has already
   passed: the body is CC-canonical (only typed RVTT calls, audited raw
   Dst/RWC words, pure assignments, PHIs, labels, debug statements and
   the loop condition), and the bounded trip evaluation proved the
   first iteration never exits -- the peeled copy therefore falls
   through to the loop unconditionally and the copied exit test is
   dropped (its scalar chain is still copied; the header PHIs consume
   it).  Header PHIs evaluate to their entry arguments inside the copy
   and are re-seeded with the copy's latch values.  Virtual operands on
   the copies are cleared for the pass-level virtual-SSA update.

   OUT_COPY_BB and NAMES (original in-body SSA name -> the copy's fresh
   name) expose the peel to the pressure-park pre-peel placement below:
   the park tier needs the peel block itself and the copy of a hoisted
   candidate's materialization (to erase the duplicate).  NAMES also
   carries the header-PHI -> entry-argument seeds the remap starts
   from; the park lookup keys on a load's lhs, never a PHI result, so
   the seeds are inert there.  */

static edge
peel_first_iteration (class loop *loop, edge entry,
		      basic_block *out_copy_bb, hash_map<tree, tree> *names)
{
  basic_block bb = loop->header;
  edge latch_e = loop_latch_edge (loop);

  hash_map<tree, tree> &map = *names;
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      tree res = gimple_phi_result (phi);
      if (!virtual_operand_p (res))
	map.put (res, PHI_ARG_DEF_FROM_EDGE (phi, entry));
    }

  basic_block copy_bb = split_edge (entry);
  gimple_stmt_iterator at = gsi_start_bb (copy_bb);

  auto remap = [&map] (tree op) -> tree
    {
      if (op && TREE_CODE (op) == SSA_NAME)
	if (tree *found = map.get (op))
	  return *found;
      return op;
    };

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;

      gimple *cp = gimple_copy (stmt);

      /* Remap operands, then give each definition a fresh name.  */
      if (gcall *call = dyn_cast <gcall *> (cp))
	{
	  for (unsigned ix = 0; ix != gimple_call_num_args (call); ++ix)
	    gimple_call_set_arg (call, ix, remap (gimple_call_arg (call, ix)));
	}
      else if (gassign *a = dyn_cast <gassign *> (cp))
	{
	  for (unsigned ix = 1; ix != gimple_num_ops (a); ++ix)
	    gimple_set_op (a, ix, remap (gimple_op (a, ix)));
	}
      /* Audited raw Dst/RWC words carry one constant input: nothing to
	 remap and nothing defined.  */

      if (tree lhs = gimple_get_lhs (cp))
	{
	  gcc_assert (TREE_CODE (lhs) == SSA_NAME);
	  tree fresh = make_ssa_name (TREE_TYPE (lhs));
	  gimple_set_lhs (cp, fresh);
	  map.put (lhs, fresh);
	}

      if (gimple_vdef (cp))
	gimple_set_vdef (cp, NULL_TREE);
      if (gimple_vuse (cp))
	gimple_set_vuse (cp, NULL_TREE);

      if (gsi_end_p (at))
	{
	  gsi_insert_before (&at, cp, GSI_NEW_STMT);
	  at = gsi_for_stmt (cp);
	}
      else
	gsi_insert_after (&at, cp, GSI_NEW_STMT);
      update_stmt (cp);
    }

  if (out_copy_bb)
    *out_copy_bb = copy_bb;

  /* The loop's entry values are now the peeled iteration's latch
     values.  */
  edge new_entry = single_succ_edge (copy_bb);
  for (gphi_iterator psi = gsi_start_phis (bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gphi *phi = psi.phi ();
      if (virtual_operand_p (gimple_phi_result (phi)))
	continue;
      tree larg = PHI_ARG_DEF_FROM_EDGE (phi, latch_e);
      SET_USE (PHI_ARG_DEF_PTR_FROM_EDGE (phi, new_entry), remap (larg));
    }

  if (dump_file)
    fprintf (dump_file,
	     "const-residency: peeled first iteration of loop bb %d into "
	     "bb %d (CC-canonical body; programming point follows the "
	     "peeled all-lanes SFPENCC)\n",
	     bb->index, copy_bb->index);
  return new_entry;
}

/* -------------------------------------------------------------------- */
/* Pre-peel ambient lane-state proof.

   The park-ordering deferral (gimple-rvtt-invariant.cc,
   residency-walk-ordering) hands a CC-restore loop's in-region
   constants to this walk -- and the walk's park tier then pays a cost
   the early invariant hoist never paid: the peel duplicates every
   still-in-body materialization statement for statement, and the park
   placement at the POST-peel programming point leaves that duplicate
   alive, so a park-hoisted constant is materialized twice per loop
   entry (once inline in the peeled iteration, once at the programming
   point).  On the softplus PRODUCTION body that tax measured +0.65%
   KERNEL vs the early-hoist form (2 words x 4 parked constants per
   face-loop entry).

   The early hoist avoids it by materializing BEFORE the peel ever
   copies the body -- sound at 114t because outside a structured region
   the machine is architecturally all-lanes (no unstructured CC exists
   at that pipeline position).  At this pass's position the CC is
   lowered, so the equivalent placement needs the ambient fact proven
   from the lowered statements: the pre-peel point is all-lanes exactly
   when NO function-local CC-affecting statement reaches it without an
   intervening word-exact all-lanes SFPENCC (the canonical-tail kill --
   the very SFPENCC each CC-canonical body ends with; the reference simulator's
   TENSIX_EXECUTE_SFPENCC overwrites cc/cc_en from the immediates).
   cc_write_reaches_point_p cannot serve: it has no kill modeling, and
   a CC-canonical loop's own writers always "reach" their preheader
   around an enclosing backedge despite every such path passing the
   trailing all-lanes SFPENCC.

   With the ambient proven all-lanes, the pre-peel park placement
   writes EVERY lane -- the identical superset-write refinement the
   post-peel placement already stands on (extra lanes over any
   iteration's in-place mask carried RA-indeterminate fresh-SSA
   garbage no audited consumer propagates), now also covering the
   peeled iteration's own consumers, so the peel's duplicated
   materialization can be erased and its uses redirected to the parked
   definition.  The CC-affecting vocabulary is the typed one -- calls
   stay CC-transparent (the established fn-entry all-lanes model of
   the plain loop class), and raw asm is transparent BECAUSE the whole
   transform is gated on the TU raw-boundary audit (facts.refused
   bails before any placement): in a TU where this code runs at all,
   every raw `.ttinsn' word decodes through the audited table (NOP /
   sync / thread-config / CLEARDVALID / SETRWC / LOADI / SFPCONFIG --
   none writes lane state) and every store is proven unable to alias
   an instruction FIFO, so no asm can carry a CC write.  An unproven
   ambient keeps the post-peel placement byte-identically.  */

enum cc_block_class
{
  CC_BLOCK_TRANSPARENT,		/* no CC-affecting statement */
  CC_BLOCK_KILLS,		/* last CC event is the all-lanes SFPENCC */
  CC_BLOCK_DIRTY		/* a CC-affecting statement escapes */
};

/* Classify BB by its LAST CC-affecting statement: CC_BLOCK_KILLS when
   that is the word-exact all-lanes SFPENCC, CC_BLOCK_DIRTY when any
   other CC writer (or a pushc/popc) follows the last kill, and
   CC_BLOCK_TRANSPARENT when the block has no typed CC event at all.
   Calls and raw asm classify as transparent because the TU
   raw-boundary audit gates the whole transform (block comment
   above).  */

static cc_block_class
classify_cc_block (basic_block bb)
{
  cc_block_class cls = CC_BLOCK_TRANSPARENT;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
	  || gimple_code (stmt) == GIMPLE_COND)
	continue;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	continue;		/* calls and raw asm are CC-transparent:
				   the TU raw-boundary audit gates the
				   whole transform (block comment).  */
      gcall *call = as_a <gcall *> (stmt);
      if (rvtt_all_lanes_encc_p (stmt))
	cls = CC_BLOCK_KILLS;
      else if (insnd->sets_cc (call)
	       || insnd->id == rvtt_insn_data::sfppushc
	       || insnd->id == rvtt_insn_data::sfppopc)
	cls = CC_BLOCK_DIRTY;
    }
  return cls;
}

/* Whether the lane-enable state on entry to POINT_BB is provably the
   architectural all-lanes state: every backwards CFG path from
   POINT_BB's entry hits the function entry (all-lanes ambient) or an
   all-lanes-SFPENCC-terminated block before any escaping CC-affecting
   statement.  */

static bool
prepeel_ambient_all_lanes_p (basic_block point_bb)
{
  hash_set<basic_block> visited;
  auto_vec<basic_block, 16> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, point_bb->preds)
    work.safe_push (e->src);
  while (!work.is_empty ())
    {
      basic_block b = work.pop ();
      if (b == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      if (visited.add (b))
	continue;
      switch (classify_cc_block (b))
	{
	case CC_BLOCK_DIRTY:
	  return false;
	case CC_BLOCK_KILLS:
	  continue;
	case CC_BLOCK_TRANSPARENT:
	  FOR_EACH_EDGE (e, ei, b->preds)
	    work.safe_push (e->src);
	  break;
	}
    }
  return true;
}

/* MERGE-RENAME class (the GIMPLE-side rename successor identified
   by the addrsqrt kernel's closure analysis).

   The one materialization shape no invariant or residency class can
   reach is the IN-LOOP constant-immediate CC-merge

       y' = sfploadi_lv (0B, y, imm, 0, 0, 0)

   whose live-value link Y is a loop-varying vector: the STATEMENT is
   loop-variant even though its immediate is not, so
   rvtt_invariant_constant_load_p can never admit it and the walk's
   candidate classes never see the immediate (the addrsqrt kernel
   census's "the one in-loop loadi is an lv CC-merge, not a hoistable
   full-lane materialization").  The GIMPLE-side rename gives the
   immediate its own pre-RA value name:

       t  = sfploadi (0B, imm, 0, 0, 0)      ; full-lane twin, parked
       y' = sfpassign_lv (y, t)              ; register-source merge

   Bit-exactness: sfpassign_lv writes Y's CC-ENABLED lanes from its
   newval operand and keeps the link's value on disabled lanes --
   exactly the original merge's lane function -- and it READS the
   newval only on enabled lanes, so the rename is exact whenever T
   carries IMM on (at least) every lane enabled at the merge point.
   The twin is placed by the established pressure-park LREG tier at its
   proven programming point (the pre-peel head under the prepeel
   ambient-all-lanes proof, or the post-peel / plain loop entry point
   after the all-lanes SFPENCC), where EVERY lane is written -- a
   strict superset.  No consumer audit of the original merge's users is
   needed: the merge keeps its lhs, lane mask, and value function
   verbatim; the parked twin's only consumer is the constructed
   sfpassign_lv reading it in the (non-live) newval position.

   Vocabulary (minimal, prove-or-refuse): the single-issue shortened
   FLOATB form only (mod 0, all scalar args INTEGER_CST, null
   instruction-buffer operand, no virtual operands) -- exactly the
   constant-image derivation constant_chain_value_p already blesses for
   the walk's own shortened candidates.  Multi-issue merge chains and
   every other mod refuse by name (merge-rename-form-unsupported), so
   the registry counters census the out-of-vocabulary breadth
   corpus-wide.

   Delivery economics are structural: one in-loop word before (the
   SFPLOADI _lv), one after (the SFPMOV-lowered sfpassign_lv), plus the
   twin's own materialization word once at the programming point -- the
   rename's priced delivery benefit is identically negative, so the
   class refuses by name (merge-rename-word-neutral) unless the
   -mtt-tensix-merge-rename-allow-neutral measurement override admits
   the neutral rename for measurement legs (an established precedent: mechanism
   shipped default-off, its named successor executed and measured).
   A twin the LREG tier cannot place undoes exactly
   (merge-rename-placement-refused); the
   undo leg is byte-identical to the flag-off leg.  */

struct merge_rename_cand
{
  gcall *tail;			/* the in-loop constant-immediate CC-merge */
  tree link;			/* its loop-varying live-value operand */
  unsigned value;		/* full 32-bit lane image of the immediate */
  class loop *loop;
  edge entry;			/* placement entry (mirrors the LOOP class's
				   own derivation at collection) */
  bool peel;
  bool cc_lifted;
};

/* Classify LOAD as a merge-rename candidate.  Returns true and fills
   *LINK_OUT / *VALUE_OUT for the in-vocabulary shape; fires the
   form-unsupported refusal (and returns false) for an immediate
   CC-merge outside the vocabulary; returns false silently for
   statements that are not immediate CC-merges at all.  */

static bool
merge_rename_shape_p (gcall *load, FILE *stream,
		      tree *link_out, unsigned *value_out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (load);
  if (!insnd || insnd->id != rvtt_insn_data::sfploadi_lv)
    return false;
  tree lhs = gimple_call_lhs (load);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
    return false;
  tree link = loadi_lv_link (load);
  if (!link || TREE_CODE (link) != SSA_NAME)
    return false;
  /* A constant-chain link is the established two-issue materialization
     owned by the invariant/residency classes; only a varying link is
     the un-hoistable CC-merge this class renames.  */
  remat_chain chain;
  if (remat_chain_p (lhs, &chain))
    return false;
  auto unsupported = [&] (const char *what) -> bool
    {
      rvtt_refuse (RVTT_REF_MERGE_RENAME_FORM_UNSUPPORTED, stream,
		   "merge-rename: refused (merge-rename-form-unsupported: "
		   "%s): ", what);
      if (stream)
	print_gimple_stmt (stream, load, 0);
      return false;
    };
  /* Conservative call virtual operands (VUSE, and the builtin decl's
     clobber VDEF) are fine: the walk's own materialization hoists
     unlink them at commit under TODO_update_ssa_only_virtuals (the
     invariant pass's virtual-operand discipline, lreg_hoist), and the
     null instruction-buffer operand required below rules out the one
     memory channel with real ordering semantics (the synth FIFO).  */
  if (!integer_zerop (gimple_call_arg (load, 0)))
    return unsupported ("non-null instruction-buffer operand");
  for (unsigned ix = 2; ix != gimple_call_num_args (load); ++ix)
    if (TREE_CODE (gimple_call_arg (load, ix)) != INTEGER_CST)
      return unsupported ("runtime immediate");
  tree mod = gimple_call_arg (load, gimple_call_num_args (load) - 1);
  if (!integer_zerop (mod))
    return unsupported ("non-FLOATB mod");
  /* The lv link chained through ANOTHER immediate CC-merge is the
     multi-issue chain (a 32-bit immediate merged in halves): out of
     this stage's vocabulary, counted by name for the registry's
     breadth census.  */
  if (gcall *ldef = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (link)))
    if (const rvtt_insn_data *ld = rvtt_get_insn_data (ldef))
      if (ld->id == rvtt_insn_data::sfploadi_lv)
	return unsupported ("multi-issue merge chain");
  *link_out = link;
  *value_out = (TREE_INT_CST_LOW (gimple_call_arg (load, 2)) & 0xffff) << 16;
  return true;
}

/* The constant-residency stage of the pass: collect hoist candidates
   over FN by class (loop-invariant constant loads, pressure-bounded
   block candidates, MAD-pair and hoist-reuse groups, and merge-rename
   chains), prove each class's legality conditions, and commit the
   winners.  ST carries the pass-wide statement state.  Returns true
   iff any statement changed.  */

bool
residency_transform (function *fn, prgm_state *st)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  auto_vec<residency_candidate> loop_cands;
  auto_vec<residency_candidate> pressure_cands;
  auto_vec<residency_candidate> madpair_cands;
  auto_vec<residency_candidate> hoistreuse_cands;
  auto_vec<merge_rename_cand> merge_cands;
  unsigned madpair_group = 0;
  hash_set<int_hash<unsigned, 0> > invalid_madpair_groups;
  hash_set<gimple *> taken;

  /* LOOP class.  */
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (!loop->num)
	continue;
      edge entry = rvtt_loop_entry_edge (loop);
      bool peel = false;
      gimple *cc_limit = nullptr;
      const char *why
	= !entry ? "no-single-entry"
	: rvtt_loop_hoist_region_opaque_p (loop, entry) ? "opaque-hoist-region"
	: rvtt_preheader_insertion_blocked_p (entry) ? "preheader-blocked"
	: nullptr;
      if (!why && rvtt_loop_has_sfpu_barrier_p (loop))
	{
	  /* CC-canonical rescue: a single-block body whose
	     only barrier statements are CC writers ending in the
	     word-exact all-lanes SFPENCC admits the first-iteration
	     peel below; candidates must precede the body's first CC
	     writer.  Anything else keeps the barrier refusal
	     byte-identically.  */
	  rvtt_cc_canonical_body canon = rvtt_loop_cc_canonical_body (loop);
	  if (canon.proven)
	    {
	      peel = true;
	      cc_limit = canon.first_cc_writer;
	    }
	  else
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "const-residency: loop bb %d cc-canonical proof "
			 "failed (%s)\n", loop->header->index, canon.why);
	      why = "sfpu-barrier";
	    }
	}
      if (why)
	{
	  rvtt_refuse_by_name (why, dump_file,
			       "const-residency: loop bb %d refused (%s)\n",
			       loop->header->index, why);
	  continue;
	}

      /* Profitability: the entry-edge programming (W+1 pushed words
	 once) pays for itself against the W pushed SFPLOADI words
	 saved per iteration at two trips (rvtt-cost.md delivery
	 model).  Correctness is trip-independent -- see
	 classify_second_trip -- so only a PROVEN single-trip loop
	 refuses (a proven loss); a runtime trip count is admitted with
	 a worst case of one extra pushed word on a single-trip entry.
	 (The CC-canonical peel class prices its peel separately below
	 and genuinely needs proven trips for the peel itself.)  */
      bool admits_runtime_trips = false;
      if (!peel)
	switch (classify_second_trip (loop, entry))
	  {
	  case TRIPS_AT_LEAST_2:
	    break;
	  case TRIPS_PROVEN_SINGLE:
	    rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			 "const-residency: loop bb %d refused "
			 "(trip-count-single-trip: the loop provably runs "
			 "one trip; the programming can never recover its "
			 "cost)\n", loop->header->index);
	    continue;
	  case TRIPS_UNKNOWN:
	    /* Dump deferred until candidates exist: this analysis
	       admission printed on 144/179 corpus ops with zero
	       candidates, drowning the fire signal.  */
	    admits_runtime_trips = true;
	    break;
	  }

      /* CROSSLOOP-CC-PEEL: the peel exists only to
	 manufacture an all-lanes programming point INSIDE a loop whose
	 body writes CC -- and any loop that needs one sits inside
	 enclosing loops whose region scans refuse those very CC writes
	 (crossloop-cc-unproven), so the placement walk can never lift
	 a peel-class placement and the peel-plus-programming
	 re-executes on EVERY enclosing iteration for a value that
	 cannot change (atan2's 295t face loop, the named witness).
	 For a PROGRAMMING-ONLY placement the crossed CC writes are
	 immaterial: the staged SFPLOADI + SFPCONFIG execute once
	 BEFORE the crossed loops, and the parked constant register is
	 state no CC write can touch.  Walk the enclosing loops under
	 the cc-immaterial region discipline (structured typed CC atoms
	 admitted; the word/replay/MOP/LREG refusals unchanged), then
	 prove the lifted preheader under the plain loop class's own
	 fn-entry-all-lanes ambient: no function-local CC write reaches
	 it -- the point executes once, ahead of every crossed
	 iteration, so the crossed loops' own CC writers are out of its
	 past by construction, and the staged load writes EVERY lane (a
	 superset of any consumer mask, the peel class's own refinement;
	 post-CC candidates additionally pass the consumer audit at
	 collection).  On success the loop's candidates place as plain
	 loop-class programming at the lifted entry and the peel is
	 never created.  Any unproven piece -- a walk stop (printed by
	 name), a proven single trip, a CC write reaching the lifted
	 preheader -- keeps the established peel byte-identically.  */
      bool cc_lifted_loop = false;
      edge lifted_entry = entry;
      if (peel
	  && riscv_tt_opt_crossloop_cc_peel > 0
	  && riscv_tt_opt_crossloop_hoist > 0)
	{
	  lifted_entry
	    = rvtt_crossloop_outermost_entry (loop, entry, 0x7fff,
					      /*cc_immaterial=*/true);
	  if (lifted_entry != entry)
	    {
	      if (classify_second_trip (loop, entry) == TRIPS_PROVEN_SINGLE)
		{
		  rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			       "const-residency: loop bb %d cc-peel lift "
			       "refused (trip-count-single-trip: the loop "
			       "provably runs one trip; the lifted "
			       "programming can never recover its cost)\n",
			       loop->header->index);
		  lifted_entry = entry;
		}
	      else
		{
		  auto_vec<gimple *> lift_writers;
		  collect_cc_writers (fn, &lift_writers);
		  if (cc_write_reaches_point_p (lift_writers,
						lifted_entry->src, nullptr))
		    {
		      rvtt_refuse (RVTT_REF_CROSSLOOP_CC_PEEL_ENTRYCC_UNPROVEN,
				   dump_file,
				   "const-residency: loop bb %d cc-peel lift "
				   "refused (crossloop-cc-peel-entrycc-"
				   "unproven: a CC write reaches the lifted "
				   "preheader bb %d)\n",
				   loop->header->index,
				   lifted_entry->src->index);
		      lifted_entry = entry;
		    }
		  else
		    {
		      cc_lifted_loop = true;
		      if (dump_file)
			fprintf (dump_file,
				 "const-residency: loop bb %d cc-peel "
				 "placement lifted to entry bb %d "
				 "(programming-only lift; crossed-loop CC "
				 "immaterial to the parked constant; no "
				 "peel)\n",
				 loop->header->index,
				 lifted_entry->dest->index);
		    }
		}
	    }
	}

      /* Plain (non-cc-lifted) placement point for this loop's
	 candidates: the outermost audited entry for a NON-peel
	 candidate, exactly the fusion class's discipline.  A PEEL-class
	 candidate is ENTRY-ANCHORED: peel_first_iteration copies the
	 loop body's first iteration onto the placement edge, so a
	 lifted placement would (a) seed the copy's header PHIs from an
	 edge that does not enter the loop, (b) move the copied body
	 above the crossed loops' own definitions, and (c) execute the
	 peeled iteration once per function instead of once per loop
	 entry.  Before the crossed-loop widening
	 (-mtt-tensix-optimize-cc-region-general) the anchoring was
	 implicit -- the enclosing region scans refused the peel loop's
	 own CC writers (CROSSLOOP-CC-PEEL block comment above), so the
	 walk could never lift a peel-class placement -- but the
	 widening admits those crossed CC atoms and the structural fact
	 must refuse by name (demonstrated wrong code: the lifted peel's
	 uses reached RTL above their defs and init-regs materialized a
	 zero const_vector no move pattern recognizes).  The candidate
	 keeps the established entry-anchored peel placement
	 byte-identically.  The sound lift for a peel-class loop is the
	 dedicated programming-only path above (no peel,
	 consumer audit, entry-CC proof).  */
      auto plain_entry = [&] () -> edge
	{
	  if (riscv_tt_opt_crossloop_hoist <= 0)
	    return entry;
	  edge oentry = rvtt_crossloop_outermost_entry (loop, entry, 0x7fff);
	  if (peel && oentry != entry)
	    {
	      rvtt_refuse (RVTT_REF_CROSSLOOP_PEEL_ENTRY_ANCHORED, dump_file,
			   "const-residency: loop bb %d lift refused "
			   "(crossloop-peel-entry-anchored: the "
			   "first-iteration peel is anchored to the loop's "
			   "own entry edge; keeping the entry-anchored "
			   "placement)\n", loop->header->index);
	      return entry;
	    }
	  return oentry;
	};

      auto_vec<residency_candidate> this_loop;
      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
	    continue;
	  bool cc_reached = false;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      /* Peel class: a candidate at or after the body's first
		 CC writer executed under the v_if region's partial
		 lane state; only the pre-region prefix is proven
		 all-lanes on iterations 2..N.

		 PRESSURE-PARK widening: such a position is
		 nevertheless admissible when every consumer of the
		 candidate's value is in the audited lane-predicated
		 set (remat_consumer_audited_p, the const-remat
		 audit).  The constant-register read that replaces the
		 materialization carries the constant in EVERY lane --
		 a strict superset of whatever lane subset the original
		 predicated SFPLOADI wrote -- so every lane the
		 original program DEFINED reads the identical value,
		 and the only changed lanes are ones whose original
		 content was the fresh SSA definition's RA-dependent
		 indeterminate garbage, which no audited consumer
		 propagates (the same superset-write refinement the
		 invariant pass's in-region hoist admission is built
		 on, gimple-rvtt-invariant.cc block comment).  A PHI
		 use or an unaudited/_lv-tied consumer refuses the
		 candidate by name.  */
	      if (cc_limit && gsi_stmt (gsi) == cc_limit)
		cc_reached = true;
	      if (cc_reached && riscv_tt_opt_pressure_park <= 0)
		break;
	      gcall *load = dyn_cast <gcall *> (gsi_stmt (gsi));
	      if (!load || taken.contains (load))
		continue;
	      if (!rvtt_invariant_constant_load_p (load, loop,
						   /*allow_shortened=*/true))
		{
		  /* MERGE-RENAME collection: the
		     immediate CC-merge whose loop-varying link fails
		     the invariant predicate above.  Recorded only; the
		     rename and its placement run after every
		     established class has placed (block comment at
		     merge_rename_cand).  */
		  tree mlink;
		  unsigned mvalue;
		  if (riscv_tt_opt_residency_merge_rename > 0
		      && riscv_tt_opt_pressure_park > 0
		      && merge_rename_shape_p (load, dump_file,
					       &mlink, &mvalue))
		    {
		      merge_rename_cand m;
		      m.tail = load;
		      m.link = mlink;
		      m.value = mvalue;
		      m.loop = loop;
		      m.cc_lifted = cc_lifted_loop;
		      m.peel = cc_lifted_loop ? false : peel;
		      m.entry = cc_lifted_loop ? lifted_entry
			: plain_entry ();
		      merge_cands.safe_push (m);
		    }
		  continue;
		}
	      remat_chain chain { load, load };
	      unsigned value;
	      if (!constant_chain_value_p (chain, &value))
		continue;
	      bool lift_this = cc_lifted_loop;
	      if (cc_reached || cc_lifted_loop)
		{
		  /* The consumer audit.  Post-CC candidates (the
		     pressure-park admission) require it always.  A
		     cc-lifted candidate requires it even in
		     the pre-CC prefix: the peel this lift forgoes kept
		     the FIRST iteration's materializations verbatim
		     (the cc-canonical proof says nothing about the
		     first iteration's lane state -- crossed-loop CC
		     atoms may run before the loop is reached), so the
		     all-lanes parked read is bit-exact only under the
		     audited-consumer superset-write refinement.  A
		     pre-CC candidate failing the audit under the lift
		     keeps the established peel placement (named
		     below), never a bare drop.  */
		  const char *bad = nullptr;
		  gimple *bad_use = nullptr;
		  imm_use_iterator uit;
		  gimple *use;
		  FOR_EACH_IMM_USE_STMT (use, uit, gimple_call_lhs (load))
		    {
		      if (is_gimple_debug (use))
			continue;
		      if (gimple_code (use) == GIMPLE_PHI)
			{
			  bad = "postcc-phi-use";
			  bad_use = use;
			  break;
			}
		      if (!remat_consumer_audited_p (use,
						     gimple_call_lhs (load)))
			{
			  bad = "consumer-lane-discipline-unaudited";
			  bad_use = use;
			  break;
			}
		    }
		  if (bad && cc_reached)
		    {
		      if (dump_file)
			{
			  rvtt_refuse_by_name (bad, dump_file,
					       "pressure-park: refused (%s): ",
					       bad);
			  print_gimple_stmt (dump_file, bad_use, 0);
			}
		      continue;
		    }
		  if (bad)
		    {
		      /* Pre-CC candidate, lift refused by the audit:
			 keep the peel placement byte-identically.  */
		      lift_this = false;
		      if (dump_file)
			{
			  rvtt_refuse
			    (RVTT_REF_CROSSLOOP_CC_PEEL_CONSUMER_UNAUDITED,
			     dump_file,
			     "const-residency: cc-peel lift refused "
			     "(crossloop-cc-peel-consumer-unaudited; "
			     "candidate keeps the peel placement): ");
			  print_gimple_stmt (dump_file, bad_use, 0);
			}
		    }
		  else if (cc_reached && dump_file)
		    {
		      fprintf (dump_file,
			       "pressure-park: admitted post-CC candidate "
			       "(every consumer lane-predicated-audited): ");
		      print_gimple_stmt (dump_file, load, 0);
		    }
		}
	      residency_candidate c;
	      c.load = load;
	      c.value = value;
	      c.loop = loop;
	      if (lift_this)
		{
		  /* Programming-only lift: the lifted entry
		     was proven at discovery; this candidate creates no
		     peel.  */
		  c.entry = lifted_entry;
		  c.peel = false;
		  c.cc_lifted = true;
		}
	      else
		{
		  /* Same outermost audited placement as the fusion
		     class; a peel-class candidate stays entry-anchored
		     (block comment at plain_entry).  */
		  c.entry = plain_entry ();
		  c.peel = peel;
		}
	      c.uses = count_nondebug_uses (gimple_call_lhs (load));
	      this_loop.safe_push (c);
	    }
	}

      /* MAD-PAIR class: the invariant pass's
	 cc-restore-discharged hoist parks a loop's constants in the
	 preheader, where neither this class's in-loop scan above nor
	 the fusion class sees them; the downstream muli/addi immediate
	 folds then consume the shortened FLOATB materializations "in
	 preference to mul,add->mad" and a resident-MAD row body decays
	 to a per-iteration MUL+ADDI (a measured hardsigmoid
	 regression).  Re-claim exactly the fold-vulnerable hoisted
	 constants of a single-use mul+add pair into PRGM registers:
	 the constant-register read is not an SFPLOADI, the immediate
	 folds no longer match, and the pre-existing mad combine fuses
	 the pair -- the same re-offer the in-loop fusion class
	 performs, from the hoisted placement.  RECOGNITION-ONLY like
	 that class: no arm here ever fuses (bit-exactness is decided
	 by the unchanged downstream rule).  Programming is in-place at
	 the hoisted materialization (the pressure class's discipline),
	 so the staged write and the constant-register read keep the
	 hoist's own execution point and the all-lanes proof is the
	 pressure-style reach test below.  Pricing: one extra pushed
	 word per claim once (the SFPCONFIG; the staged materialization
	 replaces the hoisted one) against one saved word per proven
	 iteration -- a proven single trip is a wash and refuses;
	 runtime trips admit under the established W2 policy.  */
      {
	loop_trip_class mp_trips = !peel
	  ? (admits_runtime_trips ? TRIPS_UNKNOWN : TRIPS_AT_LEAST_2)
	  : classify_second_trip (loop, entry);
	if (mp_trips == TRIPS_PROVEN_SINGLE)
	  {
	    rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			 "const-residency: madpair loop bb %d refused "
			 "(trip-count-single-trip: the loop provably runs "
			 "one trip; the re-claim can never recover its "
			 "programming word)\n", loop->header->index);
	  }
	else
	  for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	    {
	      basic_block bb = body[ix];
	      if (bb->loop_father != loop
		  || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
		continue;
	      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		   !gsi_end_p (gsi); gsi_next (&gsi))
		{
		  gcall *add = dyn_cast <gcall *> (gsi_stmt (gsi));
		  if (!add)
		    continue;
		  const rvtt_insn_data *addd = rvtt_get_insn_data (add);
		  /* Vocabulary widening: the _lv spelling of
		     the pair's add joins the discovery under the flag;
		     the base spelling keeps its established recognition
		     byte-identically.  */
		  bool add_lv = riscv_tt_opt_madpair_vocabulary > 0
		    && addd && addd->id == rvtt_insn_data::sfpadd_lv;
		  if (!addd
		      || (addd->id != rvtt_insn_data::sfpadd && !add_lv)
		      || !gimple_call_lhs (add)
		      || TREE_CODE (gimple_call_lhs (add)) != SSA_NAME)
		    continue;
		  unsigned ab = add_lv ? madpair_value_base (addd) : 0;
		  if (!integer_zerop (gimple_call_arg (add, ab + 2)))
		    continue;
		  for (unsigned swap = 0; swap != 2; ++swap)
		    {
		      unsigned mb;
		      gcall *mul
			= madpair_vocab_mul_p (gimple_call_arg
					       (add, ab + swap), loop,
					       add, &mb);
		      if (!mul)
			continue;
		      /* Classify the pair's three value operands.  */
		      struct pair_op
		      {
			gcall *load;
			unsigned value;
			bool vulnerable;
			bool shared;
		      };
		      pair_op ops[3];
		      unsigned nops = 0;
		      unsigned value;
		      bool vulnerable, is_shared;
		      if (gcall *l
			  = hoisted_madpair_load_p (gimple_call_arg
						    (add, ab + (1 - swap)),
						    loop, add, &value,
						    &vulnerable, &is_shared))
			ops[nops++]
			  = pair_op { l, value, vulnerable, is_shared };
		      for (unsigned mx = 0; mx != 2; ++mx)
			if (gcall *l
			    = hoisted_madpair_load_p (gimple_call_arg
						      (mul, mb + mx),
						      loop, mul, &value,
						      &vulnerable,
						      &is_shared))
			  ops[nops++]
			    = pair_op { l, value, vulnerable, is_shared };
		      /* Nothing fold-vulnerable: either no decay exists
			 (register operands fuse as they are) or nothing
			 here can prevent it.  */
		      bool any_vulnerable = false;
		      bool blocked = false;
		      for (unsigned ox = 0; ox != nops; ++ox)
			{
			  if (!ops[ox].vulnerable)
			    continue;
			  any_vulnerable = true;
			  if (ops[ox].shared
			      || taken.contains (ops[ox].load))
			    blocked = true;
			}
		      if (!any_vulnerable)
			continue;
		      if (blocked)
			{
			  rvtt_refuse (RVTT_REF_MADPAIR_SHARED_CONSTANT,
				       dump_file,
				       "const-residency: madpair loop bb %d "
				       "refused (madpair-shared-constant): a "
				       "fold-vulnerable materialization has "
				       "consumers beyond the pair; the "
				       "immediate fold fires on it regardless "
				       "of other claims\n",
				       loop->header->index);
			  break;
			}
		      /* Pair-atomic admission: every fold-vulnerable
			 constant of the pair claims under one group
			 key (all-or-none at placement).  */
		      ++madpair_group;
		      for (unsigned ox = 0; ox != nops; ++ox)
			{
			  if (!ops[ox].vulnerable)
			    continue;
			  residency_candidate c;
			  c.load = ops[ox].load;
			  c.value = ops[ox].value;
			  c.loop = loop;
			  c.entry = entry;
			  c.uses = count_nondebug_uses
			    (gimple_call_lhs (ops[ox].load));
			  c.inplace = true;
			  c.group = madpair_group;
			  madpair_cands.safe_push (c);
			  taken.add (c.load);
			  if (dump_file)
			    fprintf (dump_file,
				     "const-residency: madpair loop bb %d "
				     "candidate: hoisted constant 0x%08x is "
				     "fold-vulnerable; re-claiming would "
				     "re-offer the mul+add pair to the mad "
				     "combine\n",
				     loop->header->index, c.value);
			}
		      break;
		    }
		}
	    }
      }

      /* HOISTED-REUSE class under
	 -mtt-tensix-optimize-hoisted-prgm-reuse: a loop-invariant
	 constant materialization the invariant pass already parked
	 OUTSIDE the loop occupies a loop-wide LREG live range the
	 downstream cross-row pairing cannot break (an 8/8-pressure row
	 loop leaves no dead LREG for the row-B rename webs).  Re-claim
	 it into a PRGM constant register through the established
	 place() machinery: a free slot, or the TU value-identical
	 reuse (every typed TU write of the slot derives to this exact
	 32-bit image; reprogramming in place is value-idempotent).
	 The programming point is the materialization's own position --
	 the pressure class's in-place discipline with its cc-reach
	 test below -- and the replacement constant-register read keeps
	 the SSA name, so every consumer (any position, any lane
	 context) reads the identical all-lanes image the hoisted
	 all-lanes materialization produced.  Pricing: one extra
	 pushed word once per claim (the SFPCONFIG); a proven
	 single-trip loop refuses by name (madpair's trip policy).  */
      if (riscv_tt_opt_hoisted_prgm_reuse > 0)
	{
	  loop_trip_class hr_trips = !peel
	    ? (admits_runtime_trips ? TRIPS_UNKNOWN : TRIPS_AT_LEAST_2)
	    : classify_second_trip (loop, entry);
	  if (hr_trips == TRIPS_PROVEN_SINGLE)
	    {
	      rvtt_refuse (RVTT_REF_TRIP_COUNT_SINGLE_TRIP, dump_file,
			   "const-residency: hoisted-reuse loop bb %d refused "
			   "(trip-count-single-trip: the loop provably runs "
			   "one trip; the re-claim can never recover its "
			   "programming word)\n", loop->header->index);
	    }
	  else
	    {
	      /* The call-free window (see call_free_window above):
		 foreign calls or asm inside the loop body defeat the
		 DEAD-claim reclaim placement for every candidate of
		 this loop; the per-candidate half (the load's own
		 block tail) is checked at collection below.  */
	      bool loop_call_free = true;
	      basic_block *hr_body = get_loop_body_in_dom_order (loop);
	      for (unsigned ix = 0;
		   ix != loop->num_nodes && loop_call_free; ++ix)
		{
		  basic_block bb = hr_body[ix];
		  if (bb->loop_father != loop)
		    continue;
		  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		       !gsi_end_p (gsi) && loop_call_free; gsi_next (&gsi))
		    {
		      gimple *stmt = gsi_stmt (gsi);
		      if (is_gimple_debug (stmt))
			continue;
		      if (gimple_code (stmt) == GIMPLE_ASM
			  || (is_gimple_call (stmt)
			      && !rvtt_get_insn_data (stmt)))
			loop_call_free = false;
		    }
		}
	      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
		{
		  basic_block bb = hr_body[ix];
		  if (bb->loop_father != loop)
		    continue;
		  for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		       !gsi_end_p (gsi); gsi_next (&gsi))
		    {
		      gcall *use_call = dyn_cast <gcall *> (gsi_stmt (gsi));
		      if (!use_call || !rvtt_get_insn_data (use_call))
			continue;
		      for (unsigned ax = 0;
			   ax != gimple_call_num_args (use_call); ++ax)
			{
			  tree arg = gimple_call_arg (use_call, ax);
			  if (TREE_CODE (arg) != SSA_NAME)
			    continue;
			  gcall *load
			    = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (arg));
			  if (!load || !gimple_bb (load)
			      || flow_bb_inside_loop_p (loop,
							gimple_bb (load))
			      || taken.contains (load)
			      || !rvtt_invariant_constant_load_p
				   (load, loop, /*allow_shortened=*/true))
			    continue;
			  unsigned value;
			  if (!single_issue_constant_image_p (load, &value))
			    continue;
			  residency_candidate c;
			  c.load = load;
			  c.value = value;
			  c.loop = loop;
			  c.entry = entry;
			  c.uses = count_nondebug_uses
			    (gimple_call_lhs (load));
			  c.inplace = true;
			  c.hoisted_reuse = true;
			  /* Per-candidate half of the reclaim window:
			     the load sits in the loop's entry block
			     and nothing foreign follows it there.  */
			  c.call_free_window = loop_call_free
			    && entry && gimple_bb (load) == entry->src;
			  if (c.call_free_window)
			    for (gimple_stmt_iterator wgsi
				   = gsi_for_stmt (load);
				 !gsi_end_p (wgsi); gsi_next (&wgsi))
			      {
				gimple *wstmt = gsi_stmt (wgsi);
				if (is_gimple_debug (wstmt))
				  continue;
				if (gimple_code (wstmt) == GIMPLE_ASM
				    || (is_gimple_call (wstmt)
					&& !rvtt_get_insn_data (wstmt)))
				  {
				    c.call_free_window = false;
				    break;
				  }
			      }
			  hoistreuse_cands.safe_push (c);
			  taken.add (load);
			  if (dump_file)
			    {
			      fprintf (dump_file,
				       "const-residency: hoisted-reuse loop "
				       "bb %d candidate: out-of-loop "
				       "constant 0x%08x (%u uses); "
				       "re-claiming releases its loop-wide "
				       "LREG live range: ",
				       loop->header->index, c.value, c.uses);
			      print_gimple_stmt (dump_file, load, 0);
			    }
			}
		    }
		}
	      free (hr_body);
	    }
	}
      free (body);

      if (admits_runtime_trips && !this_loop.is_empty () && dump_file)
	fprintf (dump_file,
		 "const-residency: loop bb %d admits runtime trips "
		 "(entry-edge programming is never speculated; "
		 "establishment and no-clobber are trip-independent; "
		 "worst case one extra pushed word per candidate on "
		 "a single-trip entry)\n", loop->header->index);

      /* Peel pricing (rvtt-cost.md, residency-peel model): the loop
	 saves the candidates' materialization words at SLOT each on
	 every iteration after the first; the programming costs PUSH
	 per staged word plus PUSH per SFPCONFIG; the peeled body's
	 words change delivery class from replayed SLOT to pushed PUSH
	 once.  All constants are the established delivery-economics
	 table values; the required trip count is proven by bounded
	 evaluation of the loop's own scalar control.  */
      if (peel && !this_loop.is_empty ())
	{
	  /* Price only the candidates that still place through the
	     peel: a cc-lifted candidate pays its programming
	     once at the lifted preheader under the plain class's model
	     and creates no peel.  With the flag off every candidate is
	     a peel candidate and this is the established computation
	     verbatim.  */
	  unsigned sum_w = 0;
	  unsigned nprog = 0;
	  for (residency_candidate &c : this_loop)
	    if (c.peel)
	      {
		sum_w += loadi_issue_words (c.load);
		++nprog;
	      }
	  if (nprog)
	    {
	  unsigned body_w = 0;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (loop->header);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
	      if (!insnd)
		body_w += gimple_code (stmt) == GIMPLE_ASM;
	      else if (insnd->id == rvtt_insn_data::sfpxloadi
		       || insnd->id == rvtt_insn_data::sfploadi)
		body_w += loadi_issue_words (as_a <gcall *> (stmt));
	      else
		++body_w;
	    }
	  /* The one break-even spelling (rvtt-delivery-cost-core.h
	     residency_peel_break_even_trips).  */
	  unsigned need = rvtt_delivery_cost::residency_peel_break_even_trips
	    (rvtt_dcost_table (), sum_w, nprog, body_w);
	  /* 64 bounds the trip-proof WORK (bounded forward evaluation),
	     not the benefit model: a break-even needing more proven
	     trips than the evaluator will walk refuses by name (cf. the
	     allocator's const_iter bound of 96 for the same discipline).  */
	  if (need > 64 || !loop_trips_at_least_p (loop, entry, need))
	    {
	      rvtt_refuse (RVTT_REF_PEEL_TRIP_COUNT_UNPROVEN, dump_file,
			   "const-residency: loop bb %d refused "
			   "(peel-trip-count-unproven: break-even needs %u "
			   "proven trips; %u candidate words, %u programming "
			   "words, %u-word body)\n",
			   loop->header->index, need, sum_w, sum_w + nprog,
			   body_w);
	      /* Drop the peel members; a cc-lifted member
		 keeps its lifted placement, whose once-per-kernel
		 pricing does not ride the peel break-even.  With the
		 flag off every member is a peel member and this is
		 the established whole-loop refusal verbatim.  */
	      unsigned kept = 0;
	      for (residency_candidate &c : this_loop)
		if (!c.peel)
		  this_loop[kept++] = c;
	      this_loop.truncate (kept);
	    }
	  else if (dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d admits the CC-canonical "
		     "peel (%u candidate words/iteration, break-even %u "
		     "trips proven)\n",
		     loop->header->index, sum_w, need);
	    }
	}

      /* LOOP-RECLAIM window proof (-mtt-tensix-optimize-loop-prgm-reclaim):
	 a DEAD-claim reclaim placement for an in-loop candidate needs
	 the programming-to-readers window call- and asm-free -- the
	 dead claim has a foreign TU writer of a DIFFERENT value, so a
	 callee anywhere between this function's own programming and
	 the in-loop reads could interpose that store.  The window for
	 the established loop programming point is the loop body itself
	 (the CC-canonical peel is a body copy, covered by the same
	 proof) plus the candidate's own entry edge: a crossloop- or
	 cc-lifted entry widens the window across enclosing region
	 content this walk does not prove, so those candidates refuse
	 the reclaim tier by construction (call_free_window stays
	 false; free-slot and value-identical placements are
	 unaffected).  */
      if (riscv_tt_opt_loop_prgm_reclaim > 0 && !this_loop.is_empty ())
	{
	  bool loop_call_free = true;
	  basic_block *lr_body = get_loop_body_in_dom_order (loop);
	  for (unsigned ix = 0;
	       ix != loop->num_nodes && loop_call_free; ++ix)
	    {
	      basic_block bb = lr_body[ix];
	      for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
		   !gsi_end_p (gsi) && loop_call_free; gsi_next (&gsi))
		{
		  gimple *stmt = gsi_stmt (gsi);
		  if (is_gimple_debug (stmt))
		    continue;
		  if (gimple_code (stmt) == GIMPLE_ASM
		      || (is_gimple_call (stmt)
			  && !rvtt_get_insn_data (stmt)))
		    loop_call_free = false;
		}
	    }
	  free (lr_body);
	  for (residency_candidate &c : this_loop)
	    c.call_free_window
	      = loop_call_free && !c.cc_lifted && c.entry == entry;
	  if (!loop_call_free && dump_file)
	    fprintf (dump_file,
		     "const-residency: loop bb %d reclaim window unproven "
		     "(loop-reclaim-call-window: foreign call or asm in "
		     "the loop body)\n", loop->header->index);
	}
      for (residency_candidate &c : this_loop)
	{
	  loop_cands.safe_push (c);
	  taken.add (c.load);
	}
    }

  /* PRESSURE class: only when the model exceeds the LREG file.  */
  {
    const unsigned capacity = rvtt_pressure_capacity ();
    rvtt_pressure_model model;
    rvtt_pressure_compute (fn, capacity, &model);
    if (model.peak > capacity)
      {
	unsigned version;
	tree name;
	FOR_EACH_SSA_NAME (version, name, fn)
	  {
	    remat_chain chain;
	    if (!rvtt_pressure_tracked_p (name)
		|| !remat_chain_p (name, &chain))
	      continue;
	    if (chain.root != chain.tail || taken.contains (chain.tail))
	      continue;
	    unsigned value;
	    if (!constant_chain_value_p (chain, &value))
	      continue;
	    /* Out-of-loop definitions only: replacing an in-loop
	       definition in place would reprogram every iteration.  */
	    basic_block def_bb = gimple_bb (chain.tail);
	    if (def_bb->loop_father && def_bb->loop_father->num != 0)
	      continue;
	    bool relevant = bitmap_bit_p (model.over_bbs, def_bb->index);
	    if (!relevant)
	      {
		bitmap_iterator bi;
		unsigned bbi;
		EXECUTE_IF_SET_IN_BITMAP (model.over_bbs, 0, bbi, bi)
		  if (bbi < model.live_in.length () && model.live_in[bbi]
		      && bitmap_bit_p (model.live_in[bbi],
				       SSA_NAME_VERSION (name)))
		    {
		      relevant = true;
		      break;
		    }
	      }
	    if (!relevant)
	      continue;
	    residency_candidate c;
	    c.load = chain.tail;
	    c.value = value;
	    c.loop = nullptr;
	    c.entry = nullptr;
	    c.uses = count_nondebug_uses (name);
	    pressure_cands.safe_push (c);
	    taken.add (chain.tail);
	  }
      }
    else if (dump_file)
      fprintf (dump_file, "const-residency: pressure %u within the %u-LREG "
	       "file; pressure class idle\n", model.peak, capacity);
  }

  if (loop_cands.is_empty () && pressure_cands.is_empty ()
      && madpair_cands.is_empty () && hoistreuse_cands.is_empty ()
      && merge_cands.is_empty ())
    return false;

  /* The freedom proof and the all-lanes proof gate every allocation,
     exactly as for the M3 fusion class.  MERGE-RENAME candidates
     inherit both gates fail-closed: a refused TU refuses the rename
     with everything else, and the peel / cc-lifted classes they ride
     carry their own all-lanes proofs (non-peel candidates re-prove the
     ambient by name in the placement phase below).  */
  const prgm_tu_facts &facts = tu_prgm_facts ();
  if (facts.refused)
    {
      rvtt_refuse (RVTT_REF_OPAQUE_REGION_UNDECLARED, dump_file,
		   "const-residency: refused (opaque-region-undeclared): %s\n",
		   facts.reason);
      return false;
    }
  {
    auto_vec<gimple *> cc_writers;
    collect_cc_writers (fn, &cc_writers);
    if (!cc_writers.is_empty ())
      {
	/* The all-lanes proof, scoped by reachability: a candidate
	   refuses exactly when some fn-local CC writer can execute
	   before its programming point (the same reach-set cover as
	   the fusion class: the loop header's CFG ancestors include
	   the hoisted entry point's).  The CC-canonical peel class is
	   exempt: its programming point is placed after the peeled
	   iteration's own all-lanes SFPENCC, and every replaced
	   materialization is proven to have executed in that same
	   architectural state -- both facts are local to the peeled
	   loop and independent of other CC writes in the function.
	   The pressure class's point is its own in-place statement.  */
	unsigned kept = 0;
	for (residency_candidate &c : loop_cands)
	  {
	    /* A cc-lifted candidate carried its own
	       preheader proof at discovery: the lifted point executes
	       once, before every crossed iteration, and no fn-local CC
	       write reaches it -- the header-reach test below would
	       wrongly count the candidate loop's own body writers,
	       which are behind the lifted point by construction.  */
	    if (!c.peel && !c.cc_lifted
		&& cc_write_reaches_point_p (cc_writers, c.loop->header,
					     nullptr))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: loop bb %d refused "
			     "(cc-region-unproven): a CC write reaches the "
			     "programming point\n", c.loop->header->index);
		continue;
	      }
	    loop_cands[kept++] = c;
	  }
	loop_cands.truncate (kept);
	kept = 0;
	for (residency_candidate &c : pressure_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: pressure candidate in bb %d "
			     "refused (cc-region-unproven): a CC write reaches "
			     "the in-place programming point\n",
			     gimple_bb (c.load)->index);
		continue;
	      }
	    pressure_cands[kept++] = c;
	  }
	pressure_cands.truncate (kept);
	/* MAD-PAIR class: the in-place programming point is the hoisted
	   materialization's own position -- the pressure-style reach
	   test.  A refused member invalidates its whole group (the
	   remaining claims would pay their programming word while the
	   surviving immediate fold still blocks the mad rule), handled
	   with the group sweep below.  */
	kept = 0;
	for (residency_candidate &c : madpair_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: madpair candidate in bb %d "
			     "refused (cc-region-unproven): a CC write reaches "
			     "the in-place programming point\n",
			     gimple_bb (c.load)->index);
		invalid_madpair_groups.add (c.group);
		continue;
	      }
	    madpair_cands[kept++] = c;
	  }
	madpair_cands.truncate (kept);
	/* HOISTED-REUSE class: same in-place programming point, same
	   pressure-style reach test; no grouping.  */
	kept = 0;
	for (residency_candidate &c : hoistreuse_cands)
	  {
	    if (cc_write_reaches_point_p (cc_writers, gimple_bb (c.load),
					  c.load))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "const-residency: hoisted-reuse candidate in bb "
			     "%d refused (cc-region-unproven): a CC write "
			     "reaches the in-place programming point\n",
			     gimple_bb (c.load)->index);
		continue;
	      }
	    hoistreuse_cands[kept++] = c;
	  }
	hoistreuse_cands.truncate (kept);
	if (loop_cands.is_empty () && pressure_cands.is_empty ()
	    && madpair_cands.is_empty () && hoistreuse_cands.is_empty ()
	    && merge_cands.is_empty ())
	  {
	    rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			 "const-residency: refused (cc-region-unproven)"
			 " -- in-function CC writes reach every candidate"
			 " programming point; cross-call ambient proof is"
			 " not on"
			 " record here\n");
	    return false;
	  }
      }
  }
  if (!st->initialized)
    {
      st->claimed = facts.claimed;
      st->initialized = true;
    }

  /* Priced selection: per-iteration savers first (higher proven
     benefit first), then pressure-relief candidates by use count.
     Deterministic tiebreak by value then use count.  The
     loop-prgm-reclaim tier deliberately does NOT reorder selection:
     a words-saved key is pressure-blind (a duplicate-value pair can
     starve a pressure-relieving loop-wide candidate of its slot and
     blow the 8-LREG file downstream -- observed live on digamma-fresh
     as lreg-pressure-exceeded during bring-up), so GV's
     uses-then-value ranking stands and its known suboptimality for
     mixed word-weight sets stays a named residual.  */
  auto rank = [] (auto_vec<residency_candidate> &v)
    {
      for (unsigned i = 1; i < v.length (); ++i)
	for (unsigned j = i; j > 0
	     && (v[j - 1].uses < v[j].uses
		 || (v[j - 1].uses == v[j].uses
		     && v[j - 1].value > v[j].value)); --j)
	  std::swap (v[j - 1], v[j]);
    };
  rank (loop_cands);
  rank (pressure_cands);
  /* ITEM #13 (placement arbiter): the priced within-class ranking GV's
     comment above names as the known residual.  The digamma-fresh
     starvation that vetoed a words-saved key was pricing WITHOUT a
     pressure term; the arbiter prices both: the primary key is
     pressure relief (the candidate's programming point sits in a
     block the function-wide pressure model marks over capacity), the
     secondary key the run-amortized delivery benefit through the one
     delivery-cost API (loop class: the one-word in-loop
     materialization saved per body execution against the two-word
     PRGM programming once per entry; in-place classes: the extra
     programming word at the same position -- their value IS the
     relief term), then GV's uses-then-value, then discovery order.
     Shadow mode dumps both orders and changes nothing; under
     -mtt-tensix-optimize-priced-placement the priced order decides.
     Any unpriceable member (trip weight unproven) or an over-budget
     class fails the whole class closed to the legacy order, by name
     when deciding.  */
  /* Heap-held: the model's destructor releases an obstack only its
     compute initializes, so an unused model must never be
     destructed.  */
  rvtt_pressure_model *arb_model = nullptr;
  auto priced_rank = [&] (auto_vec<residency_candidate> &v, const char *cls)
    {
      bool decide = riscv_tt_opt_priced_placement > 0;
      if (v.length () < 2 || (!dump_file && !decide))
	return;
      if (v.length () > RVTT_PLACE_MAX_CANDIDATES)
	{
	  if (decide)
	    rvtt_refuse (RVTT_REF_PLACE_BUDGET_EXHAUSTED, dump_file,
			 "placement-arbiter: residency-rank %s over budget "
			 "(place-budget-exhausted); the legacy order "
			 "stands\n", cls);
	  else if (dump_file)
	    fprintf (dump_file,
		     "placement-arbiter: residency-rank %s over budget; "
		     "the legacy order stands\n", cls);
	  return;
	}
      struct priced_key
      {
	bool relief;
	int64_t net;
	unsigned uses;
	unsigned value;
	unsigned idx;		/* legacy-order position (stability) */
      };
      auto_vec<priced_key, 32> keys;
      for (unsigned i = 0; i < v.length (); ++i)
	{
	  residency_candidate &c = v[i];
	  rvtt_place_weight w
	    = rvtt_place_loop_weight (c.loop && !c.inplace ? c.loop
				      : nullptr);
	  if (!w.proven)
	    {
	      if (decide)
		rvtt_refuse (RVTT_REF_PLACE_ALTERNATIVE_UNPRICEABLE,
			     dump_file,
			     "placement-arbiter: residency-rank %s "
			     "unpriceable (place-alternative-unpriceable: "
			     "trip weight unproven); the legacy order "
			     "stands\n", cls);
	      else if (dump_file)
		fprintf (dump_file,
			 "placement-arbiter: residency-rank %s unpriceable "
			 "(trip weight unproven); the legacy order "
			 "stands\n", cls);
	      return;
	    }
	  if (!arb_model)
	    {
	      arb_model = new rvtt_pressure_model;
	      rvtt_pressure_compute (fn, rvtt_pressure_capacity (),
				     arb_model);
	    }
	  priced_key k;
	  k.relief = arb_model->over_bbs
	    && bitmap_bit_p (arb_model->over_bbs,
			     gimple_bb (c.load)->index);
	  k.net = c.loop && !c.inplace
	    ? rvtt_place_net_benefit (1, 2, w)
	    : rvtt_place_net_benefit (0, 1, w);
	  k.uses = c.uses;
	  k.value = c.value;
	  k.idx = i;
	  keys.safe_push (k);
	}
      auto before = [] (const priced_key &a, const priced_key &b)
	{
	  if (a.relief != b.relief)
	    return a.relief;
	  if (a.net != b.net)
	    return a.net > b.net;
	  if (a.uses != b.uses)
	    return a.uses > b.uses;
	  if (a.value != b.value)
	    return a.value < b.value;
	  return a.idx < b.idx;
	};
      /* The same deterministic insertion discipline as GV's rank.  */
      for (unsigned i = 1; i < keys.length (); ++i)
	for (unsigned j = i; j > 0 && before (keys[j], keys[j - 1]); --j)
	  std::swap (keys[j - 1], keys[j]);
      bool differs = false;
      for (unsigned i = 0; i < keys.length (); ++i)
	differs |= keys[i].idx != i;
      if (dump_file)
	{
	  fprintf (dump_file, "placement-arbiter: residency-rank %s legacy=[",
		   cls);
	  for (unsigned i = 0; i < v.length (); ++i)
	    fprintf (dump_file, "%s0x%08x", i ? "," : "", v[i].value);
	  fprintf (dump_file, "] priced=[");
	  for (unsigned i = 0; i < keys.length (); ++i)
	    fprintf (dump_file, "%s0x%08x", i ? "," : "",
		     v[keys[i].idx].value);
	  fprintf (dump_file, "] %s (deciding=%s)\n",
		   differs ? "DISAGREE" : "AGREE",
		   decide ? "priced" : "legacy");
	}
      if (decide && differs)
	{
	  auto_vec<residency_candidate, 32> reordered;
	  for (unsigned i = 0; i < keys.length (); ++i)
	    reordered.safe_push (v[keys[i].idx]);
	  for (unsigned i = 0; i < keys.length (); ++i)
	    v[i] = reordered[i];
	}
    };
  priced_rank (loop_cands, "loop-class");
  priced_rank (pressure_cands, "pressure-class");
  /* LOOP-RECLAIM slot discipline: a DEAD claimed slot whose unique TU
     value equals SOME pending candidate's value is that candidate's
     value-identical home -- reclaiming it with a different value
     forfeits the later (free) value-reuse placement (the repurposed-
     slot belt then refuses it), a net placement LOSS that can starve a
     pressure-relieving parking (observed live on digamma-fresh during
     bring-up: lreg-pressure-exceeded).  The dead scan skips such
     slots.  */
  auto_vec<unsigned, 32> reclaim_reserved_values;
  if (riscv_tt_opt_loop_prgm_reclaim > 0)
    {
      for (residency_candidate &c : loop_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : pressure_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : madpair_cands)
	reclaim_reserved_values.safe_push (c.value);
      for (residency_candidate &c : hoistreuse_cands)
	reclaim_reserved_values.safe_push (c.value);
    }
  /* MAD-PAIR candidates stay in discovery order: members of one pair
     are contiguous and must remain so for the all-or-none placement
     sweep below.  */

  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);

  bool changed = false;
  /* One record per peeled loop: the post-peel entry edge (the
     programming point's home), the peel block itself, and the peel's
     original-name -> copy-name map (the park tier's pre-peel placement
     erases a candidate's duplicated materialization through it).  */
  struct peel_record
  {
    edge entry;
    basic_block copy_bb;
    hash_map<tree, tree> names;
  };
  hash_map<class loop *, peel_record *> peeled;
  auto_vec<peel_record *> peel_records;
  /* CC-canonical class: the programming point is the fall-through edge
     of the peeled first iteration (one peel per loop; later candidates
     of the same loop share it).  Peeling happens only after a placement
     decision is final, so refused candidates never mutate the CFG.  */
  auto ensure_peeled = [&] (residency_candidate &c) -> peel_record *
    {
      if (!c.peel)
	return nullptr;
      if (peel_record **found = peeled.get (c.loop))
	{
	  c.entry = (*found)->entry;
	  return *found;
	}
      peel_record *rec = new peel_record;
      rec->entry = peel_first_iteration (c.loop, c.entry, &rec->copy_bb,
					 &rec->names);
      c.entry = rec->entry;
      peeled.put (c.loop, rec);
      peel_records.safe_push (rec);
      return rec;
    };
  /* SFPSTORE sources L0-L11 only (SFPSTORE.md functional model; the
     store-fold pass's SFPSTORE_MAX_SRC_LREG capability fact), so a
     PRGM-parked (L12-L14) store-source constant makes the register
     allocator materialize a per-consumer copy out of the constant file
     at every store -- inside a loop, one SFPMOV word per row (HL-F1,
     the store-source-encoding-ceiling copy tax).  */
  auto store_consumed_p = [] (residency_candidate &c) -> bool
    {
      imm_use_iterator uit;
      gimple *use;
      FOR_EACH_IMM_USE_STMT (use, uit, gimple_call_lhs (c.load))
	{
	  if (is_gimple_debug (use))
	    continue;
	  const rvtt_insn_data *uinsnd = rvtt_get_insn_data (use);
	  if (uinsnd && uinsnd->id == rvtt_insn_data::sfpstore
	      && gimple_call_arg (as_a <gcall *> (use), 1)
		 == gimple_call_lhs (c.load))
	    return true;
	}
      return false;
    };
  auto place = [&] (residency_candidate &c) -> bool
    {
      /* HL (store-sink license composition): under the license token
	 the encoding-ceiling copy tax hands the licensed sink's erased
	 merge word straight back, so the PRGM placement refuses by
	 name for EVERY candidate class and the loop-class fallback
	 lets the LREG tier hoist the materialization whole (the
	 handwritten kernels' own hoisted-value form).  Kept exactly as
	 shipped so licensed codegen stays byte-identical; the general
	 (unlicensed) treatment is the store-source-tier knob at the
	 loop-class placement sweep below.  */
      if (riscv_tt_opt_store_sink > 0 && store_consumed_p (c))
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_STORE_SOURCE_ENCODING_CEILING, dump_file,
			   "const-residency: refused "
			   "(store-source-encoding-ceiling): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      unsigned prgm = 0;
      basic_block prior_bb = nullptr;
      bool prior_reclaimed = false;
      for (prgm_alloc &a : st->allocs)
	if (a.value == c.value)
	  {
	    prgm = a.reg;
	    prior_bb = a.bb;
	    prior_reclaimed = a.reclaimed;
	    break;
	  }
      /* A same-value candidate landing on a DEAD-claim reclaimed slot:
	 the slot has a foreign TU writer of a DIFFERENT value, so the
	 earlier programming's persistence across any interposed call
	 is unprovable -- the candidate must prove its OWN call-free
	 window and always reprograms (idempotent; see below).  */
      if (prgm && prior_reclaimed && !c.call_free_window)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_LOOP_RECLAIM_CALL_WINDOW, dump_file,
			   "const-residency: refused "
			   "(loop-reclaim-call-window: same-value candidate "
			   "on a reclaimed slot without its own proven "
			   "window): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      if (!prgm)
	for (unsigned reg : prgm_regs)
	  if (!(st->claimed & (1u << reg)))
	    {
	      prgm = reg;
	      break;
	    }
      /* TU value-identical reuse: a claimed destination whose EVERY
	 TU write derives to this candidate's exact 32-bit value may
	 be reused.  Soundness is value idempotence, not ordering:
	 every write anywhere stores the same value, and the
	 candidate's own all-lanes programming (still emitted below)
	 puts that value in every lane; any interleaved lane-predicated
	 write of the same value preserves it.  No cross-function
	 ordering or dominance is used.  */
      bool tu_reuse = false;
      if (!prgm)
	{
	  const prgm_tu_facts &tu = tu_prgm_facts ();
	  for (unsigned reg : prgm_regs)
	    if ((tu.value_known & (1u << reg)) && tu.value[reg] == c.value)
	      {
		/* A slot this walk already repurposed for a DIFFERENT
		   value (a DEAD-claim reclaim) is no longer the TU
		   value's home: reusing it would interleave two
		   values' programmings at the same points.  Skip it --
		   the candidate may still reclaim another dead slot
		   below.  */
		bool repurposed = false;
		for (prgm_alloc &a : st->allocs)
		  repurposed |= a.reg == reg && a.value != c.value;
		if (repurposed)
		  continue;
		prgm = reg;
		tu_reuse = true;
		break;
	      }
	}
      /* DEAD-claim reclaim (hoisted-reuse class only): a claimed
	 destination NO statement in the TU ever reads (creg_read
	 no-reader proof) may be reprogrammed with a DIFFERENT value --
	 the existing claim's stores are unobservable -- provided the
	 candidate's own programming-to-readers window is call-free (a
	 callee could otherwise reprogram the slot between this
	 function's programming and its in-loop reads; value-identical
	 reuse above never needs the window).  The tanh-fitted anatomy:
	 the shared op init claims the slots with the HAND polynomial's
	 constants, the fitted kernel needs its own.  */
      bool tu_reclaim = false;
      bool loop_reclaim_cand = (!c.hoisted_reuse && c.loop
				&& riscv_tt_opt_loop_prgm_reclaim > 0);
      if (!prgm
	  && ((c.hoisted_reuse && riscv_tt_opt_hoisted_prgm_reuse > 0)
	      || loop_reclaim_cand))
	{
	  const prgm_tu_facts &tu = tu_prgm_facts ();
	  unsigned dead = 0;
	  for (unsigned reg : prgm_regs)
	    {
	      if (tu.creg_read & (1u << reg))
		continue;
	      if (!(tu.claimed & (1u << reg)))
		continue;
	      /* A slot that is some pending candidate's value-identical
		 home stays available for that (free) reuse -- see the
		 reclaim slot discipline above.  A hoisted-reuse
		 candidate keeps IC's shipped scan (the set is only
		 populated under loop-prgm-reclaim; when both flags run,
		 the discipline protects both classes' reuse).  */
	      if (tu.value_known & (1u << reg))
		{
		  bool reserved = false;
		  for (unsigned rv : reclaim_reserved_values)
		    reserved |= rv == tu.value[reg];
		  if (reserved)
		    continue;
		}
	      bool in_allocs = false;
	      for (prgm_alloc &a : st->allocs)
		in_allocs |= a.reg == reg;
	      if (!in_allocs)
		{
		  dead = reg;
		  break;
		}
	    }
	  if (dead && !c.call_free_window)
	    {
	      if (dump_file)
		{
		  rvtt_refuse_by_name (c.hoisted_reuse
				       ? "hoisted-reuse-call-window"
				       : "loop-reclaim-call-window",
				       dump_file,
				       "const-residency: refused "
				       "(%s): ",
				       c.hoisted_reuse
				       ? "hoisted-reuse-call-window"
				       : "loop-reclaim-call-window");
		  print_gimple_stmt (dump_file, c.load, 0);
		}
	      return false;
	    }
	  if (dead)
	    {
	      prgm = dead;
	      tu_reclaim = true;
	    }
	}
      if (!prgm)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_PRGM_EXHAUSTED, dump_file,
			   "const-residency: refused "
			   "(prgm-exhausted): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      st->claimed |= 1u << prgm;
      if (tu_reuse && dump_file)
	fprintf (dump_file,
		 "const-residency: reusing TU-programmed PRGM L%u (every "
		 "TU write stores 0x%08x; programming is value-idempotent)\n",
		 prgm, c.value);
      if (tu_reclaim && dump_file)
	fprintf (dump_file,
		 "const-residency: reclaiming DEAD-claimed PRGM L%u for "
		 "0x%08x (%s class; no TU reader of the slot; call-free "
		 "programming-to-readers window)\n",
		 prgm, c.value,
		 c.hoisted_reuse ? "hoisted-reuse" : "loop");

      /* CC-canonical class: peel only here, after a register has
	 actually been allocated (see ensure_peeled above).  */
      ensure_peeled (c);

      basic_block point_bb = (c.loop && !c.inplace)
	? c.entry->dest : gimple_bb (c.load);
      /* Reuse without reprogramming needs the earlier programming to
	 provably execute first.  Block dominance is reflexive, and an
	 in-place (pressure class) programming point does NOT dominate
	 later statements of its own block -- so equality must
	 reprogram (same register, same value: always sound, merely
	 redundant).  */
      bool reprogram
	= !prior_bb
	  || prior_reclaimed
	  || point_bb == prior_bb
	  || !dominated_by_p (CDI_DOMINATORS, point_bb, prior_bb);
      tree vec_type = TREE_TYPE (gimple_call_lhs (c.load));
      if (reprogram)
	{
	  gcall *stage = gimple_build_call
	    (xloadi_d->decl, 5, null_pointer_node,
	     build_int_cst (unsigned_type_node, c.value),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (integer_type_node, -32));
	  tree staged = make_ssa_name (vec_type);
	  gimple_call_set_lhs (stage, staged);
	  gcall *wrcfg = gimple_build_call
	    (wrcfg_d->decl, 2, staged,
	     build_int_cst (unsigned_type_node, prgm));
	  if (c.loop && !c.inplace)
	    {
	      basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
	      gimple_stmt_iterator phg = gsi_last_bb (preheader);
	      if (gsi_end_p (phg) || !stmt_ends_bb_p (gsi_stmt (phg)))
		{
		  gsi_insert_after (&phg, wrcfg, GSI_NEW_STMT);
		  gsi_insert_before (&phg, stage, GSI_SAME_STMT);
		}
	      else
		{
		  /* GSI_SAME_STMT keeps the iterator on the block
		     terminator: insert the definition first so the
		     SFPCONFIG lands after its staged operand.  */
		  gsi_insert_before (&phg, stage, GSI_SAME_STMT);
		  gsi_insert_before (&phg, wrcfg, GSI_SAME_STMT);
		}
	    }
	  else
	    {
	      gimple_stmt_iterator lgsi = gsi_for_stmt (c.load);
	      gsi_insert_before (&lgsi, stage, GSI_SAME_STMT);
	      gsi_insert_before (&lgsi, wrcfg, GSI_SAME_STMT);
	    }
	  if (!prior_bb)
	    st->allocs.safe_push (prgm_alloc { c.value, prgm, point_bb,
					       tu_reclaim });
	}
      else if (dump_file)
	fprintf (dump_file,
		 "const-residency: reused PRGM L%u for identical constant "
		 "0x%08x (dominating programming point bb %d)\n",
		 prgm, c.value, prior_bb->index);

      /* The materialization becomes a constant-register read keeping
	 its SSA name: every use follows untouched, and the read
	 expands to a zero-pressure cstlreg unspec.  */
      gimple_stmt_iterator lgsi = gsi_for_stmt (c.load);
      gcall *read = gimple_build_call
	(readlreg_d->decl, 1, build_int_cst (unsigned_type_node, prgm));
      gimple_call_set_lhs (read, gimple_call_lhs (c.load));
      unlink_stmt_vdef (c.load);
      if (tree vdef = gimple_vdef (c.load))
	{
	  gimple_set_vdef (c.load, NULL_TREE);
	  if (TREE_CODE (vdef) == SSA_NAME)
	    release_ssa_name (vdef);
	}
      gsi_replace (&lgsi, read, false);

      if (dump_file && reprogram)
	fprintf (dump_file,
		 "const-residency: allocated PRGM L%u for constant 0x%08x "
		 "(%s class, %u uses, programming point bb %d)\n",
		 prgm, c.value,
		 c.hoisted_reuse ? "hoisted-reuse"
		 : c.inplace ? "madpair" : c.loop ? "loop" : "pressure",
		 c.uses, point_bb->index);
      return true;
    };

  /* PRESSURE-PARK LREG tier: a loop-class candidate the
     programmable constant registers cannot take may still leave the
     loop as a plain LREG live range -- the rename-to-free-LREG
     admission the early invariant pass refuses under its conservative
     in-loop SSA walk (its per-candidate "left in loop by LREG
     pressure" class).  The budget here is the function-wide SSA
     pressure model at this pipeline position -- the CC machinery is
     already lowered to explicit statements, so the RTL-only-temp
     blindness that forced the invariant pass's cc_transients
     surcharge does not exist -- the same model const-remat trusts to
     FIX over-pressure; each committed hoist charges one register
     against it, and exhaustion refuses by name changing nothing.
     Placement soundness is the class's own programming-point proof:
     for the peel class the hoisted SFPLOADI lands after the peeled
     iteration's all-lanes SFPENCC, writing EVERY lane (a superset of
     any consumer mask -- bit-exact on all originally-defined lanes,
     the invariant-pass refinement on originally-indeterminate ones;
     post-CC candidates additionally passed the consumer audit at
     collection); for the plain LOOP class the point is the entry edge
     of a loop no function-local CC write reaches (the cc-region
     filter above), so the preheader lane state is the in-loop lane
     state verbatim.  */
  int lreg_budget = -1;		/* computed lazily on first exhaustion */
  auto lreg_hoist = [&] (residency_candidate &c) -> bool
    {
      if (riscv_tt_opt_pressure_park <= 0 || !c.loop || c.inplace)
	return false;
      if (lreg_budget < 0)
	{
	  lreg_budget = rvtt_pressure_residual (fn);
	}
      if (lreg_budget < 1)
	{
	  if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_LREG_FILE_EXHAUSTED, dump_file,
			   "pressure-park: refused (lreg-file-exhausted): the "
			   "function pressure model leaves no free LREG for "
			   "another loop-wide live range: ");
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	  return false;
	}
      /* The placement arbiter's erfinv relief lever: "price the
	 dst-ownership fold through the pressure-park tier"
	 (post-allocation coalescing was structurally refuted as the
	 alternative relief).  The MARGINAL park --
	 the one about to take the function's last free LREG -- is
	 priced against the downstream identity-reload fold demand
	 whose lreg-pressure-exceeded guard loses to a full file: when
	 the priced fold demand outbids this park's amortized benefit,
	 the park yields by name and the register stays free for the
	 fold's own (unchanged) RTL proof to spend.  Shadow mode dumps
	 the bid comparison and parks exactly as before.  */
      if (lreg_budget == 1
	  && (dump_file || riscv_tt_opt_priced_placement > 0))
	{
	  bool decide = riscv_tt_opt_priced_placement > 0;
	  rvtt_place_weight w = rvtt_place_loop_weight (c.loop);
	  int64_t park_bid
	    = w.proven ? rvtt_place_net_benefit (1, 1, w) : 0;
	  bool outbid
	    = rvtt_place_fold_reserve_outbids (fn, park_bid, w.proven,
					       "park-tier", dump_file,
					       decide);
	  if (decide && outbid)
	    {
	      rvtt_refuse (RVTT_REF_PLACE_FOLD_RESERVE_OUTBID, dump_file,
			   "park-tier: refused (place-fold-reserve-outbid): "
			   "the priced fold demand outbids the marginal "
			   "LREG park: ");
	      if (dump_file)
		print_gimple_stmt (dump_file, c.load, 0);
	      return false;
	    }
	}
      peel_record *rec = ensure_peeled (c);
      /* Pre-peel placement: under the park-ordering regime
	 the deferral moved this candidate from the early invariant
	 hoist to this tier, and the post-peel placement would pay the
	 peel's duplicated materialization on every loop entry.  When
	 the pre-peel ambient is proven all-lanes (block comment at
	 prepeel_ambient_all_lanes_p) the parked definition placed at
	 the HEAD of the peel block writes every lane before the peeled
	 iteration runs -- the same superset-write refinement as the
	 post-peel point -- so the peel's copy of the materialization
	 is erased and its uses redirected.  An unproven ambient keeps
	 the post-peel placement byte-identically (named refusal).  */
      bool prepeel = false;
      if (rec && riscv_tt_opt_park_ordering > 0)
	{
	  if (prepeel_ambient_all_lanes_p (rec->copy_bb))
	    prepeel = true;
	  else if (dump_file)
	    {
	      rvtt_refuse (RVTT_REF_PARK_PREPEEL_AMBIENT_UNPROVEN, dump_file,
			   "pressure-park: pre-peel placement refused "
			   "(park-prepeel-ambient-unproven, peel bb %d); "
			   "keeping the programming-point placement: ",
			   rec->copy_bb->index);
	      print_gimple_stmt (dump_file, c.load, 0);
	    }
	}
      basic_block preheader = prepeel ? rec->copy_bb
	: rvtt_commit_hoist_preheader (c.entry);
      /* Same virtual-operand discipline as the invariant pass's hoist:
	 only a renamed SSA vdef has uses to unlink or a name to
	 release; the pass-level TODO renumbers the rest.  */
      if (tree vdef = gimple_vdef (c.load))
	{
	  if (TREE_CODE (vdef) == SSA_NAME)
	    {
	      unlink_stmt_vdef (c.load);
	      release_ssa_name (vdef);
	    }
	  gimple_set_vdef (c.load, NULL_TREE);
	}
      if (gimple_vuse (c.load))
	{
	  gimple_set_vuse (c.load, NULL_TREE);
	  update_stmt (c.load);
	}
      gimple_stmt_iterator from = gsi_for_stmt (c.load);
      bool erased_copy = false;
      if (prepeel)
	{
	  /* Head of the peel block: the parked definition executes
	     before the peeled iteration under the proven all-lanes
	     ambient.  */
	  gimple_stmt_iterator at = gsi_after_labels (preheader);
	  gimple *load = gsi_stmt (from);
	  gsi_remove (&from, false);
	  if (gsi_end_p (at))
	    gsi_insert_before (&at, load, GSI_NEW_STMT);
	  else
	    gsi_insert_before (&at, load, GSI_SAME_STMT);
	  /* Erase the peel's duplicated materialization: the parked
	     definition dominates the whole peel block and carries the
	     identical constant in every lane the copy wrote (and the
	     superset refinement on the rest).  */
	  tree lhs = gimple_call_lhs (c.load);
	  if (tree *fresh = rec->names.get (lhs))
	    if (*fresh && TREE_CODE (*fresh) == SSA_NAME)
	      {
		gimple *cp = SSA_NAME_DEF_STMT (*fresh);
		if (cp && gimple_bb (cp) == rec->copy_bb)
		  {
		    replace_uses_by (*fresh, lhs);
		    gimple_stmt_iterator cgsi = gsi_for_stmt (cp);
		    gsi_remove (&cgsi, true);
		    release_defs (cp);
		    erased_copy = true;
		  }
	      }
	}
      else
	gsi_move_to_bb_end (&from, preheader);
      --lreg_budget;
      if (dump_file)
	{
	  if (prepeel)
	    fprintf (dump_file,
		     "pressure-park: hoisted invariant materialization to "
		     "a free LREG at the pre-peel entry (peel bb %d; "
		     "ambient all-lanes proven; peel duplicate %s): ",
		     preheader->index,
		     erased_copy ? "erased" : "not-found");
	  else
	    fprintf (dump_file,
		     "pressure-park: hoisted invariant materialization to "
		     "a free LREG at the programming point (preheader "
		     "bb %d): ",
		     preheader->index);
	  print_gimple_stmt (dump_file, c.load, 0);
	}
      return true;
    };

  for (residency_candidate &c : loop_cands)
    {
      /* HL-F1 generalization (-mtt-tensix-optimize-store-source-tier):
	 a store-consumed loop-class constant takes the pressure-park
	 LREG tier INSTEAD of a PRGM park -- the hoisted plain-LREG
	 materialization is SFPSTORE-sourceable, so the per-row SFPMOV
	 copy out of the constant file disappears at zero programming
	 cost (the materialization moves; no SFPCONFIG).  When the tier
	 refuses (lreg-file-exhausted, or without
	 -mtt-tensix-optimize-pressure-park providing the tier) the
	 candidate falls through and KEEPS the established parked
	 placement byte-identically: the one-word-per-row copy never
	 loses to the in-loop rematerialization a bare refusal would
	 leave behind (two words per row for wide constants).  The
	 store-sink license token keeps its own stricter refusal inside
	 place() unchanged (that path must not park even without the
	 tier: the licensed sink's word accounting depends on it).  */
      if (riscv_tt_opt_store_source_tier > 0
	  && riscv_tt_opt_store_sink <= 0
	  && store_consumed_p (c))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "const-residency: store-source-tier "
		       "(store-source-encoding-ceiling): ");
	      print_gimple_stmt (dump_file, c.load, 0);
	      /* ITEM #13 shadow: the LREG-tier-first choice as a bid
		 pair.  The LREG park saves the materialization word
		 AND the per-row copy tax; a PRGM park keeps paying
		 the copy tax and adds the programming pair -- the
		 LREG bid dominates whenever a register is free, which
		 is exactly the established tier order (the arbitrated
		 margin is the fold reserve inside the tier).  */
	      rvtt_place_weight w = rvtt_place_loop_weight (c.loop);
	      if (w.proven)
		fprintf (dump_file,
			 "placement-arbiter: store-source bb %d lreg-park "
			 "bid %" PRId64 " vs prgm-park bid %" PRId64
			 " -> lreg-tier-first\n",
			 gimple_bb (c.load)->index,
			 rvtt_place_net_benefit (2, 1, w),
			 rvtt_place_net_benefit (0, 2, w));
	      else
		fprintf (dump_file,
			 "placement-arbiter: store-source bb %d bids "
			 "unpriceable (trip weight unproven); the "
			 "established tier order stands\n",
			 gimple_bb (c.load)->index);
	    }
	  if (lreg_hoist (c))
	    {
	      changed = true;
	      continue;
	    }
	}
      changed |= place (c) || lreg_hoist (c);
    }

  /* MAD-PAIR groups place all-or-none: a half-claimed pair pays its
     programming word while the surviving immediate fold still blocks
     the mad rule -- a pure loss.  Simulate place()'s register
     selection (alloc value-dedup, then free-slot scan, then TU
     value-identical reuse -- the same order) for the whole group
     before editing anything.  */
  for (unsigned gx = 0; gx < madpair_cands.length (); )
    {
      unsigned group = madpair_cands[gx].group;
      unsigned gend = gx;
      while (gend < madpair_cands.length ()
	     && madpair_cands[gend].group == group)
	++gend;
      bool ok = !invalid_madpair_groups.contains (group);
      if (ok)
	{
	  unsigned sim = st->claimed;
	  auto_vec<unsigned, 3> sim_vals;
	  for (unsigned ix = gx; ok && ix != gend; ++ix)
	    {
	      unsigned value = madpair_cands[ix].value;
	      bool have = false;
	      for (prgm_alloc &a : st->allocs)
		if (a.value == value)
		  {
		    have = true;
		    break;
		  }
	      for (unsigned v : sim_vals)
		have |= v == value;
	      if (have)
		continue;
	      unsigned reg = 0;
	      for (unsigned r : prgm_regs)
		if (!(sim & (1u << r)))
		  {
		    reg = r;
		    break;
		  }
	      if (!reg)
		{
		  const prgm_tu_facts &tu = tu_prgm_facts ();
		  for (unsigned r : prgm_regs)
		    if ((tu.value_known & (1u << r)) && tu.value[r] == value)
		      {
			reg = r;
			break;
		      }
		}
	      if (!reg)
		ok = false;
	      else
		{
		  sim |= 1u << reg;
		  sim_vals.safe_push (value);
		}
	    }
	  if (!ok && dump_file)
	    rvtt_refuse (RVTT_REF_MADPAIR_PRGM_EXHAUSTED, dump_file,
			 "const-residency: madpair group refused "
			 "(madpair-prgm-exhausted): the pair needs more PRGM "
			 "registers than remain free\n");
	}
      if (ok)
	for (unsigned ix = gx; ix != gend; ++ix)
	  changed |= place (madpair_cands[ix]);
      gx = gend;
    }

  /* HOISTED-REUSE class: place through the shared machinery (free
     slot, alloc value-dedup, or TU value-identical reuse -- place()'s
     own order); a refused candidate keeps the hoisted LREG placement
     byte-identically.  */
  for (residency_candidate &c : hoistreuse_cands)
    changed |= place (c);

  for (residency_candidate &c : pressure_cands)
    changed |= place (c);

  /* MERGE-RENAME placement (block comment at
     merge_rename_cand): runs after every established class has placed,
     so the rename can only consume placement capacity the reviewed
     classes left behind.  */
  bool merge_writers_collected = false;
  bool merge_probed = false;
  auto_vec<gimple *> merge_cc_writers;
  for (merge_rename_cand &m : merge_cands)
    {
      if (riscv_tt_merge_rename_allow_neutral <= 0)
	{
	  rvtt_refuse (RVTT_REF_MERGE_RENAME_WORD_NEUTRAL, dump_file,
		       "merge-rename: refused (merge-rename-word-neutral: "
		       "the single-issue CC-merge is one in-loop word "
		       "before and after the rename while the parked twin "
		       "adds its materialization word -- the priced "
		       "delivery benefit is identically negative): ");
	  if (dump_file)
	    print_gimple_stmt (dump_file, m.tail, 0);
	  continue;
	}
      /* The twin's all-lanes proof: the peel and cc-lifted classes
	 carry their own (the peel's post-ENCC / pre-peel-ambient
	 points, the lift's no-CC-reaches-entry admission proof); a
	 plain loop-class candidate re-proves that no function-local CC
	 write reaches its programming point (the lifted-entry proof's
	 own check), else the parked twin's disabled-lane content would
	 be unproven at the merge.  */
      if (!m.peel && !m.cc_lifted)
	{
	  if (!merge_writers_collected)
	    {
	      collect_cc_writers (fn, &merge_cc_writers);
	      merge_writers_collected = true;
	    }
	  if (cc_write_reaches_point_p (merge_cc_writers,
					m.entry->src, nullptr))
	    {
	      rvtt_refuse (RVTT_REF_MERGE_RENAME_AMBIENT_UNPROVEN, dump_file,
			   "merge-rename: refused (merge-rename-ambient-"
			   "unproven: a function-local CC write reaches the "
			   "plain loop programming point): ");
	      if (dump_file)
		print_gimple_stmt (dump_file, m.tail, 0);
	      continue;
	    }
	}
      gcc_assert (gimple_call_num_args (m.tail) == 6);
      /* Full-lane twin of the merge's immediate: the _lv insn's
	 non-live sibling, scalar arguments verbatim.  Inserted before
	 the merge, then offered to the LREG tier BEFORE the merge is
	 touched -- a refused placement erases only the fresh twin, so
	 the fail-closed leg is the flag-off IR verbatim (virtual
	 operands included).  */
      const rvtt_insn_data *lvd = rvtt_get_insn_data (m.tail);
      const rvtt_insn_data *twin_d = lvd->get_non_live ();
      gcc_assert (twin_d && twin_d->decl
		  && twin_d->id == rvtt_insn_data::sfploadi);
      gcall *t_stmt = gimple_build_call
	(twin_d->decl, 5, gimple_call_arg (m.tail, 0),
	 gimple_call_arg (m.tail, 2), gimple_call_arg (m.tail, 3),
	 gimple_call_arg (m.tail, 4), gimple_call_arg (m.tail, 5));
      tree lhs = gimple_call_lhs (m.tail);
      tree t = make_ssa_name (TREE_TYPE (lhs));
      gimple_call_set_lhs (t_stmt, t);
      gimple_set_location (t_stmt, gimple_location (m.tail));
      gimple_stmt_iterator tgsi = gsi_for_stmt (m.tail);
      gsi_insert_before (&tgsi, t_stmt, GSI_SAME_STMT);
      /* The insertion marks virtual-operand renaming; even an undone
	 probe must surface the pass-level TODO (the IL stays
	 byte-identical, the virtual web is recomputed).  */
      merge_probed = true;

      residency_candidate c;
      c.load = t_stmt;
      c.value = m.value;
      c.loop = m.loop;
      c.entry = m.entry;
      c.uses = 1;
      c.peel = m.peel;
      c.cc_lifted = m.cc_lifted;
      if (!lreg_hoist (c))
	{
	  /* Fail-closed: erase the fresh twin; the merge was never
	     touched.  */
	  gimple_stmt_iterator dgsi = gsi_for_stmt (t_stmt);
	  gsi_remove (&dgsi, true);
	  release_defs (t_stmt);
	  rvtt_refuse (RVTT_REF_MERGE_RENAME_PLACEMENT_REFUSED, dump_file,
		       "merge-rename: refused (merge-rename-placement-"
		       "refused: the LREG tier could not place the "
		       "full-lane twin; rename not performed): ");
	  if (dump_file)
	    print_gimple_stmt (dump_file, m.tail, 0);
	  continue;
	}

      /* Placement committed: rewrite the merge to the register-source
	 predicated move.  Virtual-operand discipline as lreg_hoist's
	 (the merge's conservative call clobber is unlinked; the
	 pass-level TODO renumbers the rest).  */
      if (tree vdef = gimple_vdef (m.tail))
	{
	  if (TREE_CODE (vdef) == SSA_NAME)
	    {
	      unlink_stmt_vdef (m.tail);
	      release_ssa_name (vdef);
	    }
	  gimple_set_vdef (m.tail, NULL_TREE);
	}
      if (gimple_vuse (m.tail))
	{
	  gimple_set_vuse (m.tail, NULL_TREE);
	  update_stmt (m.tail);
	}
      const rvtt_insn_data *asg_d
	= rvtt_get_insn_data (rvtt_insn_data::sfpassign_lv);
      gcc_assert (asg_d && asg_d->decl);
      gcall *merge = gimple_build_call (asg_d->decl, 2, m.link, t);
      gimple_call_set_lhs (merge, lhs);
      gimple_set_location (merge, gimple_location (m.tail));
      gimple_stmt_iterator mgsi = gsi_for_stmt (m.tail);
      gsi_replace (&mgsi, merge, false);
      changed = true;

      /* The peel's duplicate of the twin -- created only when the peel
	 was manufactured AFTER the twin's insertion (this candidate's
	 own ensure_peeled) -- has no reader: the peel iteration keeps
	 its original immediate merge verbatim.  Erase the dead word
	 (the prepeel path's own duplicate-erase discipline; a peel
	 record predating the twin has no mapping and this is a
	 no-op).  */
      if (peel_record **rec = peeled.get (m.loop))
	if (tree *fresh = (*rec)->names.get (t))
	  if (*fresh && TREE_CODE (*fresh) == SSA_NAME
	      && has_zero_uses (*fresh))
	    {
	      gimple *cp = SSA_NAME_DEF_STMT (*fresh);
	      if (cp && gimple_bb (cp) == (*rec)->copy_bb)
		{
		  gimple_stmt_iterator cgsi = gsi_for_stmt (cp);
		  gsi_remove (&cgsi, true);
		  release_defs (cp);
		}
	    }
      if (dump_file)
	{
	  fprintf (dump_file,
		   "merge-rename: renamed in-loop immediate CC-merge "
		   "0x%08x to a parked full-lane twin + register-source "
		   "merge: ", m.value);
	  print_gimple_stmt (dump_file, merge, 0);
	}
      if (flag_checking)
	{
	  /* Shadow re-verification (the KH pattern, live under
	     -fchecking in every build): the widened admission
	     re-proven from the committed IR before belief.  */
	  basic_block tbb = gimple_bb (t_stmt);
	  basic_block mbb = gimple_bb (merge);
	  gcc_assert (tbb && mbb
		      && !flow_bb_inside_loop_p (m.loop, tbb)
		      && dominated_by_p (CDI_DOMINATORS, mbb, tbb));
	  gcc_assert (gimple_call_lhs (merge) == lhs
		      && gimple_call_arg (merge, 0) == m.link
		      && gimple_call_arg (merge, 1) == t);
	  gcc_assert (single_nondebug_use_p (t, merge));
	  unsigned tvalue;
	  gcc_assert (single_issue_constant_image_p
		      (as_a <gcall *> (SSA_NAME_DEF_STMT (t)), &tvalue)
		      && tvalue == m.value);
	}
    }

  for (peel_record *rec : peel_records)
    delete rec;
  delete arb_model;
  /* An undone merge-rename probe left the IL byte-identical but marked
     virtual-operand renaming; surface the pass TODO either way.  */
  return changed || merge_probed;
}
