/* PRGM constant programming: constant rematerialization
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

/* Constant rematerialization for the PRGM constant pass
   (-mtt-tensix-optimize-const-remat), plus helpers that derive the
   32-bit image of staged constants.

   The SFPU has eight allocatable LREGs and no spill path: the
   rvtt_sfpassign memory alternatives exist only so that LRA's constraint
   matching succeeds, and selecting one is an error at output time.  When
   the vector pressure model still exceeds the register file after
   constant residency (gimple-rvtt-prgm-residency.cc), a value
   materialized by an SFPLOADI chain from scalar inputs is re-issued
   before each use instead of being kept live, and the long-lived
   original is deleted.  The scalar inputs live in GPRs, so the clone is
   always available.

   Lane predication: SFPLOADI writes only CC-enabled lanes (SFPLOADI.md).
   A clone placed immediately before its consumer executes under the
   consumer's CC state, so every lane the consumer reads and commits was
   just written by the clone, provided the consumer commits only enabled
   lanes and reads its operands lane-locally.  remat_consumer_audited_p
   lists such consumers; other uses keep the original value
   (consumer-lane-discipline-unaudited).

   The pressure model (rvtt-pressure.cc) is not the allocator: residual
   over-pressure is reported, and rtl-rvtt-spill-diag.cc turns any actual
   spill into a user error.  */

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

/* A rematerializable chain defines a vector value from scalar-only
   inputs: a single sfpxloadi/sfploadi, or an sfploadi followed by the
   sfploadi_lv upper-half merge whose live-value operand is the first load
   (the two-issue form the immvar expansion emits for 32-bit values).
   Every other argument is a scalar, so the chain can be re-issued
   anywhere its definition dominates.  */

/* Return the vector live-value operand of sfploadi_lv CALL (argument 1,
   see rvtt-insn.def), or NULL_TREE.  */

tree
loadi_lv_link (gcall *call)
{
  tree arg = gimple_call_arg (call, 1);
  return VECTOR_TYPE_P (TREE_TYPE (arg)) ? arg : NULL_TREE;
}

/* Return true if every argument of CALL other than SKIP is a scalar, so
   the call reads no SFPU register state.  */

static bool
scalar_args_p (gcall *call, tree skip)
{
  for (unsigned ix = 0; ix != gimple_call_num_args (call); ++ix)
    {
      tree arg = gimple_call_arg (call, ix);
      if (arg == skip)
	continue;
      if (VECTOR_TYPE_P (TREE_TYPE (arg)))
	return false;
    }
  return true;
}

/* If NAME is defined by a rematerializable chain (see above), fill *OUT
   (TAIL defines NAME; ROOT is the first issue) and return true.  For the
   two-issue form the first load must die in the merge.  */

bool
remat_chain_p (tree name, remat_chain *out)
{
  if (TREE_CODE (name) != SSA_NAME)
    return false;
  gcall *tail = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (name));
  if (!tail || !gimple_bb (tail) || gimple_call_lhs (tail) != name)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (tail);
  if (!insnd)
    return false;

  if (insnd->id == rvtt_insn_data::sfpxloadi
      || insnd->id == rvtt_insn_data::sfploadi)
    {
      if (!scalar_args_p (tail, NULL_TREE))
	return false;
      out->tail = out->root = tail;
      return true;
    }

  if (insnd->id == rvtt_insn_data::sfploadi_lv)
    {
      tree link = loadi_lv_link (tail);
      if (!link || TREE_CODE (link) != SSA_NAME
	  || !scalar_args_p (tail, link)
	  || !single_nondebug_use_p (link, tail))
	return false;
      gcall *root = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (link));
      if (!root || !gimple_bb (root))
	return false;
      const rvtt_insn_data *rootd = rvtt_get_insn_data (root);
      if (!rootd
	  || (rootd->id != rvtt_insn_data::sfploadi
	      && rootd->id != rvtt_insn_data::sfpxloadi)
	  || !scalar_args_p (root, NULL_TREE))
	return false;
      out->tail = tail;
      out->root = root;
      return true;
    }

  return false;
}

/* If chain C materializes a constant, store its 32-bit lane image in
   *VALUE and return true.  The 32-bit sfpxloadi forms carry the fp32/int32
   pattern verbatim (emit_loadimm in gimple-rvtt-immvar.cc, bits 31/-32/32);
   the SFPLOADI FLOATB form is imm16 << 16.  Other encodings return false;
   they can still be rematerialized, which never interprets the value.  */

bool
constant_chain_value_p (const remat_chain &c, unsigned *value)
{
  if (c.root != c.tail)
    {
      /* The two-instruction form emit_loadimm issues for a 32-bit
	 constant whose halves are both significant: SFPLOADI USHORT of
	 the low half, then SFPLOADI_LV UPPER of the high half.  */
      const rvtt_insn_data *rootd = rvtt_get_insn_data (c.root);
      const rvtt_insn_data *taild = rvtt_get_insn_data (c.tail);
      if (!rootd || !taild
	  || rootd->id != rvtt_insn_data::sfploadi
	  || taild->id != rvtt_insn_data::sfploadi_lv
	  || !scalar_args_p (c.root, NULL_TREE))
	return false;
      tree lo = gimple_call_arg (c.root, rootd->imm_arg ());
      tree hi = gimple_call_arg (c.tail, taild->imm_arg ());
      tree lo_mod = gimple_call_arg (c.root, rootd->mod_arg ());
      tree hi_mod = gimple_call_arg (c.tail, taild->mod_arg ());
      if (TREE_CODE (lo) != INTEGER_CST || TREE_CODE (hi) != INTEGER_CST
	  || TREE_CODE (lo_mod) != INTEGER_CST
	  || TREE_CODE (hi_mod) != INTEGER_CST
	  || TREE_INT_CST_LOW (lo_mod) != SFPLOADI_MOD0_USHORT
	  || TREE_INT_CST_LOW (hi_mod) != SFPLOADI_MOD0_UPPER)
	return false;
      *value = (((TREE_INT_CST_LOW (hi) & 0xffff) << 16)
		| (TREE_INT_CST_LOW (lo) & 0xffff));
      return true;
    }
  gcall *load = c.tail;
  tree imm = gimple_call_arg (load, 1);
  if (TREE_CODE (imm) != INTEGER_CST || !scalar_args_p (load, NULL_TREE))
    return false;
  for (unsigned ix = 1; ix != gimple_call_num_args (load); ++ix)
    if (TREE_CODE (gimple_call_arg (load, ix)) != INTEGER_CST)
      return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (load);
  tree mod = gimple_call_arg (load, gimple_call_num_args (load) - 1);
  if (insnd->id == rvtt_insn_data::sfpxloadi)
    {
      HOST_WIDE_INT bits = tree_to_shwi (mod);
      if (bits != 31 && bits != 32 && bits != -32)
	return false;
      *value = TREE_INT_CST_LOW (imm);
      return true;
    }
  /* Shortened SFPLOADI: FLOATB only.  */
  if (!integer_zerop (mod))
    return false;
  *value = (TREE_INT_CST_LOW (imm) & 0xffff) << 16;
  return true;
}

/* Single-issue form of constant_chain_value_p for LOAD.  */

bool
single_issue_constant_image_p (gcall *load, unsigned *value)
{
  remat_chain chain { load, load };
  return constant_chain_value_p (chain, value);
}

/* Store in *VALUE the 32-bit constant STAGED holds for a typed SFPCONFIG
   write and return true, or return false if STAGED is not a constant
   chain; the destination's value is then unknown.  */

bool
staged_config_value (tree staged, unsigned *value)
{
  if (!staged || TREE_CODE (staged) != SSA_NAME)
    return false;
  remat_chain chain;
  if (!remat_chain_p (staged, &chain))
    return false;
  return constant_chain_value_p (chain, value);
}

/* Return true if STMT is a consumer before which NAME may be
   rematerialized: it writes its destination lanes only where CC-enabled
   and reads its vector operands lane-locally, so a clone placed just
   before it is equivalent to the original value on every consumed lane.
   Each op in the case list masks its writes with the lane-enable
   predicate in its ISA description (e.g. SFPMAD.md "lanewise if
   (LaneEnabled)"); SFPIADD's CC updates are likewise enabled-lanes-only,
   SFPSTORE writes Dst rows only for enabled lanes, and SFPSTOCHRND's PRNG
   side effect is lane state, not an LREG value.  Excluded:
   - SFPMOV mod1==2 copies all lanes (SFPMOV.md), and every plain gimple
     vector copy or PHI lowers to it;
   - SFPTRANSP predicates per destination lane while reading another lane
     (SFPTRANSP.md), and the SFPSHFT2 family reads all 32 source lanes;
   - SFPCONFIG reads staging lanes 0..7 under its own Imm16 gating, not
     the CC mask.  */

bool
remat_consumer_audited_p (gimple *stmt, tree name)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (stmt);
  /* A live-value operand is tied to the destination, so the consumer's
     CC-disabled result lanes are this operand's lanes, which a clone
     that wrote only the enabled lanes would leave undefined.  */
  if (insnd->is_live ()
      && gimple_call_arg (call, insnd->live_arg ()) == name)
    return false;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpstore:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfpand:
    case rvtt_insn_data::sfpand_lv:
    case rvtt_insn_data::sfpor:
    case rvtt_insn_data::sfpor_lv:
    case rvtt_insn_data::sfpxor:
    case rvtt_insn_data::sfpxor_lv:
    case rvtt_insn_data::sfpnot:
    case rvtt_insn_data::sfpnot_lv:
    case rvtt_insn_data::sfpshft_v:
    case rvtt_insn_data::sfpshft_v_lv:
    case rvtt_insn_data::sfpshft_i:
    case rvtt_insn_data::sfpshft_i_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpstochrnd:
    case rvtt_insn_data::sfpstochrnd_lv:
    case rvtt_insn_data::sfpstochrnd_descale_i:
    case rvtt_insn_data::sfpstochrnd_descale_i_lv:
    case rvtt_insn_data::sfpstochrnd_descale_v:
    case rvtt_insn_data::sfpstochrnd_descale_v_lv:
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
      return true;

    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfple:
      {
	/* The SET_CC form (mod1 1) compares lanewise and writes LaneFlags
	   only where enabled; the other modes write a tied destination
	   (rvtt.md rvtt_sfp<gtle>_cc).  Only the VC operand
	   accepts a constant register; in VD the read would just become a
	   per-row copy.  */
	tree mod = gimple_call_arg (call, gimple_call_num_args (call) - 1);
	return TREE_CODE (mod) == INTEGER_CST && TREE_INT_CST_LOW (mod) == 1
	  && gimple_call_arg (call, 0) != name;
      }

    case rvtt_insn_data::sfpswap:
      {
	/* SFPSWAP writes both operands back, but only where enabled, and
	   drops the VC-side write for LRegs >= 8 (SFPSWAP.md).  So a
	   constant in VC is only read, provided nothing consumes the VC
	   result.  VD stays excluded: a VD of 12 or above suppresses the
	   whole instruction.  */
	if (gimple_call_arg (call, 0) == name
	    || gimple_call_arg (call, 1) != name)
	  return false;
	tree res = gimple_call_lhs (call);
	if (!res || TREE_CODE (res) != SSA_NAME)
	  return false;
	imm_use_iterator uit;
	gimple *use;
	FOR_EACH_IMM_USE_STMT (use, uit, res)
	  {
	    if (is_gimple_debug (use))
	      continue;
	    const rvtt_insn_data *used = rvtt_get_insn_data (use);
	    if (!used || used->id != rvtt_insn_data::sfpselect2
		|| !integer_zerop (gimple_call_arg (use, 1)))
	      return false;
	  }
	return true;
      }

    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
      {
	/* Predicated for mod1 0/1/8; mod1 2 copies all lanes.  */
	tree mod = gimple_call_arg (call, gimple_call_num_args (call) - 1);
	return TREE_CODE (mod) == INTEGER_CST
	  && TREE_INT_CST_LOW (mod) != SFPMOV_MOD1_ALL;
      }

    default:
      (void) name;
      return false;
    }
}

/* Clone CHAIN immediately before the statement at *GSI and return the
   fresh SSA value.  Scalar arguments are reused (their definitions
   dominate the chain's, which dominates every use); virtual operands
   are renumbered by the pass-level TODO_update_ssa_only_virtuals.  */

static tree
clone_chain_before (const remat_chain &c, gimple_stmt_iterator *gsi)
{
  tree link_value = NULL_TREE;
  if (c.root != c.tail)
    {
      gcall *root = as_a <gcall *> (gimple_copy (c.root));
      tree fresh = make_ssa_name (TREE_TYPE (gimple_call_lhs (c.root)));
      gimple_call_set_lhs (root, fresh);
      gimple_set_vdef (root, NULL_TREE);
      gimple_set_vuse (root, NULL_TREE);
      gsi_insert_before (gsi, root, GSI_SAME_STMT);
      link_value = fresh;
    }
  gcall *tail = as_a <gcall *> (gimple_copy (c.tail));
  tree fresh = make_ssa_name (TREE_TYPE (gimple_call_lhs (c.tail)));
  gimple_call_set_lhs (tail, fresh);
  gimple_set_vdef (tail, NULL_TREE);
  gimple_set_vuse (tail, NULL_TREE);
  if (link_value)
    gimple_call_set_arg (tail, 1, link_value);
  gsi_insert_before (gsi, tail, GSI_SAME_STMT);
  return fresh;
}

/* Delete an original chain whose value has been fully rematerialized.  */

static void
delete_chain (const remat_chain &c)
{
  reset_debug_uses (c.tail);
  if (c.root != c.tail)
    reset_debug_uses (c.root);
  gimple_stmt_iterator tgsi = gsi_for_stmt (c.tail);
  unlink_stmt_vdef (c.tail);
  gsi_remove (&tgsi, true);
  release_defs (c.tail);
  if (c.root != c.tail)
    {
      gimple_stmt_iterator rgsi = gsi_for_stmt (c.root);
      unlink_stmt_vdef (c.root);
      gsi_remove (&rgsi, true);
      release_defs (c.root);
    }
}

/* Rematerialize loadi-chain values at their admissible uses in FN while
   the pressure model exceeds the LREG file.  Only values live through an
   over-pressure block are considered, in SSA version order.  Return true
   if anything changed.  */

bool
remat_transform (function *fn)
{
  const unsigned capacity = rvtt_pressure_capacity ();
  rvtt_pressure_model model;
  rvtt_pressure_compute (fn, capacity, &model);
  if (model.peak <= capacity)
    {
      if (dump_file)
	fprintf (dump_file,
		 "const-remat: pressure %u within the %u-LREG file; "
		 "nothing to do\n", model.peak, capacity);
      return false;
    }
  if (dump_file)
    fprintf (dump_file, "const-remat: pressure %u exceeds the %u-LREG "
	     "file\n", model.peak, capacity);

  /* Candidates in SSA version order.  A two-issue chain's first-load
     name is itself a well-formed single-issue chain; it is the tail's
     private link, not a candidate (deleting the tail's chain releases
     it).  */
  auto_vec<tree> names;
  hash_set<tree> chain_links;
  unsigned version;
  tree name;
  FOR_EACH_SSA_NAME (version, name, fn)
    {
      remat_chain chain;
      if (!rvtt_pressure_tracked_p (name) || !remat_chain_p (name, &chain))
	continue;
      if (chain.root != chain.tail)
	chain_links.add (gimple_call_lhs (chain.root));
      /* Live through (or defined in) an over-pressure block?  */
      bool relevant = bitmap_bit_p (model.over_bbs,
				    gimple_bb (chain.tail)->index);
      if (!relevant)
	{
	  bitmap_iterator bi;
	  unsigned bbi;
	  EXECUTE_IF_SET_IN_BITMAP (model.over_bbs, 0, bbi, bi)
	    if (bbi < model.live_in.length ()
		&& model.live_in[bbi]
		&& bitmap_bit_p (model.live_in[bbi], SSA_NAME_VERSION (name)))
	      {
		relevant = true;
		break;
	      }
	}
      if (!relevant)
	continue;
      names.safe_push (name);
    }

  bool changed = false;
  unsigned last_peak = model.peak;
  for (tree cand : names)
    {
      /* Re-validate: an earlier candidate's chain deletion may have
	 released this name (its version is then in the free list and
	 its definition statement cleared).  */
      if (TREE_CODE (cand) != SSA_NAME
	  || SSA_NAME_IN_FREE_LIST (cand)
	  || !SSA_NAME_DEF_STMT (cand)
	  || chain_links.contains (cand))
	continue;
      remat_chain chain;
      if (!remat_chain_p (cand, &chain))
	continue;

      /* Gather the real uses first: cloning mutates the use list.  */
      auto_vec<gimple *> uses;
      bool kept_any = false;
      imm_use_iterator iter;
      gimple *use_stmt;
      FOR_EACH_IMM_USE_STMT (use_stmt, iter, cand)
	{
	  if (is_gimple_debug (use_stmt))
	    continue;
	  if (gimple_code (use_stmt) == GIMPLE_PHI)
	    {
	      rvtt_refuse (RVTT_REF_PHI_USE_UNCLONABLE, dump_file,
			   "const-remat: use refused (phi-use-unclonable): ");
	      if (dump_file)
		print_gimple_stmt (dump_file, use_stmt, 0);
	      kept_any = true;
	      continue;
	    }
	  if (!remat_consumer_audited_p (use_stmt, cand))
	    {
	      rvtt_refuse (RVTT_REF_CONSUMER_LANE_DISCIPLINE_UNAUDITED,
			   dump_file,
			   "const-remat: use refused "
			   "(consumer-lane-discipline-unaudited): ");
	      if (dump_file)
		print_gimple_stmt (dump_file, use_stmt, 0);
	      kept_any = true;
	      continue;
	    }
	  uses.safe_push (use_stmt);
	}
      if (uses.is_empty ())
	continue;

      for (gimple *u : uses)
	{
	  gimple_stmt_iterator ugsi = gsi_for_stmt (u);
	  tree fresh = clone_chain_before (chain, &ugsi);
	  gcall *ucall = as_a <gcall *> (u);
	  for (unsigned ix = 0; ix != gimple_call_num_args (ucall); ++ix)
	    if (gimple_call_arg (ucall, ix) == cand)
	      gimple_call_set_arg (ucall, ix, fresh);
	  update_stmt (u);
	  if (dump_file)
	    {
	      fprintf (dump_file, "const-remat: rematerialized %s before ",
		       print_generic_expr_to_str (cand));
	      print_gimple_stmt (dump_file, u, 0);
	    }
	}
      if (!kept_any)
	delete_chain (chain);
      changed = true;

      rvtt_pressure_model next;
      rvtt_pressure_compute (fn, capacity, &next);
      last_peak = next.peak;
      if (next.peak <= capacity)
	break;
    }

  if (dump_file)
    {
      if (last_peak > capacity)
	fprintf (dump_file, "const-remat: residual pressure %u exceeds %u "
		 "(lreg-pressure-unresolvable)\n", last_peak, capacity);
      else
	fprintf (dump_file, "const-remat: pressure resolved: %u -> %u\n",
		 model.peak, last_peak);
    }
  return changed;
}
