/* Fold single-zero-assign SFPU CC regions into value-mask arithmetic.
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

/* -mtt-tensix-optimize-ccmask (default off).

   A predicated zeroing conditional

       v_if (x <= 0.0f) { z = 0.0f; } v_endif        (or x > 0.0f)

   reaches this pass as the structured CC skeleton

       sfppushc (0)
       tok = sfpxvif ()
       c   = sfpxfcmps (ib, x, 0, 0, 0, CC|TYPE_FLOAT<<shift)
       sfpxcondb (c, tok)
       zv  = <zero materialization>
       z'  = sfpassign_lv (z, zv)
       sfppopc (0)

   whose expansion is five delivered CC words per execution (SETCC pair,
   COMPC, predicated move, ENCC) forming a serial CC dependence spine.
   When the assigned value is architecturally zero and the comparison
   is a float order test against +0.0, the same per-lane function is two
   independent, shadow-fillable vector words:

       mask = SFPGT/SFPLE (x, LCONST_0, SET_DEST)    -- keep-mask
       z'   = SFPAND (z, mask)

   Bit-exactness (BH, all 2^32 x per lane, from the simulator models --
   craq-sim TENSIX_EXECUTE_SFPSETCC/SFPCOMPC/SFPMOV/SFPENCC vs
   TENSIX_EXECUTE_SFPGT/SFPLE mod1=8 and SFPAND):
   the CC lowering of "x <= 0.0f" enables the zeroing on lanes with
   {sign set} union {encoding == 0}; its complement -- the kept set --
   is {sign clear and encoding != 0}.  SFPGT SET_DEST compares
   sign-magnitude total order (sign-set encodings map below every
   sign-clear encoding, -0 to -1, +0 to 0), so mask = ~0 exactly on
   {sign clear and encoding != 0}: the same set, including both zeros,
   both NaN sign classes, and infinities.  AND with ~0/0 reproduces the
   lane merge exactly.  The mirrored argument covers "x > 0.0f" with
   SFPLE.  A host-side exhaustive sweep over all 2^32 x encodings of
   both lane models accompanies the lane evidence.

   The strict directions complete the family with the SWAPPED operand
   order.  "x < 0.0f" lowers to the single SETCC mod0 (raw sign bit
   set), so the kept set is {sign clear}; "x >= 0.0f" lowers to the
   single SETCC mod4 (raw sign bit clear), kept set {sign set}.  In the
   same total order, 0 <= x holds exactly on {sign clear} (+0 maps to
   0, every sign-set encoding maps to <= -1) and 0 > x exactly on
   {sign set}:

       mask = SFPLE (0, x, SET_DEST)     -- keep-mask for x <  0.0f
       mask = SFPGT (0, x, SET_DEST)     -- keep-mask for x >= 0.0f

   SET_DEST writes the FIRST compare operand (the md pattern ties it to
   the result), so these forms need the zero on the writable side: the
   region's own sfpxloadi/sfploadi zero materialization is reused as
   that operand, which the compare then overwrites with the mask.  The
   read-only constant register CREG_IDX_0 cannot serve (named refusal
   ccmask-zero-not-writable), and a zero with other uses is refused
   rather than silently split (ccmask-zero-shared).  EQ/NE have no
   single-order complement and keep refusing by name.  The exhaustive
   four-direction sweep of both lane models ships in
   tt/proofs/ccmask-direction-complete/ (EQUAL over 2^32 per
   direction); per the tt/proofs README contract the strict-direction
   folds may fire ONLY while that RESULT is EQUAL.

   Both replacement instructions execute under the enclosing CC state,
   so a fold inside an enclosing v_if keeps nested semantics: disabled
   lanes write neither mask nor z.  The deleted pushc/popc pair is
   stack-neutral.

   The pass runs immediately before pass_rvtt_invariant: the fold
   removes the region's CC-setting statement -- the loop-scoped barrier
   (rvtt_loop_has_sfpu_barrier_p) that otherwise forces the invariant
   immediate hoist to refuse the whole containing loop -- and exposes
   the mask's live range, so the invariant pass's own pressure-bounded
   greedy selection then hoists what fits and leaves the cheapest
   rematerializations in the loop with no further mechanism here.
   Every miss refuses by name with the program bytes unchanged.  */

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
			  "ccmask refused (%s): ", reason);
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

/* Return the defining call when VAL is a WRITABLE architectural zero
   -- an immediate materialization of 0 into an allocatable LREG (the
   read-only constant register does not qualify).  The swapped-operand
   keep-mask compares overwrite this operand with SET_DEST.  */

static gcall *
writable_zero_def (tree val)
{
  if (TREE_CODE (val) != SSA_NAME)
    return nullptr;
  gimple *def = SSA_NAME_DEF_STMT (val);
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return nullptr;
  gcall *call = as_a <gcall *> (def);
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfploadi:
      return int_arg (call, 1) == 0 ? call : nullptr;
    default:
      return nullptr;
    }
}

struct ccmask_group
{
  gcall *pushc, *xvif, *fcmp, *condb, *assign, *popc;
  tree x;	  /* compared vector */
  tree z;	  /* carried live value */
  tree zv;	  /* the assigned zero value */
  unsigned cc;	  /* SFPXCMP_MOD1_CC_* of the source compare */
};

/* Match the structured skeleton starting at the sfppushc at GSI.
   Returns true with G filled; CANDIDATE marks that the region
   identified itself as a zeroing conditional (enables named
   refusals).  */

static bool
match_group (gimple_stmt_iterator gsi, ccmask_group *g, bool *candidate)
{
  enum { WANT_XVIF, WANT_FCMP, WANT_CONDB, WANT_ASSIGN, WANT_POPC, DONE }
    want = WANT_XVIF;

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
	  return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
			    : false;
	}

      const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
      if (!insnd)
	return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
			  : false;
      gcall *call = as_a <gcall *> (stmt);

      switch (insnd->id)
	{
	case rvtt_insn_data::sfpxvif:
	  if (want != WANT_XVIF)
	    return *candidate ? refuse ("ccmask-region-shape", stmt) : false;
	  g->xvif = call;
	  want = WANT_FCMP;
	  continue;

	case rvtt_insn_data::sfpxfcmps:
	  {
	    if (want != WANT_FCMP)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    g->fcmp = call;
	    g->x = gimple_call_arg (call, 1);
	    want = WANT_CONDB;
	    continue;
	  }

	case rvtt_insn_data::sfpxfcmpv:
	case rvtt_insn_data::sfpxicmps:
	  /* Vector-vector and integer compares keep the CC lowering:
	     the mask equivalence proof here covers the float order
	     test against +0.0 only.  */
	  return *candidate ? refuse ("ccmask-compare-kind-unsupported", stmt)
			    : false;

	case rvtt_insn_data::sfpxcondb:
	  {
	    if (want != WANT_CONDB)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    tree c = gimple_call_arg (call, 0);
	    tree t = gimple_call_arg (call, 1);
	    if (TREE_CODE (c) != SSA_NAME || TREE_CODE (t) != SSA_NAME
		|| SSA_NAME_DEF_STMT (c) != g->fcmp
		|| SSA_NAME_DEF_STMT (t) != g->xvif
		|| !has_single_use (c) || !has_single_use (t))
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    g->condb = call;
	    want = WANT_ASSIGN;
	    continue;
	  }

	case rvtt_insn_data::sfpassign_lv:
	  {
	    if (want != WANT_ASSIGN)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    /* Candidate identification: the single predicated statement
	       assigns an architectural zero.  From here on refusals are
	       reported by name.  */
	    if (!zero_vector_p (gimple_call_arg (call, 1)))
	      return false;
	    *candidate = true;
	    g->assign = call;
	    g->z = gimple_call_arg (call, 0);
	    g->zv = gimple_call_arg (call, 1);
	    want = WANT_POPC;
	    continue;
	  }

	case rvtt_insn_data::sfppopc:
	  {
	    if (want != WANT_POPC || int_arg (call, 0) != 0)
	      return *candidate ? refuse ("ccmask-region-shape", stmt)
				: false;
	    g->popc = call;
	    goto region_closed;
	  }

	case rvtt_insn_data::sfppushc:
	case rvtt_insn_data::sfpcompc:
	  /* A nested region or an else-arm is not a single zeroing
	     assignment.  */
	  return *candidate ? refuse ("ccmask-region-shape", stmt) : false;

	case rvtt_insn_data::sfpxloadi:
	case rvtt_insn_data::sfploadi:
	case rvtt_insn_data::sfploadi_lv:
	case rvtt_insn_data::sfpreadlreg:
	  /* Pure LREG value materializations (the zero's own definition
	     and similar): no CC, configuration, or Dst effect; a
	     lane-predicated materialization feeding only the predicated
	     assign is re-expressed exactly by the mask merge.  */
	  continue;

	default:
	  /* Anything else with target side effects or CC involvement is
	     not this shape.  */
	  if (insnd->sets_cc (call) || insnd->has_side_effects (call))
	    return *candidate ? refuse ("ccmask-region-foreign-stmt", stmt)
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
  return *candidate ? refuse ("ccmask-region-open-cfg", g->pushc) : false;

 region_closed:
  /* The compare: float order test against immediate bits 0 (+0.0),
     with a CC selection whose complement is a single GT or LE.  */
  {
    long mod = int_arg (g->fcmp, 5);
    if (mod < 0)
      return refuse ("ccmask-compare-form", g->fcmp);
    unsigned type = ((unsigned) mod >> SFPXCMP_MOD1_TYPE_SHIFT)
      & SFPXCMP_MOD1_TYPE_MASK;
    if (type != SFPXCMP_MOD1_TYPE_FLOAT)
      return refuse ("ccmask-compare-kind-unsupported", g->fcmp);
    g->cc = (unsigned) mod & SFPXCMP_MOD1_CC_MASK;
    if (g->cc != SFPXCMP_MOD1_CC_LE && g->cc != SFPXCMP_MOD1_CC_GT
	&& g->cc != SFPXCMP_MOD1_CC_LT && g->cc != SFPXCMP_MOD1_CC_GE)
      /* EQ/NE have no single-order complement.  */
      return refuse ("ccmask-compare-direction-unsupported", g->fcmp);
    if (g->cc == SFPXCMP_MOD1_CC_LT || g->cc == SFPXCMP_MOD1_CC_GE)
      {
	/* The strict-direction keep-masks are the swapped-operand
	   compares (0 <= x, 0 > x): SET_DEST writes the first operand,
	   so the zero must be a writable materialization the compare
	   can overwrite.  Reuse the region's own zero; the read-only
	   constant register cannot be a SET_DEST operand, and a zero
	   with other uses is not silently split.  */
	gcall *zdef = writable_zero_def (g->zv);
	if (!zdef)
	  return refuse ("ccmask-zero-not-writable", g->assign);
	if (!has_single_use (g->zv))
	  return refuse ("ccmask-zero-shared", g->assign);
      }
    if (int_arg (g->fcmp, 2) != 0
	|| int_arg (g->fcmp, 3) != 0 || int_arg (g->fcmp, 4) != 0)
      /* The equivalence proof is against the +0.0 boundary's pure
	 sign/zero CC lowering; other immediates lower arithmetically.  */
      return refuse ("ccmask-boundary-unsupported", g->fcmp);
  }

  if (TREE_CODE (g->x) != SSA_NAME || TREE_CODE (g->z) != SSA_NAME)
    return refuse ("ccmask-operand-form", g->assign);

  return true;
}

/* Commit the fold for G.  */

static void
transform_group (ccmask_group *g)
{
  /* keep-mask = complement of the zeroing condition:
     x <= 0  ->  SFPGT (x, 0);    x > 0  ->  SFPLE (x, 0);
     x <  0  ->  SFPLE (0, x);    x >= 0 ->  SFPGT (0, x)
     where the strict directions swap the operands and reuse the
     region's writable zero as the SET_DEST (written) operand.  */
  bool swapped = (g->cc == SFPXCMP_MOD1_CC_LT
		  || g->cc == SFPXCMP_MOD1_CC_GE);
  const rvtt_insn_data *cmp_insnd
    = rvtt_get_insn_data ((g->cc == SFPXCMP_MOD1_CC_LE
			   || g->cc == SFPXCMP_MOD1_CC_GE)
			  ? rvtt_insn_data::sfpgt : rvtt_insn_data::sfple);
  const rvtt_insn_data *and_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpand);
  const rvtt_insn_data *zero_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcc_assert (cmp_insnd->decl && and_insnd->decl && zero_insnd->decl);

  tree vec_type = TREE_TYPE (g->x);
  gimple_stmt_iterator at = gsi_for_stmt (g->assign);
  location_t loc = gimple_location (g->assign);

  tree zero_ssa;
  if (swapped)
    /* The region's own zero materialization (checked writable and
       single-use in match_group); its def dominates the assign, hence
       this insertion point.  */
    zero_ssa = g->zv;
  else
    {
      gcall *zero = gimple_build_call (zero_insnd->decl, 1,
				       build_int_cst (unsigned_type_node,
						      CREG_IDX_0));
      zero_ssa = make_ssa_name (vec_type);
      gimple_call_set_lhs (zero, zero_ssa);
      gimple_set_location (zero, loc);
      gsi_insert_before (&at, zero, GSI_SAME_STMT);
    }

  gcall *cmp = swapped
    ? gimple_build_call (cmp_insnd->decl, 3, zero_ssa, g->x,
			 build_int_cst (unsigned_type_node,
					SFPGTLE_MOD1_SET_DEST))
    : gimple_build_call (cmp_insnd->decl, 3, g->x, zero_ssa,
			 build_int_cst (unsigned_type_node,
					SFPGTLE_MOD1_SET_DEST));
  tree mask = make_ssa_name (vec_type);
  gimple_call_set_lhs (cmp, mask);
  gimple_set_location (cmp, loc);
  gsi_insert_before (&at, cmp, GSI_SAME_STMT);

  gcall *land = gimple_build_call (and_insnd->decl, 2, g->z, mask);
  gimple_call_set_lhs (land, gimple_call_lhs (g->assign));
  gimple_set_location (land, loc);
  gsi_replace (&at, land, false);

  if (dump_file)
    {
      fprintf (dump_file, "ccmask: folded zeroing CC region into ");
      print_gimple_stmt (dump_file, cmp, 0);
      fprintf (dump_file, "ccmask:   masking ");
      print_gimple_stmt (dump_file, land, 0);
    }

  auto remove = [] (gimple *stmt)
    {
      rvtt_prep_stmt_for_deletion (stmt);
      unlink_stmt_vdef (stmt);
      gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
      gsi_remove (&gsi, true);
      release_defs (stmt);
    };
  remove (g->condb);
  remove (g->fcmp);
  remove (g->xvif);
  remove (g->pushc);
  remove (g->popc);
  /* The zero materialization the region assigned is dead once the
     assign is gone; delete it here so the invariant pass running next
     never sees a use-free architectural LREG write to hoist.  */
  if (TREE_CODE (g->zv) == SSA_NAME && has_zero_uses (g->zv))
    {
      gimple *zdef = SSA_NAME_DEF_STMT (g->zv);
      if (rvtt_get_insn_data (zdef))
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
	      ccmask_group g;
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

const pass_data pass_data_rvtt_ccmask =
{
  GIMPLE_PASS,
  "rvtt_ccmask",
  OPTGROUP_NONE,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_ccmask : public gimple_opt_pass
{
public:
  pass_rvtt_ccmask (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_ccmask, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_ccmask > 0;
  }

  unsigned execute (function *fn) final override
  {
    /* The mask equivalence is proven against the BH simulator models
       and the BH hand-kernel concordance; SFPGT/SFPLE do not exist
       before BH.  Other targets keep the CC lowering byte-identically.  */
    if (!TARGET_XTT_TENSIX_BH)
      {
	rvtt_refuse (RVTT_REF_CCMASK_TARGET_UNPROVEN, dump_file,
		     "ccmask refused (ccmask-target-unproven)\n");
	return 0;
      }
    n_folded = 0;
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file, "ccmask: folds=%u\n", n_folded);
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_ccmask (gcc::context *ctxt)
{
  return new pass_rvtt_ccmask (ctxt);
}
