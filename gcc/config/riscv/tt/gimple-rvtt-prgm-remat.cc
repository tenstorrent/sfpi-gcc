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

/* The constant-rematerialization transform of the PRGM constant
   pass (-mtt-tensix-optimize-const-remat) and the staged-constant
   derivation helpers: re-clone a cheap all-constant materialization
   chain next to a pressure-priced consumer instead of keeping the
   value live.  Split from gimple-rvtt-prgm-const.cc; the algorithm
   essay lives there.  */

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

/* ==================================================================== */
/* Constant residency + rematerialization.

   Motivation: the SFPU register file has eight allocatable LREGs
   (riscv.h SFPU_REG_NUM; L0..L7, hard regs 80..87) and NO spill path --
   the rvtt_sfpassign memory alternatives exist only so LRA's constraint
   matching succeeds, and selecting one is fatal at assembly output
   (rvtt.md rvtt_sfpassign; rvtt.cc rvtt_mov_error).  A function whose
   vector pressure exceeds the file therefore cannot compile at all.
   Production kernels work around this by hand: parking constants in the
   programmable constant registers (vConstFloatPrgm), or round-tripping
   values through Dst (ckernel_sfpu_trigonometry.h calculate_acosh).

   Two generic mechanisms, cheapest first:

   RESIDENCY (-mtt-tensix-optimize-const-residency): proven-constant
   values are parked in free programmable constant registers (SFPCONFIG
   dests 12..14; the same architectural facts as the M3 fusion class
   above: rvtt-mop-derive.cc LaneConfig-reset survival audit, dest-15
   word 0x910000F1 never touches LReg[11..14]).  Two admitted classes:
   - an in-loop invariant constant materialization (the loads the
     invariant pass left in a loop by LREG pressure) is programmed once
     on the loop entry edge and every use reads the constant register:
     saves two pushed SFPLOADI words per iteration for a one-time
     three-word programming cost (rvtt-cost.md delivery model: a
     RISC-pushed word ~ 1.23 replayed slots), so it pays for itself at
     two trips; a loop PROVEN single-trip refuses (a proven loss), and
     a runtime trip count is admitted (correctness is trip-independent;
     worst case one extra pushed word on a single-trip entry);
   - under LREG pressure (peak > SFPU_REG_NUM), an out-of-loop
     proven-constant value is reprogrammed in place: the programming
     writes replace the materialization and every use reads the
     constant register, which occupies NO allocatable LREG (the
     rvtt_sfpreadlreg expander emits a zero-cost cstlreg unspec for
     indices >= SFPU_CREG_IDX_LWM, folded into consumers by the unspec
     propagation passes).
   Selection is priced: in-loop candidates (per-iteration savings) rank
   above pressure-only candidates; within a class, more uses first.
   Refusals: prgm-exhausted, trip-count-single-trip, cc-region-unproven,
   qsr-unproven, plus the TU freedom-proof refusals shared with the M3
   class.

   REMATERIALIZATION (-mtt-tensix-optimize-const-remat): when vector
   pressure still exceeds the file, a value materialized by an SFPLOADI
   chain from loop-invariant scalar inputs spills as nothing and
   reloads as its SFPLOADI chain: each use gets a fresh clone of the
   chain immediately before it, and the long-lived original dies.  The
   scalar inputs live in GPRs (spillable normally), so the reload is
   always available.

   Lane-predication soundness of a rematerialized load: SFPLOADI writes
   only CC-enabled lanes (the reference simulator
   tensix.cpp:8546,8556-8568 [SIM];
   specs SFPLOADI.md:37-39 "if (VD < 8) lanewise if (LaneEnabled)"
   [SPEC]).  A clone placed immediately before its consumer executes
   under the consumer's CC state, so every lane the consumer reads AND
   commits is a lane the clone just wrote with the constant -- PROVIDED
   the consumer itself only commits CC-enabled lanes and reads operand
   lanes lane-locally.  That is the audited-consumer discipline below;
   consumers outside the audited class refuse by name
   (consumer-lane-discipline-unaudited), keeping the original
   materialization for that use.  Cross-lane readers (SFPTRANSP,
   SFPSHFT2) and all-lane writers (SFPMOV mod1==2, plain gimple vector
   copies) are structurally excluded.

   The pressure model is the gimple analogue of the invariant pass's
   loop proof (rvtt_pressure_loop_legal_p), generalized function
   wide: backward liveness of allocatable vector SSA values with a
   per-point peak.  It is a model, not the allocator: residual
   over-pressure after both mechanisms dumps a named refusal and the
   post-reload spill diagnosis (rtl-rvtt-spill-diag.cc) turns any
   actual spill into a named user error instead of an ICE.  */

/* The pressure model itself -- the width table, the tracked-value
   predicate, and the function-wide may-live computation -- is the
   promoted seed of the unified pressure engine and lives in
   tt/rvtt-pressure.cc.  */

/* An SFPLOADI materialization chain defining a vector value from
   scalar-only inputs: a single sfpxloadi/sfploadi, or an sfploadi
   followed by the sfploadi_lv upper-half merge whose live-value
   operand is the first load (the two-issue form the immvar expansion
   emits for 32-bit values, including runtime scalars synthesized
   through the instruction buffer).  Every non-link argument must be a
   scalar (constant or GPR-resident SSA value), so the chain can be
   re-issued anywhere its definition dominates.  */

/* The vector live-value link of an sfploadi_lv call (arg 1 by the
   builtin signature XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_..., rvtt-insn.def),
   or NULL_TREE.  */

tree
loadi_lv_link (gcall *call)
{
  tree arg = gimple_call_arg (call, 1);
  return VECTOR_TYPE_P (TREE_TYPE (arg)) ? arg : NULL_TREE;
}

/* Whether every argument of CALL other than SKIP is a scalar (no
   vector type), so the call reads no SFPU register state and can be
   re-issued anywhere its scalar operands' definitions dominate.  */

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

/* Recognize NAME as the value of a rematerializable materialization
   chain (see the chain comment above): a scalar-input
   sfpxloadi/sfploadi, or an sfploadi_lv upper-half merge whose
   live-value link is such a load dying into the merge.  Fills *OUT
   (TAIL defines NAME; ROOT is the first issue) and returns true.  */

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

/* The full 32-bit lane image of a single-issue constant chain, for the
   residency dedup and programming write.  The 32-bit sfpxloadi forms
   carry the fp32/int32 pattern verbatim (gimple-rvtt-immvar.cc
   emit_loadimm bits 31/-32/32); the shortened SFPLOADI FLOATB form is
   imm16 << 16 (rvtt-protos.h SFPLOADI_MOD0_FLOATB; the reference simulator
   tensix.cpp:8556-8558 [SIM]).  Other encodings refuse -- their value
   reconstruction is not on record here (they remain remat
   candidates, which re-issue verbatim and never interpret the
   value).  */

bool
constant_chain_value_p (const remat_chain &c, unsigned *value)
{
  if (c.root != c.tail)
    return false;
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

/* Forward-declared above for the fusion classes: the single-issue
   constant image of one materialization, through the same audited
   derivation as the residency chains.  */

bool
single_issue_constant_image_p (gcall *load, unsigned *value)
{
  remat_chain chain { load, load };
  return constant_chain_value_p (chain, value);
}

/* The 32-bit constant image staged into a typed SFPCONFIG write:
   STAGED's defining statement must be a single-issue admitted
   materialization with all-constant operands (the same derivation the
   residency candidates use).  Underivable staging refuses -- the
   destination's TU value stays unknown and no reuse is offered.  */

bool
staged_config_value (tree staged, unsigned *value)
{
  if (!staged || TREE_CODE (staged) != SSA_NAME)
    return false;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (staged));
  if (!def)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd
      || (insnd->id != rvtt_insn_data::sfpxloadi
	  && insnd->id != rvtt_insn_data::sfploadi))
    return false;
  remat_chain chain { def, def };
  return constant_chain_value_p (chain, value);
}

/* Audited remat consumers: instructions whose destination lanes are
   written only where CC-enabled and whose vector operand reads are
   lane-local, so a constant rematerialized immediately before them is
   observationally equivalent to the original long-lived value on every
   consumed lane.  Facts audited against the reference simulator's executors
   (src/tensix.cpp) and the ISA functional models (the SFP*.md
   functional specifications):
   - the canonical predicate is the shared mask idiom
     `cc_en ? cc : ALL` + for_each_lane (tensix.cpp:8304-8310) [SIM];
     representative spec form: SFPMAD.md:19-22 "lanewise if
     (LaneEnabled)" [SPEC];
   - per-op mask/write sites: SFPMAD :9199/:9202-9228; SFPMUL
     :9291/:9306-9309; SFPADD :9249/:9261; SFPMULI :8791/:8792-8795;
     SFPADDI :8808/:8809-8812; SFPIADD :8900/:8912-8923 (CC updates
     also enabled-lanes-only); SFPSTORE :8610-8615 (Dst rows written
     only for enabled lanes); SFPSETEXP :9142/:9153; SFPEXEXP
     :8851/:8856-8869; SFPSETMAN :9167/:9175; SFPEXMAN :8883/:8886;
     SFPSETSGN :9421/:9429; SFPABS :9035/:9047; SFPAND/OR/XOR/NOT via
     tensix_execute_sfpu_int32 :9059/:9063; SFPSHFT :8944/:8961;
     SFPCAST :9613/:9635; SFPLZ :9112/:9118-9125; SFPDIVP2
     :8826/:8837; SFPSTOCHRND :9515-9521 (write predicated; the PRNG
     side effect is lane-state, not an LREG value); SFPSWAP
     :9776/:9784-9797 (predicated, lane-local); SFPLUT :8761/:8774;
     SFPLUTFP32 :10152/:10196 [SIM].
   Structurally excluded (refuse by name):
   - SFPMOV mod1==2 copies all lanes (tensix.cpp:9008-9010;
     SFPMOV.md:29) -- and every plain gimple vector copy or PHI lowers
     to it;
   - SFPTRANSP predicates per DESTINATION lane while reading another
     lane (tensix.cpp:9488-9493; SFPTRANSP.md:44-45) and the SFPSHFT2
     family snapshots all 32 source lanes (tensix.cpp:9997-10063):
     cross-lane reads consume lanes the clone may not have written;
   - SFPCONFIG reads staging lanes 0..7 under its own Imm16 gating,
     not the CC mask (tensix.cpp:9665-9682).  */

bool
remat_consumer_audited_p (gimple *stmt, tree name)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (stmt);
  /* A live-value operand is tied to the destination (rvtt.md _lv
     alternatives constrain it "0"): the consumer's CC-DISABLED result
     lanes ARE this operand's lanes, so a clone that wrote only the
     enabled lanes would leak garbage through the merge.  Refuse the
     whole use.  (SFPSWAP is excluded from the table below for the
     same reason: both of its operands are tied in/out,
     rvtt.md rvtt_sfpswap.)  */
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
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
      return true;

    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
      {
	/* Predicated for mod1 0/1/8; mod1 2 copies all lanes
	   (tensix.cpp:9007-9022) [SIM].  */
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

/* Rematerialize loadi-chain values at their audited uses while the
   pressure model exceeds the LREG file.  Only values live through an
   over-pressure block are touched, in SSA version order (deterministic
   and source-stable).  */

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
