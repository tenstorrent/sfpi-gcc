/* Fold predicated SFPU value merges into the consuming Dst store.
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

/* Fold a lane-predicated value merge into the Dst store that consumes
   it (-mtt-tensix-optimize-store-fold, default off).  A merge
   (sfpassign_lv) expands to a predicated SFPMOV; when its only use is an
   SFPSTORE, the store can read the merge source directly and the SFPMOV
   disappears.  Two shapes are recognized:

   S1 -- same-mask forwarding:

       r2 = sfpassign_lv (r1, z)     ; merge under the live mask M
       ... no CC or target side effects ...
       sfpstore (A, r2)              ; store under the same mask M

   The store writes only the M lanes, where r2 equals z, so
   sfpstore (A, z) is equivalent for every data format.

   S2 -- post-region store sink:

       x = sfpload (A)               ; at the enclosing mask E
       sfppushc; <mask refinement>   ; region mask M (subset of E)
       r2 = sfpassign_lv (x, z)      ; merge under M
       sfppopc                       ; state restored to E
       sfpstore (A, r2)              ; store under E

   becomes a predicated store sfpstore (A, z) under M, before the
   sfppopc.  On the lanes of E outside M the original writes
   store_convert (load_widen (d)) back over the Dst datum d, while the
   sunk store leaves d untouched.  The sink is therefore exact only when
   that round trip is the identity for the (load Mod0, store Mod0) pair.
   Each pair's round trip was checked exhaustively over every Dst bit
   pattern; the results are in rvtt-storefold-verdicts.def, and
   fold_merge_store applies them.

   M is a subset of E only if nothing between the load and the merge
   can enable lanes: SETCC/COMPC and the structured condition forms
   refine or complement within the pushed frame, while SFPENCC or a
   nested PUSHC/POPC is refused.  The store must also address the same
   Dst rows as the load, so no Dst, RWC or configuration side effects
   may intervene and the load's address mode must not increment RWC.

   S2 is a form of assignment sinking (Knoop, Ruthing and Steffen,
   "Partial dead code elimination", PLDI 1994), but its legality
   depends on the format round trip on the lanes that stop being
   written, not on liveness, which is why tree-ssa-sink.cc cannot do
   it.  The pass runs before the invariant pass, while the structured
   CC forms are intact.  */

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
#include "cfgloop.h"
#include "fold-const.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-refuse.h"
#include "rvtt-cc-region.h"

namespace {

static unsigned n_forwarded;
static unsigned n_sunk;
static unsigned n_sunk_licensed;

/* Whether the value-changing S2 sink is allowed on the float format
   pairs (-mtt-tensix-optimize-store-sink, with
   -mtt-tensix-optimize-store-fold).  */

static bool
rvtt_store_sink_licensed_p (void)
{
  return riscv_tt_opt_store_sink > 0;
}

/* SFPLOAD/SFPSTORE Mod0 data-format selectors.  */
constexpr long SFPMEM_MOD0_FMT_SRCB  = 0;
constexpr long SFPMEM_MOD0_FMT_FP16  = 1;
constexpr long SFPMEM_MOD0_FMT_BF16  = 2;
constexpr long SFPMEM_MOD0_FMT_FP32  = 3;
constexpr long SFPMEM_MOD0_FMT_INT32 = 4;
constexpr long SFPMEM_MOD0_FMT_INT32_SM = 12;

/* SFPSTORE can only store L0-L11; higher LRegs (the programmable
   constants) are not storable.  */
constexpr unsigned SFPSTORE_MAX_SRC_LREG = 12;

/* Format-pair table from rvtt-storefold-verdicts.def.  A sink row is
   FIRE for an exact
   round trip, LICENSED for the denormal-flush class (allowed only under
   -mtt-tensix-optimize-store-sink) and REFUSE otherwise; a pair without
   a row is refused.  */

/* The targets a row was checked on.  Not all rows cover both
   Blackhole and Wormhole, and a row is ignored on a target it does not
   name.  */

enum storefold_arch
{
  STOREFOLD_ARCH_BH = 1,
  STOREFOLD_ARCH_WH = 2,
  STOREFOLD_ARCH_SHARED = STOREFOLD_ARCH_BH | STOREFOLD_ARCH_WH,
  STOREFOLD_ARCH_NONE = 0
};

enum storefold_license
{
  STOREFOLD_FIRE,
  STOREFOLD_LICENSED,
  STOREFOLD_REFUSE
};

struct storefold_sink_row
{
  long lfmt, sfmt;
  storefold_license license;
  unsigned arch;
};

/* The arch bit for the target being compiled; 0 on other targets,
   which the pass gate refuses.  */

static inline unsigned
storefold_this_arch ()
{
  if (TARGET_XTT_TENSIX_BH)
    return STOREFOLD_ARCH_BH;
  if (TARGET_XTT_TENSIX_WH)
    return STOREFOLD_ARCH_WH;
  return STOREFOLD_ARCH_NONE;
}

#define RVTT_STOREFOLD_SINK_PAIR(lfmt, sfmt, verdict, divergence, license, \
				 arch)	\
  { lfmt, sfmt, STOREFOLD_##license, arch },
static constexpr storefold_sink_row storefold_sink_rows[] = {
#include "rvtt-storefold-verdicts.def"
};
#undef RVTT_STOREFOLD_SINK_PAIR
/* Record the named refusal REASON against STMT and dump it.  Returns
   false.  */

static bool
refuse (const char *reason, gimple *stmt)
{
  rvtt_refuse_by_name_at (reason, stmt, dump_file,
			  "store-fold refused (%s): ", reason);
  if (dump_file)
    print_gimple_stmt (dump_file, stmt, 0);
  return false;
}

/* Statement classification for the assign->store and load->assign
   walks.  */

static bool
inert_stmt_p (gimple *stmt)
{
  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
    return true;
  if (gimple_code (stmt) == GIMPLE_ASSIGN)
    {
      /* Scalar plumbing stays where it is; a vector-typed plain assign
	 is not part of the recognized shapes.  */
      tree lhs = gimple_get_lhs (stmt);
      return lhs && TREE_CODE (lhs) == SSA_NAME
	     && !VECTOR_TYPE_P (TREE_TYPE (lhs));
    }
  return false;
}

/* True for rvtt calls with neither CC nor other target side effects
   (pure value computations and LReg materializations).  */

static bool
pure_vector_stmt_p (gimple *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return false;
  gcall *call = as_a <gcall *> (stmt);
  return !insnd->sets_cc (call) && !insnd->has_side_effects (call);
}

/* True for the CC statements that only refine the mask within the
   current pushed frame: the structured condition forms and the raw
   SETCC/COMPC they lower to.  SFPENCC is absent (it can enable lanes
   beyond the enclosing mask), as are PUSHC/POPC (frame structure is
   handled by the callers).  */

static bool
mask_refining_stmt_p (gimple *stmt)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (stmt);
  if (!insnd)
    return false;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpxpred:
    case rvtt_insn_data::sfpxlogic:
    case rvtt_insn_data::sfpxcond:
    case rvtt_insn_data::sfpxcmp:
    case rvtt_insn_data::sfpsetcc:
    case rvtt_insn_data::sfpcompc:
      return true;
    default:
      return false;
    }
}

/* The structured v_endif marker is a dependency token, not a CC
   instruction (rvtt_sfpxpred expands to a move), and can appear
   between the merge and the closing POPC.  Accept only the depth-one
   END form whose dependency comes from the condition token.  The IF,
   ELSE and PUSH forms change the region shape and remain barriers.  */

static bool
depth_one_end_marker_p (gimple *stmt)
{
  gcall *call = rvtt_call_with_id (stmt, rvtt_insn_data::sfpxpred);
  if (!call || rvtt_call_int_arg (call, 0)
	       != (1u << SFPXPRED_MOD1_DEPTH_SHIFT))
    return false;
  tree dep = gimple_call_arg (call, 1);
  return TREE_CODE (dep) == SSA_NAME
    && rvtt_call_with_id (SSA_NAME_DEF_STMT (dep),
			 rvtt_insn_data::sfpxcond);
}

/* The merge source must be a storable LReg: reject reads of the
   programmable-constant registers above the SFPSTORE limit.  */

static bool
storable_source_p (tree z)
{
  if (TREE_CODE (z) != SSA_NAME)
    return false;
  gimple *def = SSA_NAME_DEF_STMT (z);
  if (gcall *read = rvtt_call_with_id (def, rvtt_insn_data::sfpreadlreg))
    {
      long idx = rvtt_call_int_arg (read, 0);
      if (idx < 0 || idx >= (long) SFPSTORE_MAX_SRC_LREG)
	return false;
    }
  return true;
}

/* Walk result for the assign->store span.  */

enum span_kind { SPAN_BAD, SPAN_SAME_MASK, SPAN_REGION_CLOSED };

/* Check that the CC-region tree agrees with a span the shape walk
   accepted: SAME_MASK means the store executes under the assign's own
   frame, REGION_CLOSED means the store's frame is the assign's parent
   frame and the crossed popc is that frame's exit.  A disagreement is
   an internal error under flag_checking and a refusal otherwise.  */

static bool
span_tree_agrees_p (const rvtt_cc_region_tree *ccr, span_kind kind,
		    gcall *assign, gcall *store, gcall *popc)
{
  bool agree;
  if (kind == SPAN_SAME_MASK)
    agree = ccr->same_frame_p (assign, store);
  else
    agree = ccr->parent_frame_p (store, assign)
      && ccr->closes_frame_p (popc, ccr->region_of (assign));
  if (flag_checking)
    gcc_assert (agree);
  return agree;
}

/* Scan from the statement after ASSIGN to STORE, classifying the CC
   delta.  Handles the same-block layout and the v_endif diamond (the
   counted CC-frame destructor): body block -> join with exactly two
   predecessors, the other being a popc-only block, and the store in
   the join's other successor.  POPC_OUT receives the closing popc when
   the span crosses one.  CCR must agree wherever the walk accepts
   (span_tree_agrees_p).  */

static span_kind
classify_assign_to_store (const rvtt_cc_region_tree *ccr, gcall *assign,
			  gcall *store, gcall **popc_out)
{
  *popc_out = nullptr;
  basic_block abb = gimple_bb (assign);
  basic_block sbb = gimple_bb (store);

  gimple_stmt_iterator gsi = gsi_for_stmt (assign);
  gsi_next (&gsi);

  /* Same-block prefix (runs to the store or to the block end).  */
  for (; !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == store)
	{
	  span_kind kind
	    = *popc_out ? SPAN_REGION_CLOSED : SPAN_SAME_MASK;
	  if (!span_tree_agrees_p (ccr, kind, assign, store, *popc_out))
	    return SPAN_BAD;
	  return kind;
	}
      if (inert_stmt_p (stmt))
	continue;
      if (gcall *popc = rvtt_call_with_id (stmt, rvtt_insn_data::sfppopc))
	{
	  if (*popc_out || rvtt_call_int_arg (popc, 0) != 0)
	    return SPAN_BAD;
	  *popc_out = popc;
	  continue;
	}
      if (pure_vector_stmt_p (stmt))
	continue;
      if (!*popc_out && depth_one_end_marker_p (stmt))
	continue;
      return SPAN_BAD;
    }

  if (abb == sbb)
    return SPAN_BAD;

  /* Cross-block: the v_endif diamond.  ABB must flow to a join whose
     other predecessor is a popc-only block, and the store must open
     the join's other successor.  */
  if (!single_succ_p (abb))
    return SPAN_BAD;
  basic_block join = single_succ (abb);
  if (EDGE_COUNT (join->preds) != 2 || EDGE_COUNT (join->succs) != 2)
    return SPAN_BAD;
  basic_block popc_bb = EDGE_PRED (join, 0)->src == abb
    ? EDGE_PRED (join, 1)->src : EDGE_PRED (join, 0)->src;
  gcall *popc = nullptr;
  for (gimple_stmt_iterator psi = gsi_start_bb (popc_bb); !gsi_end_p (psi);
       gsi_next (&psi))
    {
      gimple *pstmt = gsi_stmt (psi);
      if (inert_stmt_p (pstmt))
	continue;
      if (gimple_code (pstmt) == GIMPLE_COND)
	continue;
      if (gcall *pc = rvtt_call_with_id (pstmt, rvtt_insn_data::sfppopc))
	{
	  if (popc || rvtt_call_int_arg (pc, 0) != 0)
	    return SPAN_BAD;
	  popc = pc;
	  continue;
	}
      return SPAN_BAD;
    }
  if (!popc || *popc_out || !single_succ_p (popc_bb)
      || single_succ (popc_bb) != join)
    return SPAN_BAD;
  *popc_out = popc;

  basic_block store_bb = EDGE_SUCC (join, 0)->dest == popc_bb
    ? EDGE_SUCC (join, 1)->dest : EDGE_SUCC (join, 0)->dest;
  if (store_bb != sbb)
    return SPAN_BAD;
  for (gimple_stmt_iterator ssi = gsi_start_bb (sbb); !gsi_end_p (ssi);
       gsi_next (&ssi))
    {
      gimple *sstmt = gsi_stmt (ssi);
      if (sstmt == store)
	{
	  if (!span_tree_agrees_p (ccr, SPAN_REGION_CLOSED, assign, store,
				   *popc_out))
	    return SPAN_BAD;
	  return SPAN_REGION_CLOSED;
	}
      if (inert_stmt_p (sstmt))
	continue;
      return SPAN_BAD;
    }
  return SPAN_BAD;
}

/* Scan backward context for S2: from LOAD (exclusive) to ASSIGN
   (exclusive), same block: a side-effect-free prefix, then exactly one
   sfppushc (0), then mask-refining and pure statements only.  The
   CC-region tree must agree that the assign executes in exactly one
   frame opened after the load by that pushc, whose refinements are
   exactly the statements the walk accepted.  */

static bool
check_load_to_assign (const rvtt_cc_region_tree *ccr, gcall *load,
		      gcall *assign)
{
  if (gimple_bb (load) != gimple_bb (assign))
    return refuse ("store-fold-sink-region-shape", assign);
  gcall *region_pushc = nullptr;
  auto_vec<gimple *, 8> refs_seen;
  gimple_stmt_iterator gsi = gsi_for_stmt (load);
  gsi_next (&gsi);
  for (; !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == assign)
	{
	  if (!region_pushc)
	    return refuse ("store-fold-sink-region-shape", assign);
	  /* Cross-check against the CC-region tree.  */
	  rvtt_cc_region *ra = ccr->region_of (assign);
	  bool tree_ok = ra && ra->entry == region_pushc
	    && ccr->parent_frame_p (load, assign)
	    && ccr->refinements_pure_p (ra)
	    && (ccr->refinement_chain (ra).length () == refs_seen.length ()
		|| (ccr->refinement_chain (ra).length ()
		    == refs_seen.length () + 1
		    && depth_one_end_marker_p
			 (ccr->refinement_chain (ra)[refs_seen.length ()])));
	  if (tree_ok)
	    for (unsigned i = 0; i < refs_seen.length (); i++)
	      if (ccr->refinement_chain (ra)[i] != refs_seen[i])
		{
		  tree_ok = false;
		  break;
		}
	  if (flag_checking)
	    gcc_assert (tree_ok);
	  if (!tree_ok)
	    return refuse ("store-fold-sink-region-shape", assign);
	  return true;
	}
      if (inert_stmt_p (stmt))
	continue;
      if (gcall *pushc = rvtt_call_with_id (stmt, rvtt_insn_data::sfppushc))
	{
	  if (region_pushc || rvtt_call_int_arg (pushc, 0) != 0)
	    return refuse ("store-fold-sink-region-shape", stmt);
	  region_pushc = pushc;
	  continue;
	}
      if (region_pushc && mask_refining_stmt_p (stmt))
	{
	  refs_seen.safe_push (stmt);
	  continue;
	}
      if (pure_vector_stmt_p (stmt))
	continue;
      /* SFPENCC, nested frames, other Dst/RWC/config traffic, raw asm:
	 M is no longer known to be a subset of E, or the address may
	 differ.  */
      return refuse ("store-fold-sink-span-clobbered", stmt);
    }
  return refuse ("store-fold-sink-region-shape", assign);
}

/* Delete STMT from the IL: run the target's deletion bookkeeping,
   detach its virtual def, unlink it from its block and release its
   SSA definitions.  */

static void
remove_stmt (gimple *stmt)
{
  rvtt_prep_stmt_for_deletion (stmt);
  unlink_stmt_vdef (stmt);
  gimple_stmt_iterator gsi = gsi_for_stmt (stmt);
  gsi_remove (&gsi, true);
  release_defs (stmt);
}

/* Handle a store whose value operand is a single-use lane merge.
   Returns true when the program changed.  */

static bool
fold_merge_store (rvtt_cc_region_tree *ccr, gcall *assign, gcall *store)
{
  tree merged = gimple_call_arg (store, 1);
  if (!has_single_use (merged))
    return refuse ("store-fold-merge-multi-use", store);

  tree prev = gimple_call_arg (assign, 0);
  tree z = gimple_call_arg (assign, 1);
  if (TREE_CODE (z) != SSA_NAME)
    return refuse ("store-fold-source-form", assign);
  if (!storable_source_p (z))
    return refuse ("store-fold-source-not-storable", assign);

  gcall *popc = nullptr;
  span_kind kind = classify_assign_to_store (ccr, assign, store, &popc);

  if (kind == SPAN_BAD)
    return refuse ("store-fold-mask-mismatch", store);

  if (kind == SPAN_SAME_MASK)
    {
      /* S1: forward the merge source into the same-mask store.  */
      gimple_call_set_arg (store, 1, z);
      update_stmt (store);
      if (dump_file)
	{
	  fprintf (dump_file,
		   "store-fold: forwarded merge source into same-mask ");
	  print_gimple_stmt (dump_file, store, 0);
	}
      remove_stmt (assign);
      n_forwarded++;
      return true;
    }

  /* S2: the store follows the region's closing popc.  The carried value
     must be the same-address load and the format pair's Dst round trip
     must be the identity.  */
  if (TREE_CODE (prev) != SSA_NAME)
    return refuse ("store-fold-sink-carried-not-load", assign);
  gcall *load = rvtt_call_with_id (SSA_NAME_DEF_STMT (prev),
			      rvtt_insn_data::sfpload);
  if (!load)
    return refuse ("store-fold-sink-carried-not-load", assign);

  if (!operand_equal_p (gimple_call_arg (load, 0),
			gimple_call_arg (store, 0), 0)
      || !operand_equal_p (gimple_call_arg (load, 1),
			   gimple_call_arg (store, 2), 0))
    return refuse ("store-fold-sink-address-mismatch", store);

  /* The load must not advance the RWC state the store's address
     depends on (-1 means unknown).  */
  int noinc = rvtt_no_increment_address_mode ();
  if (noinc < 0 || rvtt_call_int_arg (load, 5) != noinc)
    return refuse ("store-fold-sink-addrmode-unproven", load);

  long lfmt = rvtt_call_int_arg (load, 4);
  long sfmt = rvtt_call_int_arg (store, 5);
  bool licensed = false;
  /* Look up the format pair.  A pair without a row, or with a row only
     for the other target, is refused: the (INT32,INT32) and (FP32,FP32)
     rows are Blackhole 32-bit Dst results, and Wormhole has its own
     INT32_SM row because its integer Dst path differs.  (SRCB, SRCB)
     has no row: on Blackhole SFPLOAD can take the SrcB format from
     ThreadConfig.SFPU_DEST_FMT_Base while SFPSTORE cannot, so the two
     need not resolve to the same format.  */
  const storefold_sink_row *pair = nullptr;
  unsigned this_arch = storefold_this_arch ();
  for (const storefold_sink_row &r : storefold_sink_rows)
    if (r.lfmt == lfmt && r.sfmt == sfmt && (r.arch & this_arch))
      {
	pair = &r;
	break;
      }
  if (!pair)
    return refuse ("store-fold-sink-format-unproven", store);
  switch (pair->license)
    {
    case STOREFOLD_FIRE:
      /* Exact round trip: the sink preserves values.  */
      break;

    case STOREFOLD_LICENSED:
      /* Float pairs: the store conversion flushes denormals, and every
	 round-trip mismatch is in that class.  The sunk store keeps the
	 original bits instead, a value change allowed only under
	 -mtt-tensix-optimize-store-sink.  */
      if (!rvtt_refuse_licensed (RVTT_REF_STORE_FOLD_SINK_FORMAT_CANONICALIZING,
				 rvtt_store_sink_licensed_p (),
				 /*proof_in_scope=*/true, dump_file,
				 "store-fold refused"
				 " (store-fold-sink-format-canonicalizing): "))
	{
	  if (dump_file)
	    print_gimple_stmt (dump_file, store, 0);
	  return false;
	}
      licensed = true;
      break;

    case STOREFOLD_REFUSE:
      /* The Wormhole INT32_SM pair normalizes -0, a mismatch class
	 -mtt-tensix-optimize-store-sink does not cover.  */
      return refuse ("store-fold-sink-format-canonicalizing", store);
    }

  if (!check_load_to_assign (ccr, load, assign))
    return false;

  /* Emit the predicated store of Z at the merge position; the
     all-lanes store and the merge are deleted.  */
  gcall *newstore
    = gimple_build_call (gimple_call_fndecl (store),
			 gimple_call_num_args (store),
			 gimple_call_arg (store, 0), z,
			 gimple_call_arg (store, 2),
			 gimple_call_arg (store, 3),
			 gimple_call_arg (store, 4),
			 gimple_call_arg (store, 5),
			 gimple_call_arg (store, 6));
  gimple_set_location (newstore, gimple_location (store));
  gimple_stmt_iterator at = gsi_for_stmt (assign);
  gsi_insert_after (&at, newstore, GSI_SAME_STMT);

  if (dump_file)
    {
      if (licensed)
	fprintf (dump_file,
		 "store-fold: licensed sink (-mtt-tensix-optimize-"
		 "store-sink: predicated store preserves the enabled-"
		 "complement lanes' Dst bits the all-lanes write-back "
		 "would canonicalize) of post-region store into region "
		 "as ");
      else
	fprintf (dump_file,
		 "store-fold: sank post-region store into region as ");
      print_gimple_stmt (dump_file, newstore, 0);
    }

  remove_stmt (store);
  remove_stmt (assign);
  if (licensed)
    n_sunk_licensed++;
  else
    n_sunk++;
  /* The sink created NEWSTORE, which a later candidate's span may
     reach (chained merges).  The frame structure is unchanged but the
     statement map is per-statement, so recompute it.  */
  ccr->rebuild ();
  return true;
}

/* Run the merge-into-store folds over FUN.  Returns whether the IL
   changed.  */

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;

  /* The CC-region tree (rvtt-cc-region.h), cross-checked against the
     shape walks below.  Rebuilt only after an S2 sink creates a new
     store.  */
  rvtt_cc_region_tree ccr (fun);

  /* Merge folds first, only under -mtt-tensix-optimize-store-fold...  */
  if (riscv_tt_opt_store_fold > 0)
    FOR_EACH_BB_FN (bb, fun)
      {
	gimple_stmt_iterator gsi = gsi_start_bb (bb);
	while (!gsi_end_p (gsi))
	  {
	    gimple_stmt_iterator next = gsi;
	    gsi_next (&next);
	    if (gcall *store = rvtt_call_with_id (gsi_stmt (gsi),
					     rvtt_insn_data::sfpstore))
	      {
		tree v = gimple_call_arg (store, 1);
		if (TREE_CODE (v) == SSA_NAME)
		  if (gcall *assign
		      = rvtt_call_with_id (SSA_NAME_DEF_STMT (v),
				      rvtt_insn_data::sfpassign_lv))
		    changed |= fold_merge_store (&ccr, assign, store);
	      }
	    gsi = next;
	  }
      }

  return changed;
}

const pass_data pass_data_rvtt_store_fold =
{
  GIMPLE_PASS,
  "rvtt_store_fold",
  OPTGROUP_OTHER,
  TV_NONE,
  PROP_ssa,
  0,
  0,
  0,
  0,
};

class pass_rvtt_store_fold : public gimple_opt_pass
{
public:
  pass_rvtt_store_fold (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_store_fold, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_store_fold > 0;
  }

  unsigned execute (function *fn) final override
  {
    /* The S2 round-trip sweeps cover Blackhole and Wormhole only; S1
       shares the gate.  */
    if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
      {
	rvtt_refuse (RVTT_REF_STORE_FOLD_TARGET_UNPROVEN, dump_file,
		     "store-fold refused (store-fold-target-unproven)\n");
	return 0;
      }
    n_forwarded = 0;
    n_sunk = 0;
    n_sunk_licensed = 0;
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file,
	       "store-fold: forwarded=%u sunk=%u sunk-licensed=%u\n",
	       n_forwarded, n_sunk, n_sunk_licensed);
    return changed ? TODO_update_ssa_only_virtuals : 0;
  }
};

} /* anonymous namespace */

/* Create the store-fold pass.  */

gimple_opt_pass *
make_pass_rvtt_store_fold (gcc::context *ctxt)
{
  return new pass_rvtt_store_fold (ctxt);
}
