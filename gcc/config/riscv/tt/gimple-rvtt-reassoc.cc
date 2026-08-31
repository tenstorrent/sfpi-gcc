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

/* Reassociation of SFPU dependence chains -- THE LICENSED PASS.

   Charter position (owner ratification 2026-08-21).  Value-changing
   floating-point reassociation is LICENSED, never silent: rebalancing
   an FP accumulation chain changes which intermediate roundings occur
   and is therefore bit-changing (the same fact that makes GCC's own
   -fassociative-math documentation say it "may change the computation
   result").  The industry mechanism for that change is an explicit
   user opt-in, and this backend requires BOTH halves of the key:

     flag_associative_math   (-fassociative-math -- the generic opt-in)
     riscv_tt_opt_reassoc    (-mtt-tensix-optimize-reassoc -- ours)

   With either flag absent, every FP site here refuses by name and the
   generated code is byte-identical to a compiler without this pass.
   Every reassociating site prints a named "reassoc:" dump line.

   Site 1 -- FP accumulation-chain rebalance (licensed).  A maximal
   same-basic-block chain of plain-mod SFPADD (or SFPMUL) statements
   linked through single-use SSA values evaluates left-associated with
   depth n-1 over n terms; the MAD unit's result latency makes that
   depth the row's critical path.  Under the license the chain is
   rebalanced into the balanced binary tree over the SAME terms in the
   SAME left-to-right order (deterministic; no term reordering beyond
   the tree shape), depth ceil(log2 n), same statement count.  The
   post-RA list scheduler's existing chain interleaving then fills the
   result-latency shadows with the now-independent subchains -- the
   value-preserving half of the win stays where it was; only the
   value-changing rebalance lives here, under the license.

   Site 2 (in gimple-rvtt-combine.cc / rvtt.gc, same license): fusing a
   MULTI-USE SFPMUL into a consuming SFPADD as SFPMAD while keeping the
   mul for its other uses.  The default single-use mul+add->mad combine
   is the sfpi language contract; the multi-use variant makes the add's
   consumers see the singly-rounded product while the mul's other
   consumers see the doubly-rounded one -- a value divergence only the
   license admits.  (The prgm-const fused-MAD arm stays RECOGNITION-
   ONLY as shipped by laneDM: re-sourcing constant operands never needs
   to form a MAD, licensed or not.)

   Integer/bitwise chains -- PROVEN, NOT LICENSED.  Rebalancing a chain
   of SFPIADD (two's-complement mod-2^32 addition, CC-none, non-
   subtract mods only) or SFPAND/SFPOR/SFPXOR is value-identical on
   every input by associativity of the exact operator (mod-2^32
   addition and the bitwise lattice operators are associative and
   commutative; no rounding exists).  These fire under
   -mtt-tensix-optimize-reassoc alone -- no FP license required -- and
   are separately labeled in the dump ("reassoc: integer rebalance").

   Fail-closed vocabulary (named refusals; flag-off and every refusal
   path are byte-identical):
     associative-math-license-absent   FP chain found, -fassociative-math
                                       not given (the critical refusal)
     reassoc-cc-region-boundary        a CC-writing or unaudited statement
                                       sits inside the chain window
     reassoc-replay-playback-boundary  a TTREPLAY delivery boundary sits
                                       inside the chain window: recorded
                                       slot content is not derivable
                                       (playback may deliver CC/config
                                       writers; a link moved across a
                                       recording would change the
                                       recorded program), so value-order
                                       across it is unproven (lane FL,
                                       FH-3)
     reassoc-chain-cap-exceeded        more than REASSOC_MAX_TERMS terms
     reassoc-pressure-budget-exceeded  the block's conservative live-vector
                                       peak plus the rebalance's new
                                       simultaneously-live partials (or the
                                       mad-fuse's kept mul) could exceed
                                       the 8-LREG file: a licensed
                                       transform must never make a
                                       compilable kernel uncompilable
     reassoc-loop-carried-underived    loop-carried FP accumulator
                                       recognized (the welford-delta
                                       restructuring class) but no derived
                                       restructure exists yet: refuse

   The loop-carried arm is deliberately recognition-only: restructuring
   a loop-carried accumulator (multiple partial accumulators, delta
   forms) is reassociation-licensed in principle, but this pass ships
   no derivation for it, so it refuses by name where a candidate is
   seen.  Nothing fires that is not proven or licensed.  */

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
#include "rvtt.h"
#include "rvtt-pressure.h"
#include "rvtt-refuse.h"
#include "rvtt-cc-region.h"
#include <unordered_map>
#include <unordered_set>

/* The conservative single-block peak count this pass's pressure
   budget consumes (and the licensed mad-fuse in rvtt.gc shares) lives
   in the unified pressure engine, tt/rvtt-pressure.cc
   (rvtt_pressure_bb_peak; FABLE_GOES_BURR.md item #10).  */


namespace {

/* Hard cap on flattened chain terms: beyond this the chain refuses by
   name (reassoc-chain-cap-exceeded) rather than rewriting an
   arbitrarily large expression.  */
constexpr unsigned REASSOC_MAX_TERMS = 64;

/* Chain operator classes.  */
enum class chain_kind { none, fp_add, fp_mul, int_add, bit_and, bit_or, bit_xor };

static bool
chain_kind_fp (chain_kind k)
{
  return k == chain_kind::fp_add || k == chain_kind::fp_mul;
}

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

/* Classify STMT as a chain link.  Plain (non-_lv) forms only: the _lv
   variants carry lane-victim semantics that are not pure value
   functions of their operands.  FP links must be plain-mod (mod 0 --
   any COMPL/other mod bit is a different value function per operand
   position and refuses).  Integer adds must not set CC and must not
   carry the 2's-complement-subtract operand mod.  On a match, *MOD_OUT
   gets the mod operand tree (NULL_TREE for the modless bitwise ops).  */

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

/* Whether VAL's defining statement is a chain link of KIND (same mod
   value where the kind has a mod) in BB whose lhs is used exactly once
   (by the consumer that handed us VAL).  */

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
  bool capped;			/* Term cap hit: refuse by name.  */
};

/* Flatten VAL into C, returning the subtree depth (terms are depth 0).
   Left-to-right term order is preserved exactly.  */

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

/* The statements a chain window may contain besides the links
   themselves.  The links are pure per-lane value functions of their
   operands; the only hidden state that can change their meaning
   between the original link positions and the rewrite point is the CC
   lane-enable stack and the SFPU configuration.  Fail closed on
   anything that can write either: any CC-setting typed statement, the
   config writers, raw asm (undecoded words could program anything),
   and calls that are not typed rvtt builtins.  Plain scalar gimple,
   debug statements and the typed load/store/address-counter builtins
   (no CC or configuration effect) are transparent.

   Returns null when STMT is transparent, else the stable refusal name
   the window verdict prints (lane FL, FH-3: the replay owner class
   gets its own name -- a playback point is a window BARRIER).  */

static const char *const window_barrier_cc = "reassoc-cc-region-boundary";
static const char *const window_barrier_replay
  = "reassoc-replay-playback-boundary";
static const char *const window_barrier_fpu
  = "reassoc-fpu-choreography-boundary";

static const char *
window_stmt_barrier_name (gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return nullptr;
  /* The CC arm is the shared CC-region analysis vocabulary
     (FABLE_GOES_BURR #14, rvtt-cc-region.cc): raw asm, calls with
     unknown bodies, CC writers, and the typed all-lanes SFPENCC --
     exactly the classification this walk historically spelled
     locally.  */
  if (rvtt_cc_window_cc_event_p (stmt))
    return window_barrier_cc;
  gcall *call = dyn_cast<gcall *> (stmt);
  if (!call)
    /* Plain assignments/conditions: scalar values, no SFPU state.  */
    return nullptr;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    /* Unreachable (a foreign call is a CC event above); keep the
       fail-closed belt.  */
    return window_barrier_cc;
  switch (insnd->id)
    {
    /* The frame-scoped configuration writers: refuse (historically
       under the CC name).  */
    case rvtt_insn_data::sfpwriteconfig_v:
    case rvtt_insn_data::sfpconfig_i:
      return window_barrier_cc;
    /* Replay ownership (lane FL, FH-3).  A TTREPLAY word is a delivery
       boundary, both directions: a PLAYBACK re-delivers recorded slots
       whose content is not derivable here (they may carry CC or
       configuration writers -- crosscall refuses the same class by
       name, crosscall-caller-replay-unproven), and a RECORDING window
       swallows the following words, so a link moved across it would
       change the recorded program itself.  Value-order across a
       playback point is therefore unproven: the window refuses by
       name rather than rebalancing across it.  */
    case rvtt_insn_data::ttreplay:
      return window_barrier_replay;
    /* X6 FPU face-transpose family (lane FV): Matrix-Unit Dst/Src-bank
       moves, the SrcB transpose, the wait-gate stall, and the
       backend-config byte RMW.  None of them writes an LREG or the CC
       stack, but they read and write Dst rows and backend configuration
       through state no gimple layer models (ALU formats, RWC counters,
       bank validity) -- the window fails closed on the whole family
       rather than adjudicate transparency per member.  */
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

/* The refusal name of the first non-member statement strictly between
   FIRST and LAST (same BB, FIRST before LAST) that is not window-
   transparent, or null when the window is clean.  */

static const char *
window_barrier (chain *c, gimple *first, gimple *last)
{
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
      if (const char *why = window_stmt_barrier_name (stmt))
	return why;
    }
  return nullptr;
}

/* The textually earliest link of C in its BB.  */

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

/* Emit the balanced tree over C->terms[lo,hi) in depth-first order
   immediately before the root (left subtree completes before the right
   begins, so at most tree-depth partial results are ever simultaneously
   live -- the Sethi-Ullman shape the pressure budget is computed for).
   Split point = ceiling half; term order is preserved exactly.  */

static tree
rebalance_build (chain *c, gimple_stmt_iterator *at, tree type, tree fndecl,
		 unsigned nargs, unsigned lo, unsigned hi)
{
  if (hi - lo == 1)
    return c->terms[lo];
  unsigned mid = lo + (hi - lo + 1) / 2;
  tree l = rebalance_build (c, at, type, fndecl, nargs, lo, mid);
  tree r = rebalance_build (c, at, type, fndecl, nargs, mid, hi);
  gcall *comb = nargs == 3
    ? gimple_build_call (fndecl, 3, l, r, c->mod)
    : gimple_build_call (fndecl, 2, l, r);
  tree fresh = make_ssa_name (type);
  gimple_call_set_lhs (comb, fresh);
  gsi_insert_before (at, comb, GSI_SAME_STMT);
  return fresh;
}

/* Rebalance C in place: build the balanced binary tree over C->terms
   (depth-first emission, deterministic) with new statements inserted
   immediately before the root, rewrite the root's operands to the two
   top subtree values, and delete the old interior links.  Statement
   count is unchanged (n-1 combines for n terms).  */

static void
rebalance (chain *c)
{
  gimple_stmt_iterator at = gsi_for_stmt (c->root);
  tree type = TREE_TYPE (gimple_call_lhs (c->root));
  tree fndecl = gimple_call_fndecl (c->root);
  unsigned nargs = gimple_call_num_args (c->root);
  unsigned n = c->terms.length ();

  unsigned mid = (n + 1) / 2;
  tree l = rebalance_build (c, &at, type, fndecl, nargs, 0, mid);
  tree r = rebalance_build (c, &at, type, fndecl, nargs, mid, n);
  gimple_call_set_arg (c->root, 0, l);
  gimple_call_set_arg (c->root, 1, r);
  update_stmt (c->root);

  /* Delete the old interior links, consumers first (a link is
     removable once its lhs has no remaining uses).  */
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

/* Process one candidate root.  Returns true when code changed.  */

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

  /* License gate for the FP classes: BOTH flags.  The pass gate already
     requires -mtt-tensix-optimize-reassoc; test -fassociative-math HERE,
     per chain, so the refusal is named and visible.  */
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

  /* Pressure budget (corpus finding: pressure-blind rebalancing turned
     compilable Cos/Sin/I1/welford kernels into lreg-pressure-exceeded
     refusals).  The DFS-emitted balanced tree keeps up to BALANCED
     partial results simultaneously live where the serial chain keeps
     one; refuse when the block's conservative peak plus that delta
     could exceed the eight-LREG file.  Applies to the integer classes
     too -- the physics is identical.  */
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
			 "(reassoc-fpu-choreography-boundary: an X6 Matrix-"
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

/* Loop-carried FP accumulator recognition (the welford-delta
   restructuring class) -- RECOGNITION-ONLY, refuses by name.  A loop-
   header PHI whose latch value chains through SFPADD/SFPMAD statements
   back to the PHI result is a loop-carried FP accumulation; splitting
   it into partial accumulators is licensed reassociation in principle,
   but no derivation ships here, so the candidate refuses by name.  */

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
		      rvtt_refuse (RVTT_REF_REASSOC_LOOP_CARRIED_UNDERIVED, dump_file,
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

static bool
transform (function *fn)
{
  bool changed = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      /* Collect candidate roots first (rewrites edit the sequence): a
	 qualifying link whose lhs is NOT consumed as the single use of
	 another same-kind qualifying link in this BB.  */
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
    /* The target half of the license key gates the whole pass; the
       -fassociative-math half is tested per FP chain (named refusal).
       Integer/bitwise rebalancing is value-identical and needs only
       this flag.  Default off: stock codegen is byte-identical.  */
    return TARGET_XTT_TENSIX && riscv_tt_opt_reassoc > 0;
  }

  unsigned execute (function *fn) final override
  {
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    note_loop_carried (fn);
    bool changed = transform (fn);
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_reassoc (gcc::context *ctxt)
{
  return new pass_rvtt_reassoc (ctxt);
}
