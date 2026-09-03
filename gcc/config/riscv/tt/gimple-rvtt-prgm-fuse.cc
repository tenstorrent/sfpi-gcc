/* PRGM constant programming: the fusion-class transform
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

/* The M3 fusion-class transform of the PRGM constant pass
   (-mtt-tensix-optimize-prgm-const): collect fusion-enabling
   immediate candidates from loops, gate them on the TU freedom
   proof and the cc-reach all-lanes proof, program each constant
   into a free PRGM register on the loop entry edge and rewrite the
   candidate to read it back.  Split from gimple-rvtt-prgm-const.cc;
   the algorithm essay lives there.  */

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
/* Per-function transform.					      */

/* Whether ADDR, an SFPU builtin's instruction-buffer operand, is one
   of the two canonical spellings: literal zero or the address of the
   public external `__instrn_buffer' declaration.  */

static bool
canonical_buffer_arg_p (tree addr)
{
  if (integer_zerop (addr))
    return true;
  STRIP_NOPS (addr);
  if (TREE_CODE (addr) != ADDR_EXPR)
    return false;
  tree decl = TREE_OPERAND (addr, 0);
  return VAR_P (decl)
    && DECL_EXTERNAL (decl)
    && TREE_PUBLIC (decl)
    && DECL_ASSEMBLER_NAME (decl)
    && !strcmp (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)),
		"__instrn_buffer");
}

/* The fusion-enabling candidate: LHS = sfpaddi (buf, MUL, imm, 0, 0, 0)
   where MUL = sfpmul (a, b, 0) in the same loop with the addi as its
   only use.  */

struct candidate
{
  gcall *addi;			/* the SFPADDI or SFPADD to rewrite */
  gcall *mul;
  gcall *loadi;			/* in-loop materialization, or null for
				   the immediate SFPADDI shape */
  unsigned value;		/* fp32 bits of the constant */
  class loop *loop;
  edge entry;
};

/* Whether EXPECTED is the only non-debug statement using the SSA name
   VALUE (several operands of EXPECTED may read it).  False when VALUE
   has no non-debug use at all.  */

bool
single_nondebug_use_p (tree value, gimple *expected)
{
  imm_use_iterator iter;
  use_operand_p use_p;
  gimple *seen = nullptr;
  FOR_EACH_IMM_USE_FAST (use_p, iter, value)
    {
      gimple *use = USE_STMT (use_p);
      if (is_gimple_debug (use))
	continue;
      if (seen && use != seen)
	return false;
      seen = use;
    }
  return seen == expected;
}

/* A single-use in-loop SFPMUL with the plain mod, defining SRC.  */

static gcall *
fusable_mul_p (tree src, class loop *loop, gimple *only_use)
{
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *mul = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!mul)
    return nullptr;
  const rvtt_insn_data *muld = rvtt_get_insn_data (mul);
  if (!muld || muld->id != rvtt_insn_data::sfpmul
      || !integer_zerop (gimple_call_arg (mul, 2))
      || !gimple_bb (mul)
      || !flow_bb_inside_loop_p (loop, gimple_bb (mul))
      || !single_nondebug_use_p (src, only_use))
    return nullptr;
  return mul;
}

/* The first VALUE operand position of a MAD-PAIR member call: the
   lane-carrier _lv spellings carry the carrier vector in argument 0,
   shifting both value operands and the mod one position right.  */

unsigned
madpair_value_base (const rvtt_insn_data *insnd)
{
  return (insnd->id == rvtt_insn_data::sfpmul_lv
	  || insnd->id == rvtt_insn_data::sfpadd_lv) ? 1 : 0;
}

/* MAD-PAIR discovery vocabulary
   (-mtt-tensix-optimize-madpair-vocabulary): the downstream combine
   fuses the mul+add pair through spellings the base discovery does not
   walk -- the lane-carrier _lv forms of the members (the muli/addi
   immediate folds match those spellings too, so the fold decay exists
   there identically) and a single-use SFPMOV complement wrapper
   between the mul and the add (the -a+b rewrite reduces it before the
   mad rule fires).  The vocabulary itself is answered by
   rvtt_combine_will_fuse_p from the combiner's own GENERATED tables
   (rvtt-combine.inc <- rvtt.gc, the generated verdict tables) -- the hand
   mirror of the spellings is deleted, so discovery/combine drift is
   impossible by construction and every future rvtt.gc widening reaches
   the discovery automatically.  Everything else stays the reviewed
   MAD-PAIR class here: candidate admission (plain mod, in-loop,
   single-use), refusal names, grouping, the cc-reach proof and
   pricing.  Flag off delegates to the base single-spelling test
   byte-identically.  *VALUE_BASE reports the returned mul's first
   value-operand position for the caller's constant classification.  */

gcall *
madpair_vocab_mul_p (tree src, class loop *loop, gimple *only_use,
		     unsigned *value_base)
{
  *value_base = 0;
  if (riscv_tt_opt_madpair_vocabulary <= 0)
    return fusable_mul_p (src, loop, only_use);
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!def)
    return nullptr;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return nullptr;
  /* One complement wrapper: -(a*b) + c.  The wrapper spelling and its
     constant mod are the -a+b reduction rules' feed shape, queried
     from the generated tables.  The wrapper must be the add's single
     feed and the mul must die into the wrapper, mirroring the
     single-use discipline of the -a+b and mad rules.  */
  bool wrapper = rvtt_combine_will_fuse_p (def, rvtt_insn_data::sfpmov_lv,
					   rvtt_insn_data::sfpadd_lv);
  if (wrapper)
    {
      unsigned base = insnd->is_live () ? 1 : 0;
      if (!gimple_bb (def)
	  || !flow_bb_inside_loop_p (loop, gimple_bb (def))
	  || !single_nondebug_use_p (src, only_use))
	return nullptr;
      src = gimple_call_arg (def, base);
      only_use = def;
      if (TREE_CODE (src) != SSA_NAME)
	return nullptr;
      def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
      if (!def)
	return nullptr;
      insnd = rvtt_get_insn_data (def);
      if (!insnd)
	return nullptr;
    }
  /* The mul spellings the mad rules consume, from the same tables.  */
  bool mul_spelling = rvtt_combine_will_fuse_p (def,
						rvtt_insn_data::sfpmul_lv,
						rvtt_insn_data::sfpadd_lv);
  if (!mul_spelling)
    return nullptr;
  unsigned base = madpair_value_base (insnd);
  if (!integer_zerop (gimple_call_arg (def, base + 2))
      || !gimple_bb (def)
      || !flow_bb_inside_loop_p (loop, gimple_bb (def))
      || !single_nondebug_use_p (src, only_use))
    return nullptr;
  *value_base = base;
  return def;
}

/* An in-loop invariant constant materialization defining SRC whose
   full 32-bit lane image is recoverable through the audited
   single-issue-chain derivation (single_issue_constant_image_p below:
   the sfpxloadi 31/32/-32 verbatim-image forms and the shortened
   SFPLOADI FLOATB form -- the same recovery the residency classes
   use).  Other encodings refuse (their value reconstruction is not on
   record).  */

bool single_issue_constant_image_p (gcall *load, unsigned *value);

static gcall *
invariant_float_load_p (tree src, class loop *loop, gimple *only_use,
			unsigned *value)
{
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *load = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!load
      || !gimple_bb (load)
      || !flow_bb_inside_loop_p (loop, gimple_bb (load))
      || !rvtt_invariant_constant_load_p (load, loop,
					  /*allow_shortened=*/true)
      || !single_nondebug_use_p (src, only_use)
      || !single_issue_constant_image_p (load, value))
    return nullptr;
  return load;
}

/* Recognize CALL inside LOOP as one of the two fusion-enabling
   shapes -- the immediate SFPADDI form or the materialized SFPADD
   form (struct candidate above) -- and fill *OUT except for the entry
   edge, which the caller places.  Returns true on a match.  */

static bool
fusion_candidate_p (gcall *call, class loop *loop, candidate *out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return false;

  if (insnd->id == rvtt_insn_data::sfpaddi)
    {
      /* The immediate shape: LHS = sfpaddi (buf, MUL, imm, 0, 0, 0).  */
      if (!gimple_call_lhs (call)
	  || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
	  || !canonical_buffer_arg_p (gimple_call_arg (call, 0)))
	return false;
      for (unsigned ix = 2; ix != gimple_call_num_args (call); ++ix)
	if (TREE_CODE (gimple_call_arg (call, ix)) != INTEGER_CST)
	  return false;
      /* Plain-add form only: synthesized id/var fields and mod all
	 zero.  */
      for (unsigned ix = 3; ix != gimple_call_num_args (call); ++ix)
	if (!integer_zerop (gimple_call_arg (call, ix)))
	  return false;

      gcall *mul = fusable_mul_p (gimple_call_arg (call, 1), loop, call);
      if (!mul)
	return false;
      out->addi = call;
      out->mul = mul;
      out->loadi = nullptr;
      out->value
	= (TREE_INT_CST_LOW (gimple_call_arg (call, 2)) & 0xffff) << 16;
      out->loop = loop;
      return true;
    }

  if (insnd->id == rvtt_insn_data::sfpadd)
    {
      /* The materialized shape the pressure refusal actually leaves in
	 a loop: LHS = sfpadd (MUL, LOADI, 0) (either operand order),
	 with the in-loop invariant materialization feeding only the
	 add.  */
      if (!gimple_call_lhs (call)
	  || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
	  || !integer_zerop (gimple_call_arg (call, 2)))
	return false;
      for (unsigned swap = 0; swap != 2; ++swap)
	{
	  tree mul_op = gimple_call_arg (call, swap);
	  tree load_op = gimple_call_arg (call, 1 - swap);
	  gcall *mul = fusable_mul_p (mul_op, loop, call);
	  unsigned value;
	  gcall *load = mul
	    ? invariant_float_load_p (load_op, loop, call, &value) : nullptr;
	  if (mul && load)
	    {
	      out->addi = call;
	      out->mul = mul;
	      out->loadi = load;
	      out->value = value;
	      out->loop = loop;
	      return true;
	    }
	}
      return false;
    }

  return false;
}

/* The fused-MAD admission -- RECOGNITION-ONLY.
   This arm matches an sfpmad the front-end ALREADY emitted; it never
   forms one.  Fusing an unfused MUL+ADD into a MAD collapses two
   roundings into one and is bit-changing on any Horner step, so no
   arm of this pass may perform that rewrite: the only transformation
   here is re-sourcing one operand of the EXISTING statement (whether
   the final code is fused is decided by the pre-existing downstream
   mul+add->mad combine identically in the fired and unfired legs).
   LHS = sfpmad (A, B, C, 0) computes per-lane A*B + C from its operand
   VALUES alone -- the plain mod has no implicit register pairing or
   operand reinterpretation -- so an operand defined by an in-loop
   invariant single-issue constant materialization used only by this
   statement can be parked in a PRGM register and read back: the
   constant-register read yields the identical 32-bit image in every
   lane the materialization wrote (the all-lanes proof for both is the
   same cc-region proof every class passes).  Each qualifying operand
   is its own candidate (the sdpa exp leg carries two).  Non-plain mods
   refuse by name -- their operand semantics are not audited here; the
   _lv variant is excluded (its lane-victim operand is not value-only).
   Like the materialized SFPADD shape, no trip proof is required: the
   entry-edge programming is never speculated and
   establishment/no-clobber is trip-independent.  Appends candidates
   (without entry edges -- the caller places them) and returns how
   many.  */

static unsigned
mad_operand_candidates (gcall *call, class loop *loop,
			auto_vec<candidate> *out)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd || insnd->id != rvtt_insn_data::sfpmad)
    return 0;
  if (!gimple_call_lhs (call)
      || TREE_CODE (gimple_call_lhs (call)) != SSA_NAME
      || gimple_call_num_args (call) != 4)
    return 0;

  gcall *loads[3] = { nullptr, nullptr, nullptr };
  unsigned values[3] = { 0, 0, 0 };
  bool any = false;
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      loads[ix] = invariant_float_load_p (gimple_call_arg (call, ix), loop,
					  call, &values[ix]);
      any |= loads[ix] != nullptr;
    }
  if (!any)
    return 0;

  tree mod = gimple_call_arg (call, 3);
  if (TREE_CODE (mod) != INTEGER_CST || !integer_zerop (mod))
    {
      rvtt_refuse (RVTT_REF_MAD_MOD_UNPROVEN, dump_file,
		   "prgm-const: sfpmad refused (mad-mod-unproven): a non-plain "
		   "mod's operand semantics are not audited here\n");
      return 0;
    }

  unsigned n = 0;
  for (unsigned ix = 0; ix != 3; ++ix)
    {
      if (!loads[ix])
	continue;
      /* One candidate per materialization: mad (x, k, k) carries the
	 same load in two operand slots.  */
      bool dup = false;
      for (unsigned jx = 0; jx != ix; ++jx)
	dup |= loads[jx] == loads[ix];
      if (dup)
	continue;
      candidate c;
      c.addi = call;
      c.mul = nullptr;
      c.loadi = loads[ix];
      c.value = values[ix];
      c.loop = loop;
      c.entry = nullptr;
      out->safe_push (c);
      ++n;
    }
  return n;
}

/* The hoisted mad-pair operand: a constant
   materialization defining SRC that sits OUTSIDE LOOP (the invariant
   pass's cc-restore-discharged hoist parks loop constants in the
   preheader) with ONLY_USE as its single non-debug consumer inside the
   loop, and whose full 32-bit lane image is recoverable through the
   audited single-issue derivation.  *VULNERABLE reports whether the
   materialization is the shortened SFPLOADI FLOATB form -- the exact
   shape the downstream muli/addi immediate folds match ("in preference
   to mul,add->mad"): folding rewrites the pair's add (or mul) into its
   immediate form and the mad combine can no longer fuse, decaying a
   one-word MAD row to a two-word MUL+ADDI row every iteration.  The
   sfpxloadi 31/32/-32 forms are NOT vulnerable (the folds match only
   the SFPLOADI insn) and need no re-claim: the mad rule fuses register
   operands wherever they were materialized.  */

gcall *
hoisted_madpair_load_p (tree src, class loop *loop, gimple *only_use,
			unsigned *value, bool *vulnerable, bool *shared)
{
  if (TREE_CODE (src) != SSA_NAME)
    return nullptr;
  gcall *load = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (src));
  if (!load
      || !gimple_bb (load)
      || flow_bb_inside_loop_p (loop, gimple_bb (load))
      || !rvtt_invariant_constant_load_p (load, loop,
					  /*allow_shortened=*/true)
      || !single_issue_constant_image_p (load, value))
    return nullptr;
  /* Fold-vulnerable = the materialization's spelling is one the
     downstream muli/addi immediate folds match, answered from the
     combiner's GENERATED tables (the a{*,+}fp16b rules' sfploadi feed)
     instead of a hand insn-id mirror: the
     fold consumes exactly the shortened-SFPLOADI shape feeding this
     pair member's spelling.  The sfpxloadi verbatim-image forms match
     no fold row and need no re-claim.  */
  *vulnerable = false;
  if (const rvtt_insn_data *used = rvtt_get_insn_data (only_use))
    {
      rvtt_insn_data::insn_id consumer
	= (used->is_live () || !used->get_live ())
	  ? used->id : used->get_live ()->id;
      *vulnerable
	= rvtt_combine_will_fuse_p (load, rvtt_insn_data::sfploadi,
				    consumer);
    }
  /* A materialization with consumers beyond the pair statement cannot
     be re-claimed here: the constant-register substitution would reach
     positions this class has not audited.  The caller refuses the
     whole pair when such a constant is also fold-vulnerable (the
     immediate fold fires on it regardless of our other claims).  */
  *shared = !single_nondebug_use_p (src, only_use);
  return load;
}

/* Every CC-writing statement in FN, collected once per function.  */

void
collect_cc_writers (function *fn, auto_vec<gimple *> *out)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (gsi_stmt (gsi));
	if (insnd && insnd->sets_cc (as_a <gcall *> (gsi_stmt (gsi))))
	  out->safe_push (gsi_stmt (gsi));
      }
}

/* Whether any CC writer in WRITERS can execute before the programming
   point (POINT_BB, and POINT_STMT within it when the point is a
   statement rather than the block entry).  The all-lanes proof needs
   the function-entry lane state to reach the point on every path; a
   fn-local CC writer defeats it exactly when some CFG path runs the
   writer and then reaches the point.  Reachability is block-granular
   (fail-closed over-approximation of "can execute before"): the reach
   set is computed backwards from POINT_BB's predecessors, so POINT_BB
   itself is in the set only when it lies on a cycle; a writer in
   POINT_BB outside any cycle defeats the proof exactly when it
   precedes POINT_STMT in the block (a block-entry point is defeated by
   any writer in the block).  Everything else about the proof -- the
   fn-entry-all-lanes model and call transparency -- is unchanged from
   the function-granular version.  */

bool
cc_write_reaches_point_p (const auto_vec<gimple *> &writers,
			  basic_block point_bb, gimple *point_stmt)
{
  if (writers.is_empty ())
    return false;

  hash_set<basic_block> reach;
  auto_vec<basic_block, 16> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, point_bb->preds)
    work.safe_push (e->src);
  while (!work.is_empty ())
    {
      basic_block b = work.pop ();
      if (reach.add (b))
	continue;
      FOR_EACH_EDGE (e, ei, b->preds)
	work.safe_push (e->src);
    }

  for (gimple *w : writers)
    {
      basic_block wbb = gimple_bb (w);
      if (!wbb)
	continue;
      if (reach.contains (wbb))
	return true;
      if (wbb == point_bb)
	{
	  /* POINT_BB is not on a cycle (the reach test above would have
	     caught it): the writer executes before the point exactly
	     when it textually precedes it.  */
	  if (!point_stmt)
	    return true;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (wbb);
	       !gsi_end_p (gsi); gsi_next (&gsi))
	    {
	      if (gsi_stmt (gsi) == w)
		return true;
	      if (gsi_stmt (gsi) == point_stmt)
		break;
	    }
	}
    }
  return false;
}

/* The M3 fusion-class transform over FN.  Collect the fusion-enabling
   candidates from FN's loops, gate them on the TU freedom proof and
   the cc-reach all-lanes proof, then for each allocate a free PRGM
   register (identical fp32 values share one; a dominating earlier
   programming is not repeated), program the constant on the loop
   entry edge (staging SFPLOADI + SFPCONFIG) and rewrite the candidate
   to read the constant register.  ST carries the SFPCONFIG claims and
   the value-allocation table across this function's classes.  Returns
   whether the IL changed; refusals leave it untouched.  */

bool
transform (function *fn, prgm_state *st)
{
  if (!dom_info_available_p (CDI_DOMINATORS))
    calculate_dominance_info (CDI_DOMINATORS);

  auto_vec<candidate> candidates;
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (!loop->num)
	continue;
      /* No zero-trip proof is needed at this late pipeline position:
	 the programming point sits on the loop entry edge, whose
	 destination is the loop header, so control reaching it executes
	 the header (and every candidate block, by the
	 executes-every-entered-iteration proof below) at least once --
	 the SFPCONFIG write is never speculated relative to the loop.
	 (The invariant pass's first-header-test fold targets the
	 pre-rotation shape and cannot see the rotated do-while form
	 this pass runs on.)  */
      edge entry = rvtt_loop_entry_edge (loop);
      const char *why
	= !entry ? "no-single-entry"
	: rvtt_loop_hoist_region_opaque_p (loop, entry) ? "opaque-hoist-region"
	: rvtt_preheader_insertion_blocked_p (entry) ? "preheader-blocked"
	: rvtt_loop_has_sfpu_barrier_p (loop) ? "sfpu-barrier"
	: nullptr;
      if (why)
	{
	  rvtt_refuse_by_name (why, dump_file,
			       "prgm-const: loop bb %d refused (%s)\n",
			       loop->header->index, why);
	  continue;
	}

      basic_block *body = get_loop_body_in_dom_order (loop);
      for (unsigned ix = 0; ix != loop->num_nodes; ++ix)
	{
	  basic_block bb = body[ix];
	  if (bb->loop_father != loop
	      || !rvtt_stmt_executes_every_entered_iteration_p (loop, bb))
	    continue;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      if (!is_a <gcall *> (gsi_stmt (gsi)))
		continue;
	      gcall *call = as_a <gcall *> (gsi_stmt (gsi));
	      candidate c;
	      unsigned pushed = 0;
	      if (fusion_candidate_p (call, loop, &c))
		{
		  candidates.safe_push (c);
		  pushed = 1;
		}
	      else
		pushed = mad_operand_candidates (call, loop, &candidates);
	      if (!pushed)
		continue;
	      /* Under the cross-loop hoist, lift the programming
		 point to the outermost enclosing entry edge whose
		 region is audited-inert for the whole LREG file
		 (allocatable staging register plus the
		 programmable-constant destinations); the walk
		 returns ENTRY unchanged when nothing is proven.  */
	      edge point = riscv_tt_opt_crossloop_hoist > 0
		? rvtt_crossloop_outermost_entry (loop, entry, 0x7fff)
		: entry;
	      for (unsigned kx = candidates.length () - pushed;
		   kx != candidates.length (); ++kx)
		candidates[kx].entry = point;
	    }
	}
      free (body);
    }

  if (candidates.is_empty ())
    return false;

  /* The freedom proof gates every allocation.  */
  const prgm_tu_facts &facts = tu_prgm_facts ();
  if (facts.refused)
    {
      rvtt_refuse (RVTT_REF_OPAQUE_REGION_UNDECLARED, dump_file,
		   "prgm-const: refused (opaque-region-undeclared): %s\n",
		   facts.reason);
      return false;
    }

  /* The all-lanes proof, scoped by reachability: a candidate refuses
     exactly when some fn-local CC writer can execute before its
     programming point.  One reach set from the candidate loop's header
     covers the (possibly cross-loop hoisted) entry-edge point and every
     block between it and the loop: the point can reach the header, so
     each of the point's CFG ancestors is an ancestor of the header
     too.  */
  {
    auto_vec<gimple *> cc_writers;
    collect_cc_writers (fn, &cc_writers);
    if (!cc_writers.is_empty ())
      {
	unsigned kept = 0;
	for (candidate &c : candidates)
	  {
	    if (cc_write_reaches_point_p (cc_writers, c.loop->header,
					  nullptr))
	      {
		rvtt_refuse (RVTT_REF_CC_REGION_UNPROVEN, dump_file,
			     "prgm-const: loop bb %d refused "
			     "(cc-region-unproven): a CC write reaches the "
			     "programming point\n", c.loop->header->index);
		continue;
	      }
	    candidates[kept++] = c;
	  }
	candidates.truncate (kept);
	if (candidates.is_empty ())
	  return false;
      }
  }

  if (!st->initialized)
    {
      st->claimed = facts.claimed;
      st->initialized = true;
    }
  unsigned &claimed = st->claimed;
  bool changed = false;
  const rvtt_insn_data *xloadi_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpxloadi);
  const rvtt_insn_data *wrcfg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpwriteconfig_v);
  const rvtt_insn_data *readlreg_d
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  const rvtt_insn_data *add_d = rvtt_get_insn_data (rvtt_insn_data::sfpadd);

  /* Identical-immediate reuse: an earlier allocation of the SAME fp32
     value shares its PRGM register (the register is claimed by us and
     every programming write stores the same constant, so reuse is
     order-insensitive); when the earlier allocation's LOOP HEADER
     moreover DOMINATES the new loop's entry, the earlier programming
     provably executed first (any path to a loop header passes its
     entry edge by induction over the backedge) and no second
     programming write is emitted.  (The sdpa shape: three inlined exp
     bodies used to burn L12+L13+L14 on one immediate.)  Candidates
     are visited in block order so the dominating allocation is seen
     first.  */
  auto_vec<prgm_alloc, 4> &allocs = st->allocs;

  /* Sort candidates by the function's block order (an approximation
     of program order; correctness never depends on it -- the
     dominance test does the proving).  */
  {
    hash_map<basic_block, int> seq;
    int n = 0;
    basic_block obb;
    FOR_EACH_BB_FN (obb, fn)
      seq.put (obb, n++);
    auto key = [&seq] (const candidate &c) -> int
      {
	int *p = seq.get (c.entry->dest);
	return p ? *p : INT_MAX;
      };
    for (unsigned i = 1; i < candidates.length (); ++i)
      for (unsigned j = i; j > 0 && key (candidates[j - 1])
					> key (candidates[j]); --j)
	std::swap (candidates[j - 1], candidates[j]);
  }

  for (candidate &c : candidates)
    {
      unsigned prgm = 0;
      basic_block prior_bb = nullptr;
      for (prgm_alloc &a : allocs)
	if (a.value == c.value)
	  {
	    prgm = a.reg;
	    prior_bb = a.bb;
	    break;
	  }
      if (!prgm)
	for (unsigned reg : prgm_regs)
	  if (!(claimed & (1u << reg)))
	    {
	      prgm = reg;
	      break;
	    }
      if (!prgm)
	{
	  rvtt_refuse (RVTT_REF_PRGM_EXHAUSTED, dump_file,
		       "prgm-const: refused (prgm-exhausted): ");
	  if (dump_file)
	    print_gimple_stmt (dump_file, c.addi, 0);
	  continue;
	}
      claimed |= 1u << prgm;

      tree vec_type = TREE_TYPE (gimple_call_lhs (c.addi));
      bool reprogram
	= !prior_bb
	  || !dominated_by_p (CDI_DOMINATORS, c.entry->dest, prior_bb);
      if (reprogram)
	{
	  /* Program the constant on the loop entry edge.  */
	  basic_block preheader = rvtt_commit_hoist_preheader (c.entry);
	  gcall *load = gimple_build_call
	    (xloadi_d->decl, 5, null_pointer_node,
	     build_int_cst (unsigned_type_node, c.value),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (unsigned_type_node, 0),
	     build_int_cst (integer_type_node, -32));
	  tree staged = make_ssa_name (vec_type);
	  gimple_call_set_lhs (load, staged);
	  gcall *wrcfg = gimple_build_call
	    (wrcfg_d->decl, 2, staged,
	     build_int_cst (unsigned_type_node, prgm));

	  gimple_stmt_iterator phg = gsi_last_bb (preheader);
	  if (gsi_end_p (phg) || !stmt_ends_bb_p (gsi_stmt (phg)))
	    {
	      gsi_insert_after (&phg, wrcfg, GSI_NEW_STMT);
	      gsi_insert_before (&phg, load, GSI_SAME_STMT);
	    }
	  else
	    {
	      gsi_insert_before (&phg, wrcfg, GSI_SAME_STMT);
	      gsi_insert_before (&phg, load, GSI_SAME_STMT);
	    }
	  if (!prior_bb)
	    allocs.safe_push (prgm_alloc { c.value, prgm, c.entry->dest });
	}
      else if (dump_file)
	fprintf (dump_file,
		 "prgm-const: reused PRGM L%u for identical immediate "
		 "0x%08x (dominating programming point bb %d)\n",
		 prgm, c.value, prior_bb->index);

      /* Read it back as a constant register and re-offer the pair to
	 the mad combine (which runs after this pass).  */
      gimple_stmt_iterator gsi = gsi_for_stmt (c.addi);
      gcall *read = gimple_build_call
	(readlreg_d->decl, 1, build_int_cst (unsigned_type_node, prgm));
      tree creg = make_ssa_name (vec_type);
      gimple_call_set_lhs (read, creg);
      gsi_insert_before (&gsi, read, GSI_SAME_STMT);

      if (!c.loadi)
	{
	  /* Immediate shape: the SFPADDI becomes a plain SFPADD of the
	     constant register.  */
	  gcall *add = gimple_build_call
	    (add_d->decl, 3, gimple_call_arg (c.addi, 1), creg,
	     build_int_cst (unsigned_type_node, 0));
	  gimple_call_set_lhs (add, gimple_call_lhs (c.addi));
	  gsi_replace (&gsi, add, false);
	}
      else
	{
	  /* Materialized shape: the SFPADD or SFPMAD keeps its form
	     with the constant-register operand (every vector operand
	     slot equal to the materialization is rewritten -- a MAD
	     can carry the same constant twice); the in-loop
	     materialization is removed (its only use was this
	     statement).  */
	  tree load_lhs = gimple_call_lhs (c.loadi);
	  for (unsigned ix = 0; ix != gimple_call_num_args (c.addi) - 1; ++ix)
	    if (gimple_call_arg (c.addi, ix) == load_lhs)
	      gimple_call_set_arg (c.addi, ix, creg);
	  update_stmt (c.addi);
	  gimple_stmt_iterator lgsi = gsi_for_stmt (c.loadi);
	  if (tree vdef = gimple_vdef (c.loadi))
	    if (TREE_CODE (vdef) == SSA_NAME)
	      unlink_stmt_vdef (c.loadi);
	  gsi_remove (&lgsi, true);
	  release_defs (c.loadi);
	}

      changed = true;
      if (dump_file && reprogram)
	fprintf (dump_file,
		 "prgm-const: allocated PRGM L%u for invariant immediate "
		 "0x%08x (loop header bb %d)\n",
		 prgm, c.value, c.loop->header->index);
    }
  return changed;
}
