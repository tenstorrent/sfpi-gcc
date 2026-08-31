/* Fold conditional-negate SFPU CC regions into the integer SFPABS.
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

/* -mtt-tensix-optimize-int-abs (default off).

   A predicated integer negation conditional

       v_if (v < 0) { r = 0 - v; } v_endif

   reaches this pass as the structured CC skeleton

       sfppushc (0)
       tok = sfpxvif ()
       c   = sfpxicmps (ib, x, 0, 0, 0, TYPE_INT<<4 | CC_LT)
       sfpxcondb (c, tok)
       zv  = <zero materialization>
       neg = sfpiadd_v (x, zv, ARG_2SCOMP_LREG_DST|CC_NONE)   ; zv - x
       r   = sfpassign_lv (x, neg)
       sfppopc (0)

   whose expansion is five delivered words per execution (SETCC, IADD,
   ENCC around the load/store pair) forming a serial CC dependence
   spine.  The same per-lane function is ONE vector word:

       r = SFPABS (x, mod1=INT)

   Bit-exactness (all 2^32 x per lane, from the pinned simulator
   models -- craq-sim TENSIX_EXECUTE_SFPSETCC mod1=0 raw-sign-bit
   select + TENSIX_EXECUTE_SFPIADD mod1=6 two's-complement wrap
   subtract vs TENSIX_EXECUTE_SFPABS mod1=0):
   the CC lowering of "v < 0" enables the negation exactly on lanes
   whose raw bit 31 is set; SFPABS mod1=0 negates exactly the lanes
   with src >= 0x80000000 -- the same set -- and both arms compute the
   identical wrapping 0 - src, including INT32_MIN -> INT32_MIN.  The
   exhaustive host sweep (mismatches = 0 over 2^32) ships in
   tt/proofs/int-abs-negate-select/; per the tt/proofs README contract
   this fold may fire ONLY while that RESULT is EQUAL.

   The proof is a statement about the VALUE FUNCTION r(v), not about
   one spelling; every spelling whose region reduces to the identical
   value function is admitted by the same artifact (the reduction
   record accompanies it as REDUCTION.md there):

     - "v <= 0" (CC_LE): the enabled set gains exactly raw v == 0
       (two's complement zero is unique), where the wrapping negation
       is the identity 0 - 0 = 0 = v -- pointwise the same r(v).
     - the v_else spelling, v_if (v >= 0) { } v_else { r = 0 - v; }:
       a single SFPCOMPC directly after the condition binds (an empty
       then-arm) complements the enabled set within the enclosing
       frame (craq-sim TENSIX_EXECUTE_SFPCOMPC), so CC_GE folds to the
       CC_LT set and CC_GT to the CC_LE set on the enclosing-enabled
       lanes -- the sets already covered.

   Every other order direction (CC_GE/CC_GT direct, CC_LT/CC_LE under
   an else-arm) yields the negate-on-complement value function, which
   is NOT an absolute value: it refuses int-abs-region-shape.  EQ/NE
   are not order tests and keep refusing
   int-abs-compare-kind-unsupported.  A COMPC anywhere but directly
   after the condition binds -- in particular after a lane-predicated
   materialization (a non-empty then-arm) -- is not this shape.

   The replacement executes under the enclosing CC state, so a fold
   inside an enclosing v_if keeps nested semantics (the ccmask
   precedent): on enclosing-disabled lanes SFPABS writes nothing and
   the structured merge carries x, which is also what the deleted
   region produced there.  The deleted pushc/popc pair is
   stack-neutral.

   The pass runs immediately before pass_rvtt_invariant, next to the
   ccmask fold and for the same reasons: the fold removes the region's
   CC-setting statement (the loop-scoped barrier that otherwise forces
   the invariant immediate hoist to refuse the containing loop) and
   shortens the row's serial spine.  Every miss refuses by name with
   the program bytes unchanged.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-cfg.h"
#include "tree-ssa-loop-niter.h"
#include "cfgloop.h"
#include "rvtt.h"
#include "rvtt-refuse.h"

namespace {

static unsigned n_folded;

static gcall *
is_rvtt_call (gimple *stmt, rvtt_insn_data::insn_id id)
{
  if (const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt))
    if (insnd->id == id)
      return as_a <gcall *> (stmt);
  return nullptr;
}

static long
int_arg (gcall *call, unsigned n)
{
  tree arg = gimple_call_arg (call, n);
  return TREE_CODE (arg) == INTEGER_CST ? TREE_INT_CST_LOW (arg) : -1;
}

static bool
refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "int-abs refused (%s): ", reason);
  if (dump_file)
    print_gimple_stmt (dump_file, stmt, 0);
  return false;
}

/* Return true when VAL is an architectural all-zero vector: a read of
   the constant-zero register or an immediate materialization of 0.  */

static bool
zero_vector_p (tree val)
{
  if (TREE_CODE (val) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (val);
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (def);
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpreadlreg:
      return int_arg (call, 0) == CREG_IDX_0;
    case rvtt_insn_data::sfpxloadi:
      /* (ib, value, ...) -- all-constant argument forms only.  */
      return int_arg (call, 1) == 0;
    case rvtt_insn_data::sfploadi:
      return int_arg (call, 1) == 0;
    default:
      return false;
    }
}

struct intabs_group
{
  gcall *pushc, *xvif, *icmp, *condb, *iadd, *assign, *popc;
  gcall *compc;	  /* the else-arm marker, when present */
  tree x;	  /* compared vector */
};

/* Match the structured skeleton starting at the sfppushc at GSI.
   Returns true with G filled; CANDIDATE marks that the region
   identified itself as a subtract-form conditional (enables named
   refusals).  */

static bool
match_group (gimple_stmt_iterator gsi, intabs_group *g, bool *candidate)
{
  enum { WANT_XVIF, WANT_ICMP, WANT_CONDB, WANT_IADD, WANT_ASSIGN,
	 WANT_POPC }
    want = WANT_XVIF;
  unsigned lreg_mats_after_condb = 0;

  *candidate = false;
  memset (g, 0, sizeof (*g));

  gcall *pushc = is_rvtt_call (gsi_stmt (gsi), rvtt_insn_data::sfppushc);
  if (!pushc || int_arg (pushc, 0) != 0)
    return false;
  g->pushc = pushc;

  gsi_next (&gsi);
  for (unsigned steps = 0; !gsi_end_p (gsi) && steps < 64; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
	continue;
      steps++;

      /* Scalar plumbing (e.g. instruction-buffer address loads) is not
	 part of the vector region; it stays where it is.  */
      if (gimple_code (stmt) == GIMPLE_ASSIGN)
	{
	  tree lhs = gimple_get_lhs (stmt);
	  if (lhs && TREE_CODE (lhs) == SSA_NAME
	      && !VECTOR_TYPE_P (TREE_TYPE (lhs)))
	    continue;
	  return *candidate ? refuse ("int-abs-region-foreign-stmt", stmt)
			    : false;
	}

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	return *candidate ? refuse ("int-abs-region-foreign-stmt", stmt)
			  : false;
      gcall *call = as_a <gcall *> (stmt);

      switch (insnd->id)
	{
	case rvtt_insn_data::sfpxvif:
	  if (want != WANT_XVIF)
	    return *candidate ? refuse ("int-abs-region-shape", stmt) : false;
	  g->xvif = call;
	  want = WANT_ICMP;
	  continue;

	case rvtt_insn_data::sfpxicmps:
	  {
	    if (want != WANT_ICMP)
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    g->icmp = call;
	    g->x = gimple_call_arg (call, 1);
	    want = WANT_CONDB;
	    continue;
	  }

	case rvtt_insn_data::sfpxfcmps:
	case rvtt_insn_data::sfpxfcmpv:
	case rvtt_insn_data::sfpxicmpv:
	  /* Float and vector-vector compares keep the CC lowering: the
	     SFPABS equivalence proof here covers the signed-int sign
	     test against literal 0 only.  (A float region is the
	     ccmask pass's candidate class, never this one.)  */
	  return *candidate ? refuse ("int-abs-compare-kind-unsupported",
				      stmt)
			    : false;

	case rvtt_insn_data::sfpxcondb:
	  {
	    if (want != WANT_CONDB)
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    tree c = gimple_call_arg (call, 0);
	    tree t = gimple_call_arg (call, 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->icmp
		|| SSA_NAME_DEF_STMT (t) != g->xvif
		|| !has_single_use (c) || !has_single_use (t))
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    g->condb = call;
	    want = WANT_IADD;
	    continue;
	  }

	case rvtt_insn_data::sfpiadd_v:
	  {
	    if (want != WANT_IADD)
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    /* Candidate identification: the predicated statement is a
	       two's-complement subtract form.  From here on refusals
	       are reported by name.  */
	    long mod = int_arg (call, 2);
	    if (mod < 0 || !(mod & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST))
	      return false;
	    *candidate = true;
	    g->iadd = call;
	    want = WANT_ASSIGN;
	    continue;
	  }

	case rvtt_insn_data::sfpassign_lv:
	  {
	    if (want != WANT_ASSIGN)
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    tree nv = gimple_call_arg (call, 1);
	    tree lhs = gimple_call_lhs (g->iadd);
	    if (TREE_CODE (nv) != SSA_NAME || nv != lhs
		|| !has_single_use (nv))
	      return refuse ("int-abs-region-shape", stmt);
	    g->assign = call;
	    want = WANT_POPC;
	    continue;
	  }

	case rvtt_insn_data::sfppopc:
	  {
	    if (want != WANT_POPC || int_arg (call, 0) != 0)
	      return *candidate ? refuse ("int-abs-region-shape", stmt)
				: false;
	    g->popc = call;
	    goto region_closed;
	  }

	case rvtt_insn_data::sfppushc:
	  /* A nested region is not a single negate assignment.  */
	  return *candidate ? refuse ("int-abs-region-shape", stmt) : false;

	case rvtt_insn_data::sfpcompc:
	  /* The else-arm marker: admissible exactly once, directly
	     after the condition binds and before any lane-predicated
	     materialization -- i.e. an empty then-arm.  The fold
	     accounts for it by complementing the compare's enabled
	     set (region_closed below).  Anywhere else it is not this
	     shape.  */
	  if (want == WANT_IADD && !g->compc && lreg_mats_after_condb == 0)
	    {
	      g->compc = call;
	      continue;
	    }
	  return *candidate ? refuse ("int-abs-region-shape", stmt) : false;

	case rvtt_insn_data::sfpxloadi:
	case rvtt_insn_data::sfploadi:
	case rvtt_insn_data::sfploadi_lv:
	case rvtt_insn_data::sfpreadlreg:
	  /* Pure LREG value materializations (the zero's own definition
	     and similar): no CC, configuration, or Dst effect; a
	     lane-predicated materialization feeding only the predicated
	     subtract is re-expressed exactly by SFPABS.  */
	  if (want == WANT_IADD && !g->compc)
	    lreg_mats_after_condb++;
	  continue;

	default:
	  /* Anything else with target side effects or CC involvement is
	     not this shape.  */
	  if (insnd->sets_cc (call) || insnd->has_side_effects (call))
	    return *candidate ? refuse ("int-abs-region-foreign-stmt", stmt)
			      : false;
	  continue;
	}
    }
  /* The block ended before the closing sfppopc.  The v_endif spelling
     places the popc behind a structural diamond (the CC frame
     destructor's counted pop): the body block jumps to a join J with
     exactly two predecessors -- the body block and a block P whose only
     statement is sfppopc (0) and whose single successor is J.  Match
     that closing shape; anything else is an open region.  */
  if (want == WANT_POPC)
    {
      basic_block body_bb = gimple_bb (g->assign);
      if (single_succ_p (body_bb))
	{
	  basic_block join = single_succ (body_bb);
	  if (EDGE_COUNT (join->preds) == 2)
	    {
	      basic_block popc_bb = EDGE_PRED (join, 0)->src == body_bb
		? EDGE_PRED (join, 1)->src : EDGE_PRED (join, 0)->src;
	      gcall *popc = nullptr;
	      bool only = true;
	      for (gimple_stmt_iterator psi = gsi_start_bb (popc_bb);
		   !gsi_end_p (psi) && only; gsi_next (&psi))
		{
		  gimple *pstmt = gsi_stmt (psi);
		  if (is_gimple_debug (pstmt)
		      || gimple_code (pstmt) == GIMPLE_LABEL)
		    continue;
		  if (gcall *pc = is_rvtt_call (pstmt,
						rvtt_insn_data::sfppopc))
		    {
		      if (popc || int_arg (pc, 0) != 0)
			only = false;
		      else
			popc = pc;
		    }
		  else
		    only = false;
		}
	      if (only && popc && single_succ_p (popc_bb)
		  && single_succ (popc_bb) == join)
		{
		  g->popc = popc;
		  goto region_closed;
		}
	    }
	}
    }
  return *candidate ? refuse ("int-abs-region-open-cfg", g->pushc) : false;

 region_closed:
  /* The compare: a signed-int order test against immediate bits 0
     whose EFFECTIVE enabled set -- after the else-arm complement when
     one is present -- reduces to the proven value function's enabled
     sets: {v < 0} (the proof's own set) or {v <= 0} (gains exactly
     raw v == 0, where the wrapping negation is the identity, so the
     value function is pointwise unchanged).  Any other effective set
     computes a different value function and is not an absolute value.
     EQ/NE are not order tests; every other type or boundary lowers
     differently.  */
  {
    long mod = int_arg (g->icmp, 5);
    if (mod < 0)
      return refuse ("int-abs-compare-form", g->icmp);
    unsigned type = ((unsigned) mod >> SFPXCMP_MOD1_TYPE_SHIFT)
      & SFPXCMP_MOD1_TYPE_MASK;
    unsigned cc = (unsigned) mod & SFPXCMP_MOD1_CC_MASK;
    if (type != SFPXCMP_MOD1_TYPE_INT
	|| (cc != SFPXCMP_MOD1_CC_LT && cc != SFPXCMP_MOD1_CC_LE
	    && cc != SFPXCMP_MOD1_CC_GE && cc != SFPXCMP_MOD1_CC_GT))
      return refuse ("int-abs-compare-kind-unsupported", g->icmp);
    /* The order-test complement pairs are LT<->GE and LE<->GT: encoded
       as cc ^ 1 (LT=0 GE=1, GT=4 LE=5).  */
    unsigned eff = g->compc ? (cc ^ 1) : cc;
    if (eff != SFPXCMP_MOD1_CC_LT && eff != SFPXCMP_MOD1_CC_LE)
      return refuse ("int-abs-region-shape", g->icmp);
    if (int_arg (g->icmp, 2) != 0
	|| int_arg (g->icmp, 3) != 0 || int_arg (g->icmp, 4) != 0)
      return refuse ("int-abs-boundary-unsupported", g->icmp);
  }

  /* The predicated statement: exactly neg = 0 - x with the wrapping
     two's-complement subtract and no CC side channel.
     sfpiadd_v (A, B, ARG_2SCOMP_LREG_DST) computes B - A: A must be
     the compared value and B an architectural zero.  */
  if (int_arg (g->iadd, 2)
      != (long) (SFPIADD_MOD1_ARG_2SCOMP_LREG_DST | SFPIADD_MOD1_CC_NONE))
    return refuse ("int-abs-iadd-mod-unsupported", g->iadd);
  if (gimple_call_arg (g->iadd, 0) != g->x)
    return refuse ("int-abs-operand-mismatch", g->iadd);
  if (!zero_vector_p (gimple_call_arg (g->iadd, 1)))
    return refuse ("int-abs-minuend-not-zero", g->iadd);

  /* The merge must carry the compared value itself on untaken lanes:
     r = (x < 0) ? -x : x.  A different carried value is min/max-like
     selection, not an absolute value.  */
  if (gimple_call_arg (g->assign, 0) != g->x)
    return refuse ("int-abs-carried-value-mismatch", g->assign);

  if (TREE_CODE (g->x) != SSA_NAME)
    return refuse ("int-abs-operand-form", g->assign);

  return true;
}

/* Commit the fold for G.  */

static void
transform_group (intabs_group *g)
{
  const rvtt_insn_data *abs_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpabs);
  gcc_assert (abs_insnd->decl);

  gimple_stmt_iterator at = gsi_for_stmt (g->assign);
  location_t loc = gimple_location (g->assign);

  gcall *abscall
    = gimple_build_call (abs_insnd->decl, 2, g->x,
			 build_int_cst (unsigned_type_node,
					SFPABS_MOD1_INT));
  gimple_call_set_lhs (abscall, gimple_call_lhs (g->assign));
  gimple_set_location (abscall, loc);
  gsi_replace (&at, abscall, false);

  if (dump_file)
    {
      fprintf (dump_file, "int-abs: folded negate-select CC region into ");
      print_gimple_stmt (dump_file, abscall, 0);
    }

  auto remove = [] (gimple *stmt)
    {
      rvtt_prep_stmt_for_deletion (stmt);
      unlink_stmt_vdef (stmt);
      gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
      gsi_remove (&gsi, true);
      release_defs (stmt);
    };
  /* Identify the zero materialization BEFORE deleting its use:
     rvtt_prep_stmt_for_deletion strips the lhs off a single-use arg's
     defining call (releasing the SSA name) when the use goes away, so
     the name cannot be queried afterwards.  */
  tree zv = gimple_call_arg (g->iadd, 1);
  gimple *zdef = nullptr;
  if (TREE_CODE (zv) == SSA_NAME)
    {
      gimple *d = SSA_NAME_DEF_STMT (zv);
      if (d && rvtt_get_insn_data (d))
	zdef = d;
    }
  remove (g->iadd);
  if (g->compc)
    remove (g->compc);
  remove (g->condb);
  remove (g->icmp);
  remove (g->xvif);
  remove (g->pushc);
  remove (g->popc);
  /* The zero materialization the region subtracted from is dead once
     the subtract is gone; delete it here so the invariant pass running
     next never sees a use-free architectural LREG write to hoist.
     Keep it when other uses survive.  */
  if (zdef && gimple_bb (zdef))
    {
      tree lhs = gimple_call_lhs (zdef);
      if (!lhs || (TREE_CODE (lhs) == SSA_NAME && has_zero_uses (lhs)))
	remove (zdef);
    }

  n_folded++;
}

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next = gsi;
	  gsi_next (&next);
	  if (is_rvtt_call (gsi_stmt (gsi), rvtt_insn_data::sfppushc))
	    {
	      intabs_group g;
	      bool candidate;
	      if (match_group (gsi, &g, &candidate))
		{
		  transform_group (&g);
		  changed = true;
		  /* Statements around GSI were deleted; restart from
		     the recorded successor, which is never a region
		     member (the region begins at its pushc).  */
		}
	    }
	  gsi = next;
	}
    }

  return changed;
}

const pass_data pass_data_rvtt_int_abs =
{
  GIMPLE_PASS,
  "rvtt_int_abs",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_int_abs : public gimple_opt_pass
{
public:
  pass_rvtt_int_abs (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_int_abs, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_int_abs > 0;
  }

  unsigned execute (function *fn) final override
  {
    /* The SFPABS equivalence is proven against the pinned simulator's
       shared WH/BH integer-abs arm, but the proof run, the twins and
       the concordant hand kernels are BH; the simulator itself records
       the integer abs of INT32_MIN as changed on later architectures.
       Fail closed everywhere the proof was not run.  */
    if (!TARGET_XTT_TENSIX_BH)
      {
	rvtt_refuse (RVTT_REF_INT_ABS_TARGET_UNPROVEN, dump_file,
		     "int-abs refused (int-abs-target-unproven)\n");
	return 0;
      }
    n_folded = 0;
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file, "int-abs: folds=%u\n", n_folded);
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_int_abs (gcc::context *ctxt)
{
  return new pass_rvtt_int_abs (ctxt);
}
