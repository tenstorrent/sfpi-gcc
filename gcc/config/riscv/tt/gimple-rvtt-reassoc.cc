/* Licensed reassociation of SFPU value chains.
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

/* Reassociation of SFPU dependence chains.

   Site 1: a maximal same-block chain of plain-mod SFPADD (or SFPMUL)
   statements linked through single-use SSA values has dependence depth
   n-1 over n terms, and the MAD unit's result latency makes that depth
   the critical path.  The chain is rebuilt as a balanced binary tree over
   the same terms in the same left-to-right order (depth ceil(log2 n),
   same statement count); the post-RA scheduler then fills the latency
   shadows with the independent subtrees.

   Site 2 (gimple-rvtt-combine.cc / rvtt.gc): fusing a multi-use SFPMUL
   into a consuming SFPADD as SFPMAD while keeping the mul for its other
   uses, so the two sets of consumers see differently rounded products.

   Site 3 (-mtt-tensix-optimize-reassoc-loop-carried): a loop-header PHI
   whose latch value chains through K plain-mod SFPADD/SFPMAD links back
   to the PHI is split into P round-robin partial accumulators.  The P-1
   new PHIs start at +0.0 and are combined by a balanced SFPADD tree on
   the single exit edge, so the recurrence bound per iteration falls from
   K * (words + latency) to ceil(K/P) * (words + latency).  This is
   modulo variable expansion (M. Lam, PLDI 1988) applied to a reduction.
   P is the minimum of the latency-derived ideal (accum_split_factor in
   rvtt-timing.h), the pressure headroom + 1, 4 and K.

   Floating-point reassociation changes rounding, so FP sites also need
   -fassociative-math; without it they refuse
   (associative-math-license-absent).  SFPIADD (mod 2^32, no CC, no
   subtract) and SFPAND/SFPOR/SFPXOR chains are associative and
   commutative, so they need only -mtt-tensix-optimize-reassoc.

   tree-ssa-reassoc.cc and the unroller's variable expansion do not apply
   because the links are SFPU builtin calls, not GIMPLE arithmetic.  They
   also do not know the real limit: the eight LREGs cannot be spilled, so
   a site whose block peak (rvtt_pressure_bb_peak) plus its new
   simultaneously live values could exceed eight refuses
   (reassoc-pressure-budget-exceeded).  A chain window must not contain a
   CC or configuration writer, or a TTREPLAY boundary, whose recorded
   content is unknown here (reassoc-cc-region-boundary,
   reassoc-replay-playback-boundary).  Every rewrite and refusal is
   reported in the dump with a "reassoc:" line.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-ssanames.h"
#include "cfgloop.h"
#include "cfganal.h"
#include "dominance.h"
#include "tree-cfg.h"
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-cc-region.h"
#include "rvtt-effects.h"
#include "rvtt-timing.h"
#include "rvtt-delivery-cost.h"
#include <unordered_map>
#include <unordered_set>

/* The pressure budget uses rvtt_pressure_bb_peak (rvtt-pressure.cc).  */

namespace {

/* Maximum number of chain terms; longer chains refuse
   (reassoc-chain-cap-exceeded).  */
constexpr unsigned REASSOC_MAX_TERMS = 64;

/* Chain operator classes.  */
enum class chain_kind { none, fp_add, fp_mul, int_add, bit_and, bit_or,
			bit_xor };

/* Return true if chain kind K is floating-point, so rebalancing it also
   requires -fassociative-math.  */

static bool
chain_kind_fp (chain_kind k)
{
  return k == chain_kind::fp_add || k == chain_kind::fp_mul;
}

/* Return the dump name of chain kind K.  */

static const char *
chain_kind_name (chain_kind k)
{
  switch (k)
    {
    case chain_kind::fp_add: return "sfpadd";
    case chain_kind::fp_mul: return "sfpmul";
    case chain_kind::int_add: return "sfpiadd";
    case chain_kind::bit_and: return "sfpand";
    case chain_kind::bit_or: return "sfpor";
    case chain_kind::bit_xor: return "sfpxor";
    default: return "?";
    }
}

/* If STMT is a chain link, return its kind and set *MOD_OUT to its mod operand
   (NULL_TREE for the bitwise ops).  Only plain (non-_lv) forms qualify, since
   the _lv forms keep disabled lanes of the destination.  FP links need mod 0;
   integer adds must not set CC or subtract.  */

static chain_kind
classify_link (gimple *stmt, tree *mod_out)
{
  gcall *call = dyn_cast<gcall *> (stmt);
  if (!call)
    return chain_kind::none;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return chain_kind::none;
  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
    return chain_kind::none;

  *mod_out = NULL_TREE;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpmul:
      {
	if (gimple_call_num_args (call) != 3)
	  return chain_kind::none;
	tree mod = gimple_call_arg (call, 2);
	if (TREE_CODE (mod) != INTEGER_CST || !integer_zerop (mod))
	  return chain_kind::none;
	*mod_out = mod;
	return insnd->id == rvtt_insn_data::sfpadd ? chain_kind::fp_add
						   : chain_kind::fp_mul;
      }
    case rvtt_insn_data::sfpiadd_v:
      {
	if (gimple_call_num_args (call) != 3)
	  return chain_kind::none;
	tree mod = gimple_call_arg (call, 2);
	if (TREE_CODE (mod) != INTEGER_CST)
	  return chain_kind::none;
	if (insnd->sets_cc (call))
	  return chain_kind::none;
	if (TREE_INT_CST_LOW (mod) & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST)
	  return chain_kind::none;
	*mod_out = mod;
	return chain_kind::int_add;
      }
    case rvtt_insn_data::sfpand:
      return chain_kind::bit_and;
    case rvtt_insn_data::sfpor:
      return chain_kind::bit_or;
    case rvtt_insn_data::sfpxor:
      return chain_kind::bit_xor;
    default:
      return chain_kind::none;
    }
}

/* Return true if VAL is defined in BB by a chain link of KIND with mod MOD and
   VAL has a single use.  */

static gcall *
link_def (tree val, chain_kind kind, tree mod, basic_block bb)
{
  if (TREE_CODE (val) != SSA_NAME)
    return nullptr;
  gimple *def = SSA_NAME_DEF_STMT (val);
  if (!def || gimple_bb (def) != bb)
    return nullptr;
  tree def_mod = NULL_TREE;
  if (classify_link (def, &def_mod) != kind)
    return nullptr;
  if (mod && def_mod
      && TREE_INT_CST_LOW (mod) != TREE_INT_CST_LOW (def_mod))
    return nullptr;
  use_operand_p use_p;
  gimple *use_stmt;
  if (!single_imm_use (val, &use_p, &use_stmt))
    return nullptr;
  return as_a<gcall *> (def);
}

struct chain
{
  gcall *root;
  chain_kind kind;
  tree mod;			/* Shared mod tree (NULL for bitwise).  */
  auto_vec<tree, 16> terms;	/* Leaf terms, source left-to-right.  */
  auto_vec<gcall *, 16> links;	/* Interior links, root excluded.  */
  unsigned depth;		/* Depth of the original expression tree.  */
  bool capped;			/* Term cap exceeded.  */
};

/* Flatten VAL into C in left-to-right term order and return the subtree depth
   (terms have depth 0).  */

static unsigned
flatten (tree val, chain *c, basic_block bb)
{
  if (c->terms.length () <= REASSOC_MAX_TERMS)
    if (gcall *def = link_def (val, c->kind, c->mod, bb))
      {
	unsigned d0 = flatten (gimple_call_arg (def, 0), c, bb);
	unsigned d1 = flatten (gimple_call_arg (def, 1), c, bb);
	c->links.safe_push (def);
	return 1 + MAX (d0, d1);
      }
  if (c->terms.length () > REASSOC_MAX_TERMS)
    c->capped = true;
  c->terms.safe_push (val);
  return 0;
}

/* Statements a chain window may contain besides the links.  The links are
   per-lane functions of their operands; the only hidden state that could
   change their meaning between the original positions and the rewrite point is
   the CC lane-enable state and the SFPU configuration.  So CC-setting
   statements, configuration writers, raw asm and calls other than rvtt
   builtins are barriers, as is a replay word.  Scalar gimple, debug statements
   and the typed load/store/address-counter builtins are transparent.  */

static const char *const window_barrier_cc = "reassoc-cc-region-boundary";
static const char *const window_barrier_replay
  = "reassoc-replay-playback-boundary";
static const char *const window_barrier_fpu
  = "reassoc-fpu-choreography-boundary";

/* The CC-region tree of the current function, built once per execute under
   -mtt-tensix-optimize-cc-region-general and null otherwise.  Statements
   created by earlier rewrites are not in it and stay barriers.  */
static rvtt_cc_region_tree *window_ccr;

/* Return true if STMT is a typed CC event confined to a proven frame strictly
   inside the window's frame WR.  That frame's popc restores the lane-enable
   state it saved, so the mask at WR's depth is the same on both sides and the
   event does not affect links in WR.  Raw asm and other calls are never
   confined.  */

static bool
window_cc_event_frame_transparent_p (gimple *stmt, rvtt_cc_region *wr)
{
  if (!window_ccr || !wr)
    return false;
  if (!rvtt_get_insn_data (stmt))
    return false;		/* raw asm / foreign call: barrier */
  rvtt_cc_region *r = window_ccr->region_of (stmt);
  if (!r || r == wr || !window_ccr->structured_p (r))
    return false;
  for (rvtt_cc_region *a = r->parent; a; a = a->parent)
    if (a == wr)
      return true;		/* strictly inside a proven child frame */
  return false;
}

/* Return the refusal name if STMT is a barrier for a window whose links are in
   frame WR (a CC event not confined inside WR, a configuration writer, a
   replay word or a matrix-unit statement), or null if STMT is transparent.  */

static const char *
window_stmt_barrier_name (gimple *stmt, rvtt_cc_region *wr)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return nullptr;
  /* CC events as classified by rvtt-cc-region.cc: raw asm, calls with unknown
     bodies, CC writers and the all-lanes SFPENCC.  An event confined inside WR
     is checked by the other cases instead.  */
  if (rvtt_cc_window_cc_event_p (stmt)
      && !window_cc_event_frame_transparent_p (stmt, wr))
    return window_barrier_cc;
  gcall *call = dyn_cast<gcall *> (stmt);
  if (!call)
    /* Plain assignments/conditions: scalar values, no SFPU state.  */
    return nullptr;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    /* Not reached (other calls are CC events above).  */
    return window_barrier_cc;
  switch (insnd->id)
    {
    /* Configuration writers refuse under the CC name.  */
    case rvtt_insn_data::sfpwriteconfig_v:
    case rvtt_insn_data::sfpconfig_i:
      return window_barrier_cc;
    /* A TTREPLAY word is a barrier: a playback delivers recorded words that
       are unknown here and may write CC or configuration, and a recording
       captures the following words, so moving a link across it would change
       the recorded program.  */
    case rvtt_insn_data::ttreplay:
      return window_barrier_replay;
    /* Matrix-unit statements (Dst/Src-bank moves, the SrcB transpose, the
       wait-gate stall, the backend-config RMW) write no LREG or CC, but they
       access Dst rows and backend configuration through state no gimple pass
       models, so treat them all as barriers.  */
    case rvtt_insn_data::ttmovd2b:
    case rvtt_insn_data::ttmovb2a:
    case rvtt_insn_data::ttmovb2d:
    case rvtt_insn_data::ttmova2d:
    case rvtt_insn_data::tttrnspsrcb:
    case rvtt_insn_data::ttstallwait:
    case rvtt_insn_data::ttrmwcib:
      return window_barrier_fpu;
    default:
      return nullptr;
    }
}

/* Return the refusal name of the first non-member statement strictly between
   FIRST and LAST (in the same block) that is a barrier, or null if there is
   none.  */

static const char *
window_barrier (chain *c, gimple *first, gimple *last)
{
  /* The window's frame, if the root and every link are proven to be in the
     same frame; otherwise null.  */
  rvtt_cc_region *wr = nullptr;
  if (window_ccr)
    {
      rvtt_cc_region *r = window_ccr->region_of (c->root);
      if (r && window_ccr->structured_p (r)
	  && window_ccr->region_of (first) == r)
	{
	  wr = r;
	  for (gcall *l : c->links)
	    if (window_ccr->region_of (l) != r)
	      {
		wr = nullptr;
		break;
	      }
	}
    }

  for (gimple_stmt_iterator gsi = gsi_for_stmt (first);
       gsi_stmt (gsi) != last; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == first || stmt == c->root)
	continue;
      bool is_link = false;
      for (gcall *l : c->links)
	if (l == stmt)
	  {
	    is_link = true;
	    break;
	  }
      if (is_link)
	continue;
      if (const char *why = window_stmt_barrier_name (stmt, wr))
	return why;
    }
  return nullptr;
}

/* Return the earliest link of C in its block.  */

static gimple *
earliest_link (chain *c)
{
  basic_block bb = gimple_bb (c->root);
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    for (gcall *l : c->links)
      if (l == gsi_stmt (gsi))
	return gsi_stmt (gsi);
  return c->root;
}

/* Emit the balanced tree over TERMS[LO,HI) depth-first before *AT and return
   its value.  The left subtree completes before the right starts, so at most
   tree-depth partial results are live at once, which is what the pressure
   budget assumes.  The split point is the ceiling half and term order is
   preserved.  MOD is the mod operand (NULL for the bitwise ops).  */

static tree
rebalance_build (const vec<tree> &terms, tree mod, gimple_stmt_iterator *at,
		 tree type, tree fndecl, unsigned lo, unsigned hi)
{
  if (hi - lo == 1)
    return terms[lo];
  unsigned mid = lo + (hi - lo + 1) / 2;
  tree l = rebalance_build (terms, mod, at, type, fndecl, lo, mid);
  tree r = rebalance_build (terms, mod, at, type, fndecl, mid, hi);
  gcall *comb = mod
    ? gimple_build_call (fndecl, 3, l, r, mod)
    : gimple_build_call (fndecl, 2, l, r);
  tree fresh = make_ssa_name (type);
  gimple_call_set_lhs (comb, fresh);
  gsi_insert_before (at, comb, GSI_SAME_STMT);
  return fresh;
}

/* As rebalance_build, but append to *SEQ for insertion on an edge (the
   loop-carried split's exit reduction).  */

static tree
reduction_build_seq (const vec<tree> &terms, tree mod, gimple_seq *seq,
		     tree type, tree fndecl, unsigned lo, unsigned hi)
{
  if (hi - lo == 1)
    return terms[lo];
  unsigned mid = lo + (hi - lo + 1) / 2;
  tree l = reduction_build_seq (terms, mod, seq, type, fndecl, lo, mid);
  tree r = reduction_build_seq (terms, mod, seq, type, fndecl, mid, hi);
  gcall *comb = mod
    ? gimple_build_call (fndecl, 3, l, r, mod)
    : gimple_build_call (fndecl, 2, l, r);
  tree fresh = make_ssa_name (type);
  gimple_call_set_lhs (comb, fresh);
  gimple_seq_add_stmt (seq, comb);
  return fresh;
}

/* Rebalance C in place: emit the balanced tree over C->terms before the root,
   make the root combine the two top subtrees, and delete the old interior
   links.  The statement count is unchanged.  */

static void
rebalance (chain *c)
{
  gimple_stmt_iterator at = gsi_for_stmt (c->root);
  tree type = TREE_TYPE (gimple_call_lhs (c->root));
  tree fndecl = gimple_call_fndecl (c->root);
  unsigned n = c->terms.length ();

  unsigned mid = (n + 1) / 2;
  tree l = rebalance_build (c->terms, c->mod, &at, type, fndecl, 0, mid);
  tree r = rebalance_build (c->terms, c->mod, &at, type, fndecl, mid, n);
  gimple_call_set_arg (c->root, 0, l);
  gimple_call_set_arg (c->root, 1, r);
  update_stmt (c->root);

  /* Delete the old interior links, consumers first.  */
  auto_vec<gcall *, 16> pending;
  for (gcall *lnk : c->links)
    pending.safe_push (lnk);
  bool progress = true;
  while (!pending.is_empty () && progress)
    {
      progress = false;
      unsigned ix = 0;
      while (ix < pending.length ())
	{
	  gcall *lnk = pending[ix];
	  tree lhs = gimple_call_lhs (lnk);
	  if (lhs && !has_zero_uses (lhs))
	    {
	      ++ix;
	      continue;
	    }
	  reset_debug_uses (lnk);
	  unlink_stmt_vdef (lnk);
	  gimple_stmt_iterator gsi = gsi_for_stmt (lnk);
	  gsi_remove (&gsi, true);
	  release_defs (lnk);
	  pending.unordered_remove (ix);
	  progress = true;
	}
    }
  gcc_assert (pending.is_empty ());
}

/* Try to rebalance the chain rooted at ROOT.  Return true if the code
   changed.  */

static bool
process_root (gcall *root, chain_kind kind, tree mod)
{
  basic_block bb = gimple_bb (root);
  chain c;
  c.root = root;
  c.kind = kind;
  c.mod = mod;
  c.depth = 0;
  c.capped = false;

  unsigned d0 = flatten (gimple_call_arg (root, 0), &c, bb);
  unsigned d1 = flatten (gimple_call_arg (root, 1), &c, bb);
  c.depth = 1 + MAX (d0, d1);

  if (c.capped)
    {
      rvtt_refuse (RVTT_REF_REASSOC_CHAIN_CAP_EXCEEDED, dump_file,
		   "reassoc: refusing %s chain in bb %d "
		   "(reassoc-chain-cap-exceeded: more than %u terms)\n",
		   chain_kind_name (kind), bb->index, REASSOC_MAX_TERMS);
      return false;
    }

  unsigned n = c.terms.length ();
  if (n < 3 || c.links.is_empty ())
    return false;
  unsigned balanced = ceil_log2 (n);
  if (balanced >= c.depth)
    return false;		/* Already balanced (or too short to gain).  */

  /* FP chains also need -fassociative-math.  Test it per chain so that the
     refusal appears in the dump.  */
  if (chain_kind_fp (kind) && !flag_associative_math)
    {
      rvtt_refuse (RVTT_REF_ASSOCIATIVE_MATH_LICENSE_ABSENT, dump_file,
		   "reassoc: refusing fp %s chain rebalance depth %u->%u in "
		   "bb %d (associative-math-license-absent: value-changing "
		   "FP reassociation needs -fassociative-math AND "
		   "-mtt-tensix-optimize-reassoc)\n",
		   chain_kind_name (kind), c.depth, balanced, bb->index);
      return false;
    }

  /* LREGs cannot be spilled.  The balanced tree keeps up to BALANCED partial
     results live where the serial chain keeps one; refuse if the block's peak
     plus that difference could exceed the register file.  */
  unsigned extra_live = balanced - 1;
  unsigned peak = rvtt_pressure_bb_peak (bb);
  if (peak + extra_live > rvtt_pressure_capacity ())
    {
      rvtt_refuse (RVTT_REF_REASSOC_PRESSURE_BUDGET_EXCEEDED, dump_file,
		   "reassoc: refusing %s chain rebalance depth %u->%u in "
		   "bb %d (reassoc-pressure-budget-exceeded: conservative "
		   "block peak %u + %u new live partials > 8 LREGs -- a "
		   "licensed transform must never make a compilable kernel "
		   "uncompilable)\n",
		   chain_kind_name (kind), c.depth, balanced, bb->index,
		   peak, extra_live);
      return false;
    }

  gimple *first = earliest_link (&c);
  if (const char *barrier = window_barrier (&c, first, root))
    {
      if (dump_file)
	{
	  if (barrier == window_barrier_replay)
	    rvtt_refuse (RVTT_REF_REASSOC_REPLAY_PLAYBACK_BOUNDARY, dump_file,
			 "reassoc: refusing %s chain rebalance in bb %d "
			 "(reassoc-replay-playback-boundary: a TTREPLAY "
			 "delivery boundary sits inside the chain window -- "
			 "recorded slot content is not derivable, so value-"
			 "order across the playback point is unproven)\n",
			 chain_kind_name (kind), bb->index);
	  else if (barrier == window_barrier_fpu)
	    rvtt_refuse (RVTT_REF_REASSOC_FPU_CHOREOGRAPHY_BOUNDARY, dump_file,
			 "reassoc: refusing %s chain rebalance in bb %d "
			 "(reassoc-fpu-choreography-boundary: a Matrix-"
			 "Unit face-transpose family statement sits inside "
			 "the chain window -- its Dst and backend-"
			 "configuration effects are unmodeled at gimple)\n",
			 chain_kind_name (kind), bb->index);
	  else
	    rvtt_refuse (RVTT_REF_REASSOC_CC_REGION_BOUNDARY, dump_file,
			 "reassoc: refusing %s chain rebalance in bb %d "
			 "(reassoc-cc-region-boundary: a CC-writing, "
			 "configuration-writing, or unaudited statement sits "
			 "inside the chain window)\n",
			 chain_kind_name (kind), bb->index);
	}
      return false;
    }

  if (dump_file)
    {
      if (chain_kind_fp (kind))
	fprintf (dump_file,
		 "reassoc: licensed rebalance depth %u->%u (%s chain of %u "
		 "terms, bb %d) (flag_associative_math && "
		 "-mtt-tensix-optimize-reassoc)\n",
		 c.depth, balanced, chain_kind_name (kind), n, bb->index);
      else
	fprintf (dump_file,
		 "reassoc: integer rebalance depth %u->%u (%s chain of %u "
		 "terms, bb %d) (value-identical: mod-2^32/bitwise "
		 "associativity; no FP license required)\n",
		 c.depth, balanced, chain_kind_name (kind), n, bb->index);
    }

  rebalance (&c);
  return true;
}

/* Without -mtt-tensix-optimize-reassoc-loop-carried, report a loop-header PHI
   whose latch value chains through SFPADD/SFPMAD statements back to the PHI as
   reassoc-loop-carried-underived.  */

static void
note_loop_carried (function *fn)
{
  if (number_of_loops (fn) <= 1)
    return;
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (loop->num == 0)
	continue;
      edge latch = loop_latch_edge (loop);
      if (!latch)
	continue;
      for (gphi_iterator psi = gsi_start_phis (loop->header);
	   !gsi_end_p (psi); gsi_next (&psi))
	{
	  gphi *phi = psi.phi ();
	  tree res = gimple_phi_result (phi);
	  tree lv = PHI_ARG_DEF_FROM_EDGE (phi, latch);
	  if (TREE_CODE (lv) != SSA_NAME || TREE_CODE (res) != SSA_NAME)
	    continue;
	  /* Walk up through FP accumulate ops looking for the PHI.  */
	  tree cur = lv;
	  bool saw_fp = false;
	  for (unsigned step = 0; step != 8 && TREE_CODE (cur) == SSA_NAME;
	       ++step)
	    {
	      gcall *def = dyn_cast<gcall *> (SSA_NAME_DEF_STMT (cur));
	      if (!def)
		break;
	      const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
	      if (!insnd
		  || (insnd->id != rvtt_insn_data::sfpadd
		      && insnd->id != rvtt_insn_data::sfpmad))
		break;
	      saw_fp = true;
	      unsigned acc_args
		= insnd->id == rvtt_insn_data::sfpmad ? 3 : 2;
	      tree next = NULL_TREE;
	      for (unsigned ax = 0; ax != acc_args; ++ax)
		{
		  tree a = gimple_call_arg (def, ax);
		  if (a == res)
		    {
		      rvtt_refuse (RVTT_REF_REASSOC_LOOP_CARRIED_UNDERIVED,
				   dump_file,
				   "reassoc: loop-carried accumulator "
				   "(loop %d, phi in bb %d) restructure "
				   "underived -- refusing "
				   "(reassoc-loop-carried-underived)\n",
				   loop->num, loop->header->index);
		      goto next_phi;
		    }
		  if (TREE_CODE (a) == SSA_NAME && !next)
		    next = a;
		}
	      /* Follow the accumulator position (arg 2 for mad, else the
		 first SSA operand) upward.  */
	      cur = insnd->id == rvtt_insn_data::sfpmad
		? gimple_call_arg (def, 2) : next;
	      if (!cur)
		break;
	    }
	  (void) saw_fp;
	next_phi:;
	}
    }
}

/* ==================================================================
   Loop-carried accumulator splitting
   (-mtt-tensix-optimize-reassoc-loop-carried).  Recognition follows the walk
   above, bounded by REASSOC_MAX_TERMS.  */

/* Maximum split factor: beyond four partials the recurrence is issue-bound for
   every known latency and the register cost dominates.  */
constexpr unsigned SPLIT_MAX_PARTIALS = 4;

/* If STMT is a loop-carried accumulation link, a plain-mod SFPADD (accumulator
   in operand 0 or 1) or plain-mod SFPMAD (accumulator in operand 2), return
   its insn data and set *NVAL to the number of value operands; otherwise
   return null.  */

static const rvtt_insn_data *
loop_link_classify (gimple *stmt, unsigned *nval)
{
  gcall *call = dyn_cast<gcall *> (stmt);
  if (!call)
    return nullptr;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    return nullptr;
  tree lhs = gimple_call_lhs (call);
  if (!lhs || TREE_CODE (lhs) != SSA_NAME)
    return nullptr;
  unsigned n;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpadd:
      n = 2;
      break;
    case rvtt_insn_data::sfpmad:
      n = 3;
      break;
    default:
      return nullptr;
    }
  if (gimple_call_num_args (call) != n + 1)
    return nullptr;
  tree mod = gimple_call_arg (call, n);
  if (TREE_CODE (mod) != INTEGER_CST || !integer_zerop (mod))
    return nullptr;
  *nval = n;
  return insnd;
}

/* Return true if ARG can continue the accumulator chain toward RES: it is RES
   or the result of another link in BB.  */

static bool
chain_continues_p (tree res, tree arg, basic_block bb)
{
  if (arg == res)
    return true;
  if (TREE_CODE (arg) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (arg);
  if (!def || gimple_bb (def) != bb)
    return false;
  unsigned nval;
  return loop_link_classify (def, &nval) != nullptr;
}

/* One recognized loop-carried accumulation chain.  */

struct loop_chain
{
  class loop *loop;
  gphi *phi;
  tree res, lv;			/* Header PHI result / latch value.  */
  basic_block bb;		/* The single block holding every link.  */
  auto_vec<gcall *, 16> links;	/* Execution order, first..last.  */
  auto_vec<unsigned, 16> accpos; /* Accumulator operand index per link.  */
  bool has_add;			/* Some link is SFPADD (fndecl reuse).  */
};

/* Walk the accumulator chain of PHI in LOOP from its latch value back to its
   result.  On success fill *LC with the links in execution order and return
   true; otherwise set *WHY to the reason and return false.  */

static bool
loop_chain_walk (class loop *loop, gphi *phi, loop_chain *lc,
		 const char **why)
{
  lc->loop = loop;
  lc->phi = phi;
  lc->res = gimple_phi_result (phi);
  lc->lv = PHI_ARG_DEF_FROM_EDGE (phi, loop_latch_edge (loop));
  lc->bb = nullptr;
  lc->has_add = false;
  if (TREE_CODE (lc->lv) != SSA_NAME || TREE_CODE (lc->res) != SSA_NAME)
    return *why = "latch value is not an SSA name", false;

  tree cur = lc->lv;
  unsigned steps = 0;
  while (cur != lc->res)
    {
      if (TREE_CODE (cur) != SSA_NAME)
	return *why = "chain leaves SSA", false;
      if (++steps > REASSOC_MAX_TERMS)
	return *why = "chain cap exceeded", false;
      gimple *def = SSA_NAME_DEF_STMT (cur);
      unsigned nval = 0;
      const rvtt_insn_data *insnd = def ? loop_link_classify (def, &nval)
					: nullptr;
      if (!insnd)
	return *why = "latch value does not chain through plain "
		      "sfpadd/sfpmad links", false;
      gcall *call = as_a<gcall *> (def);
      basic_block dbb = gimple_bb (def);
      if (!lc->bb)
	{
	  if (!flow_bb_inside_loop_p (loop, dbb) || dbb->loop_father != loop)
	    return *why = "links outside the loop's own body", false;
	  lc->bb = dbb;
	}
      else if (dbb != lc->bb)
	return *why = "links span blocks", false;
      if (cur != lc->lv)
	{
	  use_operand_p use_p;
	  gimple *use_stmt;
	  if (!single_imm_use (cur, &use_p, &use_stmt))
	    return *why = "an intermediate sum has other consumers", false;
	}
      unsigned accpos;
      if (nval == 3)
	{
	  /* For MAD the accumulator must be operand 2; a multiplicand is not
	     an additive recurrence.  */
	  accpos = 2;
	  for (unsigned ax = 0; ax != 2; ++ax)
	    if (gimple_call_arg (call, ax) == lc->res)
	      return *why = "accumulator feeds a non-additive operand", false;
	}
      else
	{
	  bool c0 = chain_continues_p (lc->res, gimple_call_arg (call, 0),
				       gimple_bb (call));
	  bool c1 = chain_continues_p (lc->res, gimple_call_arg (call, 1),
				       gimple_bb (call));
	  if (c0 == c1)
	    return *why = c0 ? "ambiguous accumulator operand"
			     : "broken accumulator chain", false;
	  accpos = c0 ? 0 : 1;
	}
      lc->links.safe_push (call);
      lc->accpos.safe_push (accpos);
      if (insnd->id == rvtt_insn_data::sfpadd)
	lc->has_add = true;
      cur = gimple_call_arg (call, accpos);
    }
  if (lc->links.is_empty ())
    return *why = "empty chain", false;

  /* Collected from the latch back; reverse into execution order.  */
  for (unsigned i = 0, j = lc->links.length () - 1; i < j; ++i, --j)
    {
      std::swap (lc->links[i], lc->links[j]);
      std::swap (lc->accpos[i], lc->accpos[j]);
    }

  /* The PHI result must have exactly one non-debug use, the first link's
     accumulator operand: any other use would see a partial sum after the
     split.  */
  {
    imm_use_iterator imm;
    use_operand_p use_p;
    unsigned inside = 0;
    FOR_EACH_IMM_USE_FAST (use_p, imm, lc->res)
      {
	gimple *us = USE_STMT (use_p);
	if (is_gimple_debug (us))
	  continue;
	if (us != lc->links[0]
	    || USE_FROM_PTR (use_p) != gimple_call_arg (lc->links[0],
							lc->accpos[0])
	    || ++inside > 1)
	  return *why = "accumulator PHI has uses beyond the chain", false;
      }
    if (inside != 1)
      return *why = "accumulator PHI has uses beyond the chain", false;
  }

  /* Inside the loop the latch value may feed only the header PHI; its other
     uses are outside and are redirected to the reduction.  */
  {
    imm_use_iterator imm;
    use_operand_p use_p;
    FOR_EACH_IMM_USE_FAST (use_p, imm, lc->lv)
      {
	gimple *us = USE_STMT (use_p);
	if (is_gimple_debug (us) || us == lc->phi)
	  continue;
	if (flow_bb_inside_loop_p (loop, gimple_bb (us)))
	  return *why = "running sum consumed inside the loop", false;
      }
  }
  return true;
}

/* Try to split the accumulator of PHI in LOOP.  Return true if the code
   changed.  */

static bool
split_loop_carried_phi (class loop *loop, gphi *phi)
{
  loop_chain lc;
  const char *why = nullptr;
  if (!loop_chain_walk (loop, phi, &lc, &why))
    {
      /* Report only actual accumulators (a latch value reached through at
	 least one link), as note_loop_carried does.  */
      if (lc.links.is_empty ())
	return false;
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_LOOP_SHAPE, dump_file,
		   "reassoc: refusing loop-carried split (loop %d, phi in "
		   "bb %d) (reassoc-partials-loop-shape: %s)\n",
		   loop->num, loop->header->index, why);
      return false;
    }

  unsigned k = lc.links.length ();

  /* Loop shape: one entry besides the latch, every link executed once per
     iteration, and a single exit leaving the links' block after the links, so
     the exit edge carries every partial's final value.  */
  edge entry_e = nullptr;
  {
    edge e;
    edge_iterator ei;
    FOR_EACH_EDGE (e, ei, loop->header->preds)
      if (e != loop_latch_edge (loop))
	{
	  if (entry_e)
	    {
	      entry_e = nullptr;
	      break;
	    }
	  entry_e = e;
	}
  }
  auto_vec<edge> exits = get_loop_exit_edges (loop);
  edge exit_e = exits.length () == 1 ? exits[0] : nullptr;
  if (!entry_e || !exit_e || exit_e->src != lc.bb)
    {
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_LOOP_SHAPE, dump_file,
		   "reassoc: refusing loop-carried split of %u-link chain "
		   "(loop %d, bb %d) (reassoc-partials-loop-shape: loop "
		   "entry/exit shape unproven)\n",
		   k, loop->num, lc.bb->index);
      return false;
    }

  /* The split adds identity reads before the loop and the reduction after it,
     so the CC lane-enable state and the SFPU configuration must be the same
     from the loop entry to the exit reduction: apply the window barrier check
     to the whole loop body.  */
  {
    /* The links' frame, if every link, the exit block's last statement and the
       header's first statement are proven to be in it; otherwise null.  */
    rvtt_cc_region *wr = nullptr;
    if (window_ccr && !lc.links.is_empty ())
      {
	rvtt_cc_region *r = window_ccr->region_of (lc.links[0]);
	if (r && window_ccr->structured_p (r))
	  {
	    wr = r;
	    for (gcall *l : lc.links)
	      if (window_ccr->region_of (l) != r)
		{
		  wr = nullptr;
		  break;
		}
	    gimple_stmt_iterator lsi = gsi_last_nondebug_bb (lc.bb);
	    gimple_stmt_iterator hsi = gsi_start_nondebug_bb (loop->header);
	    if (wr
		&& (gsi_end_p (lsi) || gsi_end_p (hsi)
		    || window_ccr->region_of (gsi_stmt (lsi)) != r
		    || window_ccr->region_of (gsi_stmt (hsi)) != r))
	      wr = nullptr;
	  }
      }

    basic_block *body = get_loop_body (loop);
    const char *barrier = nullptr;
    for (unsigned i = 0; !barrier && i != loop->num_nodes; ++i)
      for (gimple_stmt_iterator gsi = gsi_start_bb (body[i]);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  bool is_link = false;
	  for (gcall *l : lc.links)
	    if (l == stmt)
	      {
		is_link = true;
		break;
	      }
	  if (is_link)
	    continue;
	  if ((barrier = window_stmt_barrier_name (stmt, wr)))
	    break;
	}
    free (body);
    if (barrier)
      {
	if (barrier == window_barrier_replay)
	  rvtt_refuse (RVTT_REF_REASSOC_REPLAY_PLAYBACK_BOUNDARY,
		       dump_file,
		       "reassoc: refusing loop-carried split of %u-link "
		       "chain (loop %d, bb %d) "
		       "(reassoc-replay-playback-boundary: a TTREPLAY "
		       "delivery boundary sits inside the loop body)\n",
		       k, loop->num, lc.bb->index);
	else if (barrier == window_barrier_fpu)
	  rvtt_refuse (RVTT_REF_REASSOC_FPU_CHOREOGRAPHY_BOUNDARY,
		       dump_file,
		       "reassoc: refusing loop-carried split of %u-link "
		       "chain (loop %d, bb %d) "
		       "(reassoc-fpu-choreography-boundary: a "
		       "Matrix-Unit face-transpose family statement sits "
		       "inside the loop body)\n",
		       k, loop->num, lc.bb->index);
	else
	  rvtt_refuse (RVTT_REF_REASSOC_CC_REGION_BOUNDARY, dump_file,
		       "reassoc: refusing loop-carried split of %u-link "
		       "chain (loop %d, bb %d) "
		       "(reassoc-cc-region-boundary: a CC-writing, "
		       "configuration-writing, or unaudited statement "
		       "sits inside the loop body)\n",
		       k, loop->num, lc.bb->index);
	return false;
      }
  }

  /* The links are FP, so the split also needs -fassociative-math.  */
  if (!flag_associative_math)
    {
      rvtt_refuse (RVTT_REF_ASSOCIATIVE_MATH_LICENSE_ABSENT, dump_file,
		   "reassoc: refusing loop-carried split of %u-link chain "
		   "(loop %d, bb %d) (associative-math-license-absent: "
		   "value-changing FP reassociation needs -fassociative-math "
		   "AND -mtt-tensix-optimize-reassoc-loop-carried)\n",
		   k, loop->num, lc.bb->index);
      return false;
    }

  /* Every link's result latency must be known; the benefit computation is in
     rvtt-timing.h.  */
  int lat = 0;
  for (gcall *l : lc.links)
    {
      int ll = rvtt_builtin_result_latency (rvtt_get_insn_data (l));
      if (ll < 0)
	{
	  rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_LATENCY_UNAUDITED,
		       dump_file,
		       "reassoc: refusing loop-carried split of %u-link "
		       "chain (loop %d, bb %d) "
		       "(reassoc-partials-latency-unaudited: a link's "
		       "result latency is not on the audited record)\n",
		       k, loop->num, lc.bb->index);
	  return false;
	}
      lat = MAX (lat, ll);
    }
  int p_ideal = rvtt_timing::accum_split_factor (1, lat);

  /* A 1-link recurrence cannot be split without unrolling, and a 0-latency
     chain is already issue-bound.  */
  unsigned p_cap = MIN ((unsigned) MAX (p_ideal, 0), SPLIT_MAX_PARTIALS);
  p_cap = MIN (p_cap, k);
  if (p_cap < 2)
    {
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_UNPROFITABLE, dump_file,
		   "reassoc: refusing loop-carried split of %u-link chain "
		   "(loop %d, bb %d) (reassoc-partials-unprofitable: chain "
		   "shorter than the benefit threshold)\n",
		   k, loop->num, lc.bb->index);
      return false;
    }

  /* The P-1 extra accumulators are live across the loop body and the exit
     block until the reduction.  */
  unsigned capacity = rvtt_pressure_capacity ();
  unsigned peak = MAX (rvtt_pressure_bb_peak (lc.bb),
		       rvtt_pressure_bb_peak (exit_e->dest));
  unsigned headroom = capacity > peak ? capacity - peak : 0;
  unsigned p = MIN (p_cap, headroom + 1);
  if (p < 2)
    {
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_PRESSURE, dump_file,
		   "reassoc: refusing loop-carried split of %u-link chain "
		   "(loop %d, bb %d) (reassoc-partials-pressure: "
		   "conservative peak %u leaves no headroom for an extra "
		   "partial in the 8-LREG file)\n",
		   k, loop->num, lc.bb->index, peak);
      return false;
    }

  int64_t saving = rvtt_timing::accum_split_saving (k, p, 1, lat);
  if (saving <= 0)
    {
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_UNPROFITABLE, dump_file,
		   "reassoc: refusing loop-carried split of %u-link chain "
		   "(loop %d, bb %d) (reassoc-partials-unprofitable: no "
		   "modeled recurrence saving at P=%u)\n",
		   k, loop->num, lc.bb->index, p);
      return false;
    }

  /* ---- Commit.  ---- */

  tree type = TREE_TYPE (lc.res);
  location_t loc = gimple_location (lc.links[0]);

  /* The identity is a read of the +0.0 constant register on the entry path.
     It is itself value-changing (+0.0 + -0.0 is +0.0), which
     -fassociative-math permits.  */
  const rvtt_insn_data *zero_insnd
    = rvtt_get_insn_data (rvtt_insn_data::sfpreadlreg);
  gcc_assert (zero_insnd && zero_insnd->decl);
  gcall *zero = gimple_build_call (zero_insnd->decl, 1,
				   build_int_cst (unsigned_type_node,
						  CREG_IDX_0));
  tree zero_ssa = make_ssa_name (type);
  gimple_call_set_lhs (zero, zero_ssa);
  gimple_set_location (zero, loc);
  {
    gimple_stmt_iterator gsi = gsi_last_bb (entry_e->src);
    if (!gsi_end_p (gsi) && stmt_ends_bb_p (gsi_stmt (gsi)))
      gsi_insert_before (&gsi, zero, GSI_SAME_STMT);
    else
      gsi_insert_after (&gsi, zero, GSI_NEW_STMT);
  }

  /* Add P-1 partial-accumulator PHIs starting at the identity; the original
     PHI becomes partial 0.  */
  auto_vec<tree, 4> cur;
  cur.safe_push (lc.res);
  auto_vec<gphi *, 4> new_phis;
  for (unsigned j = 1; j != p; ++j)
    {
      gphi *pj = create_phi_node (make_ssa_name (type), loop->header);
      add_phi_arg (pj, zero_ssa, entry_e, loc);
      new_phis.safe_push (pj);
      cur.safe_push (gimple_phi_result (pj));
    }

  /* Round-robin rewire: link i accumulates into partial i mod P.  */
  for (unsigned i = 0; i != k; ++i)
    {
      unsigned j = i % p;
      gimple_call_set_arg (lc.links[i], lc.accpos[i], cur[j]);
      update_stmt (lc.links[i]);
      cur[j] = gimple_call_lhs (lc.links[i]);
    }

  /* Latch arguments: each partial cycles through its own subchain.  */
  edge latch_e = loop_latch_edge (loop);
  SET_PHI_ARG_DEF (lc.phi, latch_e->dest_idx, cur[0]);
  for (unsigned j = 1; j != p; ++j)
    add_phi_arg (new_phis[j - 1], cur[j], latch_e, loc);

  /* Insert a balanced reduction of the P exit values (CUR) on the exit edge,
     and redirect every outside use of the old sum to the total.  For a guarded
     loop that use is the exit-merge PHI's argument for the exit edge, so the
     skipped-loop path keeps the initial value.  */
  {
    auto_vec<use_operand_p, 8> out_uses;
    imm_use_iterator imm;
    use_operand_p use_p;
    FOR_EACH_IMM_USE_FAST (use_p, imm, lc.lv)
      {
	gimple *us = USE_STMT (use_p);
	if (is_gimple_debug (us) || us == lc.phi
	    || flow_bb_inside_loop_p (loop, gimple_bb (us)))
	  continue;
	out_uses.safe_push (use_p);
      }
    if (!out_uses.is_empty ())
      {
	tree red_fndecl;
	tree red_mod;
	if (lc.has_add)
	  {
	    gcall *addl = nullptr;
	    for (gcall *l : lc.links)
	      if (rvtt_get_insn_data (l)->id == rvtt_insn_data::sfpadd)
		{
		  addl = l;
		  break;
		}
	    red_fndecl = gimple_call_fndecl (addl);
	    red_mod = gimple_call_arg (addl, 2);
	  }
	else
	  {
	    const rvtt_insn_data *add_insnd
	      = rvtt_get_insn_data (rvtt_insn_data::sfpadd);
	    gcc_assert (add_insnd && add_insnd->decl);
	    red_fndecl = add_insnd->decl;
	    red_mod = build_int_cst (unsigned_type_node, 0);
	  }
	gimple_seq seq = NULL;
	auto_vec<tree, 4> terms;
	for (unsigned j = 0; j != p; ++j)
	  terms.safe_push (cur[j]);
	tree red = reduction_build_seq (terms, red_mod, &seq, type,
					red_fndecl, 0, p);
	gsi_insert_seq_on_edge (exit_e, seq);
	gsi_commit_edge_inserts ();
	for (use_operand_p u : out_uses)
	  {
	    gimple *us = USE_STMT (u);
	    SET_USE (u, red);
	    update_stmt (us);
	  }
      }
  }

  if (dump_file)
    fprintf (dump_file,
	     "reassoc: licensed loop-carried split P=%u over %u-link "
	     "sfpadd/sfpmad chain (loop %d, bb %d): modeled recurrence "
	     "%" PRId64 "->%" PRId64 " slots/iter, +%u one-time words "
	     "(%" PRId64 " centislots) (flag_associative_math && "
	     "-mtt-tensix-optimize-reassoc-loop-carried)\n",
	     p, k, loop->num, lc.bb->index,
	     (int64_t) k * (1 + lat),
	     (int64_t) k * (1 + lat) - saving,
	     2 * (p - 1),
	     rvtt_dcost_words_to_centislots
	       (2 * ((int64_t) p - 1), rvtt_delivery_cost::PLANE_RISC_PUSH));
  return true;
}

/* Try to split every loop-carried accumulator in FN.  Return true if the code
   changed.  */

static bool
split_loop_carried (function *fn)
{
  bool changed = false;
  if (number_of_loops (fn) <= 1)
    return false;
  for (class loop *loop : loops_list (fn, LI_FROM_INNERMOST))
    {
      if (loop->num == 0 || !loop_latch_edge (loop))
	continue;
      /* Collect the header PHIs first (the transform adds PHIs).  */
      auto_vec<gphi *, 8> phis;
      for (gphi_iterator psi = gsi_start_phis (loop->header);
	   !gsi_end_p (psi); gsi_next (&psi))
	phis.safe_push (psi.phi ());
      for (gphi *phi : phis)
	changed |= split_loop_carried_phi (loop, phi);
    }
  return changed;
}

/* Rebalance the straight-line chains of FN: in each block, pass every chain
   root (a link whose result is not the single-use operand of another link of
   the same kind and mod) to process_root.  Return true if the code
   changed.  */

static bool
transform (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      /* Collect the roots first, since rewriting edits the sequence.  */
      auto_vec<gcall *, 8> roots;
      auto_vec<tree, 8> mods;
      auto_vec<chain_kind, 8> kinds;
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  tree mod = NULL_TREE;
	  chain_kind kind = classify_link (gsi_stmt (gsi), &mod);
	  if (kind == chain_kind::none)
	    continue;
	  gcall *call = as_a<gcall *> (gsi_stmt (gsi));
	  tree lhs = gimple_call_lhs (call);
	  use_operand_p use_p;
	  gimple *use_stmt;
	  if (single_imm_use (lhs, &use_p, &use_stmt)
	      && gimple_bb (use_stmt) == bb)
	    {
	      tree use_mod = NULL_TREE;
	      chain_kind use_kind = classify_link (use_stmt, &use_mod);
	      if (use_kind == kind
		  && (!mod || !use_mod
		      || TREE_INT_CST_LOW (mod)
			 == TREE_INT_CST_LOW (use_mod)))
		continue;	/* Interior link; the root will reach it.  */
	    }
	  roots.safe_push (call);
	  mods.safe_push (mod);
	  kinds.safe_push (kind);
	}
      for (unsigned ix = 0; ix != roots.length (); ++ix)
	changed |= process_root (roots[ix], kinds[ix], mods[ix]);
    }
  return changed;
}

const pass_data pass_data_rvtt_reassoc =
{
  GIMPLE_PASS,
  "rvtt_reassoc",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_reassoc : public gimple_opt_pass
{
public:
  pass_rvtt_reassoc (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_reassoc, ctxt)
  {}

  bool gate (function *) final override
  {
    /* -fassociative-math is tested per FP chain;
       -mtt-tensix-optimize-reassoc-loop-carried enables the loop split.  */
    return TARGET_XTT_TENSIX
	   && (riscv_tt_opt_reassoc > 0
	       || riscv_tt_opt_reassoc_loop_carried > 0);
  }

  unsigned execute (function *fn) final override
  {
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    /* One CC-region tree per function.  Rewrites only add statements without
       CC effect and delete links, so the tree stays valid for the remaining
       statements.  */
    rvtt_cc_region_tree *ccr = nullptr;
    if (riscv_tt_opt_cc_region_general > 0)
      {
	ccr = new rvtt_cc_region_tree (fn);
	window_ccr = ccr;
      }
    bool changed = false;
    if (riscv_tt_opt_reassoc_loop_carried > 0)
      /* Split the accumulators that fit (reassoc-partials-* otherwise).  */
      changed |= split_loop_carried (fn);
    else
      /* Only report loop-carried candidates.  */
      note_loop_carried (fn);
    if (riscv_tt_opt_reassoc > 0)
      changed |= transform (fn);
    window_ccr = nullptr;
    delete ccr;
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the reassociation pass.  rvtt-passes.def places it after the CC
   analysis and before the combiner, so that new add pairs are offered to the
   mul+add->mad rule.  */

gimple_opt_pass *
make_pass_rvtt_reassoc (gcc::context *ctxt)
{
  return new pass_rvtt_reassoc (ctxt);
}
