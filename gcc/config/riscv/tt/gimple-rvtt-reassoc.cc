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
                                       restructuring class) but the
                                       derived restructure is not enabled
                                       (-mtt-tensix-optimize-reassoc-
                                       loop-carried absent): refuse
     reassoc-partials-loop-shape       loop-carried candidate does not fit
                                       the derived shape (links span
                                       blocks, multiple exits, accumulator
                                       consumed mid-sum, ...)
     reassoc-partials-latency-unaudited  a link's result latency is not on
                                       the audited record at the gimple
                                       seam: no benefit derivation
     reassoc-partials-unprofitable     chain shorter than the benefit
                                       threshold (a 1-link recurrence
                                       cannot split without unrolling;
                                       no modeled recurrence saving)
     reassoc-partials-pressure         no pressure headroom for even one
                                       extra partial accumulator

   Site 3 -- loop-carried accumulator SPLITTING (licensed; FABLE item
   #8, -mtt-tensix-optimize-reassoc-loop-carried).  A loop-header PHI
   whose latch value chains through K plain-mod SFPADD/SFPMAD links back
   to the PHI result is the classical reduction-variable-expansion
   candidate (tree-vect-loop.cc reduction chains;
   -fvariable-expansion-in-unroller): the serial recurrence bound is
   K * (words + latency) slots per iteration.  Under the token the
   chain is split into P round-robin partial accumulators -- P-1 new
   header PHIs initialized to the +0.0 constant-register identity
   (identity legality is part of the license: -0.0 + x vs x is exactly
   the divergence class the FP license ratifies) -- and reduced by a
   balanced SFPADD tree on the loop's single exit edge, cutting the
   recurrence bound to ceil(K/P) * (words + latency).  P =
   min(latency-derived ideal, pressure headroom + 1, 4, K); the
   latency-derived ideal and the modeled saving live in rvtt-timing.h
   (accum_split_factor / accum_split_saving) with the latency fact read
   once at the audited gimple seam (rvtt_builtin_result_latency); the
   one-time overhead words are priced through rvtt-delivery-cost in the
   fire dump.  FP splits need BOTH license keys (-fassociative-math +
   the token); with the token absent the standing
   reassoc-loop-carried-underived refusal continues byte-identically
   (note_loop_carried below is the historical diagnostic, kept
   verbatim).  Nothing fires that is not proven or licensed.  */

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
enum class chain_kind { none, fp_add, fp_mul, int_add, bit_and, bit_or,
			bit_xor };

/* Whether chain kind K is a floating-point class, i.e. rebalancing it
   additionally requires the -fassociative-math license.  */

static bool
chain_kind_fp (chain_kind k)
{
  return k == chain_kind::fp_add || k == chain_kind::fp_mul;
}

/* Dump name of chain kind K: the plain-form rvtt insn every link of
   such a chain is.  */

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

/* Stage-B frame transparency (laneKL, FABLE_GOES_BURR R2): the
   CC-region tree for the current function, live only under
   -mtt-tensix-optimize-cc-region-general (null otherwise -- every
   query below then fails closed to the historical barrier).  Built
   once per execute; statements minted by earlier rebalances in the
   same run are unmapped and simply keep the fail-closed verdicts.  */
static rvtt_cc_region_tree *window_ccr;

/* STMT is a typed CC event whose whole effect is confined to a
   structurally proven frame STRICTLY inside the window's own frame WR:
   the frame's popc restores the lane-enable state it saved, so the
   mask AT the window's own depth is identical on both sides -- the
   event is transparent to a code-motion window whose links all sit at
   WR (checked by the callers).  Raw asm and foreign calls stay
   barriers (they may deliver configuration or arbitrary words); the
   non-CC barrier arms (config/replay/FPU) still apply to every
   statement inside the skipped frame, each scanned on its own.  */

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

/* The window-barrier classification of STMT for a code-motion window
   whose links sit in frame WR: the refusal-name constant for a CC
   event (unless tree-proven confined strictly inside WR),
   configuration writer, replay word, or FPU face-transpose statement,
   or null when STMT is window-transparent (debug/label, scalar
   assigns/conditions, other typed rvtt calls).  */

static const char *
window_stmt_barrier_name (gimple *stmt, rvtt_cc_region *wr)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return nullptr;
  /* The CC arm is the shared CC-region analysis vocabulary
     (FABLE_GOES_BURR #14, rvtt-cc-region.cc): raw asm, calls with
     unknown bodies, CC writers, and the typed all-lanes SFPENCC --
     exactly the classification this walk historically spelled
     locally.  Stage B: an event tree-proven confined to a frame
     strictly inside WR falls through to the non-CC arms.  */
  if (rvtt_cc_window_cc_event_p (stmt)
      && !window_cc_event_frame_transparent_p (stmt, wr))
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
  /* Stage-B frame transparency context: the window's own frame, valid
     only when the root and every link provably sit in that one frame
     (same mask at every original link position and at the rewrite
     point).  Null keeps the historical verdicts fail-closed.  */
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

/* Emit the balanced tree over TERMS[lo,hi) in depth-first order
   immediately before *AT (left subtree completes before the right
   begins, so at most tree-depth partial results are ever simultaneously
   live -- the Sethi-Ullman shape the pressure budget is computed for).
   Split point = ceiling half; term order is preserved exactly.  MOD is
   the shared mod operand tree (NULL for the modless bitwise ops).
   Shared by the straight-line rebalance and the loop-carried split's
   post-loop reduction (item #8).  */

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

/* Emit the balanced tree over TERMS[lo,hi) into *SEQ (no block yet):
   the loop-carried split's post-loop reduction, built for edge
   insertion.  Same deterministic ceiling-half shape as
   rebalance_build.  */

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
  unsigned n = c->terms.length ();

  unsigned mid = (n + 1) / 2;
  tree l = rebalance_build (c->terms, c->mod, &at, type, fndecl, 0, mid);
  tree r = rebalance_build (c->terms, c->mod, &at, type, fndecl, mid, n);
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
   Loop-carried accumulator SPLITTING (FABLE_GOES_BURR item #8) --
   the derived restructure behind -mtt-tensix-optimize-reassoc-loop-
   carried.  Recognition is a widened form of the walk above (bound =
   REASSOC_MAX_TERMS, not the historical 8-step diagnostic bound, which
   note_loop_carried keeps verbatim for the token-off path); every
   non-fitting candidate refuses by name.  */

/* Hard cap on the split factor (the item-#8 plan constant): beyond
   four partials the recurrence is issue-bound for every audited
   latency on record and the register cost outweighs.  */
constexpr unsigned SPLIT_MAX_PARTIALS = 4;

/* Classify STMT as a loop-carried accumulation link: plain-mod SFPADD
   (accumulator in value operand 0 or 1) or plain-mod SFPMAD
   (accumulator strictly operand 2).  Returns the insn data and sets
   *NVAL to the value-operand count, else null.  */

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

/* Whether ARG can continue the accumulator chain toward RES: it is RES
   itself or the result of another link in BB.  */

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

/* Walk the accumulator chain of PHI from its latch value back to its
   result.  True on a complete recognition (LC filled, links in
   execution order); false with *WHY set to a dump-stable reason
   fragment on any non-fitting shape.  Fail-closed: ambiguity refuses.  */

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
	  /* MAD: the accumulator position is operand 2 strictly; the
	     PHI result in a multiplicand is not an additive
	     recurrence.  */
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

  /* Collected latch-to-header: reverse into execution order.  */
  for (unsigned i = 0, j = lc->links.length () - 1; i < j; ++i, --j)
    {
      std::swap (lc->links[i], lc->links[j]);
      std::swap (lc->accpos[i], lc->accpos[j]);
    }

  /* The PHI result must have exactly one non-debug use: the first
     link's accumulator operand.  Any other inside use observes a
     partial sum after the split; an outside use observes the value at
     an iteration BOUNDARY (not the post-link total the exit reduction
     reconstructs) -- both refuse.  */
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

  /* The latch value may feed only the header PHI inside the loop;
     everything else must sit outside (the exit consumers the reduction
     will retarget).  */
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

/* Process one loop-carried candidate under the token.  Returns true
   when code changed.  */

static bool
split_loop_carried_phi (class loop *loop, gphi *phi)
{
  loop_chain lc;
  const char *why = nullptr;
  if (!loop_chain_walk (loop, phi, &lc, &why))
    {
      /* Only diagnose candidates the recognition vocabulary sees as
	 loop-carried FP accumulators at all (a latch value chaining
	 through at least one link); everything else is not a candidate
	 and stays silent, exactly like the historical walk.  */
      if (lc.links.is_empty ())
	return false;
      rvtt_refuse (RVTT_REF_REASSOC_PARTIALS_LOOP_SHAPE, dump_file,
		   "reassoc: refusing loop-carried split (loop %d, phi in "
		   "bb %d) (reassoc-partials-loop-shape: %s)\n",
		   loop->num, loop->header->index, why);
      return false;
    }

  unsigned k = lc.links.length ();

  /* Loop shape: single entry beside the latch; every link executes
     exactly once per latching iteration (the links' block dominates
     the latch source by SSA construction); the single exit leaves from
     the links' block AFTER the links (its controlling statement is the
     block's last), so the exit edge carries every partial's final
     value and the reduction can be placed on it under the same ambient
     CC state.  */
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

  /* Window transparency over the WHOLE loop body, through the shared
     CC-region vocabulary (the same fail-closed classification as the
     straight-line window): the split leaves every link in place but
     threads new values across all of them, adds identity reads before
     the loop and the reduction after it -- the ambient CC lane-enable
     state and the SFPU configuration must therefore be invariant from
     the loop entry through the exit reduction.  Any CC event, config
     writer, replay boundary, or FPU-choreography statement anywhere in
     the body refuses by its established window name.  */
  {
    /* Stage-B frame transparency context for the whole-body walk: the
       links' one frame, valid only when every link, the exit block's
       controlling statement (the reduction's mask on the exit edge)
       and the header's first statement (the identity reads' mask on
       the entry edge) provably sit in it.  Null keeps the historical
       verdicts fail-closed.  */
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
		       "(reassoc-fpu-choreography-boundary: an X6 "
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

  /* License gate: every link is FP (SFPADD/SFPMAD), so the split needs
     BOTH keys -- the token gated the pass arm, -fassociative-math is
     tested here per candidate so the refusal is named and visible.  */
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

  /* Audited latency at the gimple seam; the benefit arithmetic lives
     in rvtt-timing.h (item #11 discipline).  Every link is a one-word
     MAD-family value op; the audited result latency must be on record
     for each.  */
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

  /* Benefit threshold before pressure: a 1-link recurrence cannot
     split without unrolling (underived), and a 0-latency chain is
     already issue-bound.  */
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

  /* Pressure budget (item #10 engine): P-1 extra accumulators are
     simultaneously live across the loop body and, until the reduction
     retires them, across the exit block.  The same conservative
     single-block peak the straight-line arm budgets against.  */
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

  /* Identity: the +0.0 constant-register read, materialized once on
     the entry path (dead on any non-loop path).  Identity legality is
     the license's -0.0 + x vs x divergence class (see the file
     head).  */
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

  /* P-1 new partial-accumulator PHIs, identity-initialized; the
     original PHI keeps the original init and becomes partial 0.  */
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

  /* Post-loop balanced reduction over the P exit values (the exit
     leaves lc.bb after every link, so cur[] holds the live values on
     the exit edge), inserted ON the single exit edge (committing may
     split a critical edge; the new block lies outside the loop and the
     hooks keep the loop structure current).  Every outside consumer of
     the old running sum is retargeted to the reduced total: for the
     canonical guarded-loop shape that consumer is the exit-merge PHI's
     exit-edge argument, which keeps the not-executed path's init value
     intact.  */
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

/* The token-on loop-carried arm: derive the split for every fitting
   candidate; refuse the rest by name.  Returns true when code
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

/* The straight-line rebalancing walk over FN: in each block, collect
   the chain roots -- a qualifying link whose result is not itself
   consumed as the single use of a same-kind, same-mod link in the
   block -- and offer each root to process_root.  Returns whether the
   IL changed.  */

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
       the straight-line flag.  The loop-carried token gates ONLY the
       split arm (one-knob-one-mechanism, item #8).  Default off: stock
       codegen is byte-identical.  */
    return TARGET_XTT_TENSIX
	   && (riscv_tt_opt_reassoc > 0
	       || riscv_tt_opt_reassoc_loop_carried > 0);
  }

  unsigned execute (function *fn) final override
  {
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    /* Stage-B frame transparency (laneKL, R2): one tree per function,
       live for both window walks only under the flag.  Rebalances
       insert CC-inert statements and delete only links, so the
       surviving frames' facts stay exact; minted statements are
       unmapped and fail closed.  */
    rvtt_cc_region_tree *ccr = nullptr;
    if (riscv_tt_opt_cc_region_general > 0)
      {
	ccr = new rvtt_cc_region_tree (fn);
	window_ccr = ccr;
      }
    bool changed = false;
    if (riscv_tt_opt_reassoc_loop_carried > 0)
      /* The derived split (item #8); every non-fitting candidate
	 refuses by its reassoc-partials-* name.  */
      changed |= split_loop_carried (fn);
    else
      /* Token absent: the standing reassoc-loop-carried-underived
	 refusal continues byte-identically (the historical
	 diagnostic walk, kept verbatim).  */
      note_loop_carried (fn);
    if (riscv_tt_opt_reassoc > 0)
      changed |= transform (fn);
    window_ccr = nullptr;
    delete ccr;
    loop_optimizer_finalize ();
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} /* anonymous namespace */

/* Instantiate the pass for its rvtt-passes.def seat: after the CC
   analysis and immediately before the combiner, so a rebalance's
   fresh add pairs are re-offered to the existing mul+add->mad
   rule.  */

gimple_opt_pass *
make_pass_rvtt_reassoc (gcc::context *ctxt)
{
  return new pass_rvtt_reassoc (ctxt);
}
