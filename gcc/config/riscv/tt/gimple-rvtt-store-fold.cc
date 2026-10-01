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

/* -mtt-tensix-optimize-store-fold (default off).

   Fold a lane-predicated value merge into the Dst store that consumes
   it.  A merge (sfpassign_lv) expands to a predicated SFPMOV; when its
   only use is an SFPSTORE, the store can read the merge source directly
   and the SFPMOV disappears.  No LReg live range is created or
   extended.  Two shapes are recognized:

   S1 -- same-mask forwarding:

       r2 = sfpassign_lv (r1, z)     ; merge under the live mask M
       ... no CC or target side effects ...
       sfpstore (A, r2)              ; store under the SAME mask M

   The store writes only the M lanes, and on those lanes r2 equals z, so
   sfpstore (A, z) is equivalent for every data format and the merge is
   then dead.  This follows from the lane-masked semantics of
   sfpassign_lv and sfpstore alone; no format table is involved.

   S2 -- post-region store sink:

       x = sfpload (A)               ; at the enclosing mask E
       sfppushc; <mask refinement>   ; region mask M (subset of E)
       r2 = sfpassign_lv (x, z)      ; merge under M
       sfppopc                       ; state restored to E
       sfpstore (A, r2)              ; store under E

   becomes a predicated store at the merge position:

       x = sfpload (A)
       sfppushc; <mask refinement>
       sfpstore (A, z)               ; store under M
       sfppopc

   On the M lanes both forms store z.  On the lanes of E outside M the
   original writes store_convert (load_widen (d)) back over the Dst
   datum d, while the sunk form leaves d untouched.  The sink therefore
   preserves values exactly when that round trip is the identity for
   every Dst bit pattern of the (load Mod0, store Mod0) pair.  Each pair
   is decided by the exhaustive sweep in tt/proofs/store-sink-roundtrip/,
   whose verdicts are compiled into tt/rvtt-storefold-verdicts.def:

     - (INT32, INT32), Blackhole: EQUAL over 2^32; the sink fires.
     - (FP16, FP16) and (BF16, BF16) in the 16-bit Dst layout, and
       (FP32, FP32) in the 32-bit layout on Blackhole: NOT-EQUAL.  The
       store conversion flushes denormals, and every mismatch is in
       that class.  The sink refuses
       (store-fold-sink-format-canonicalizing) unless
       -mtt-tensix-optimize-store-sink is also given; that flag accepts
       the value change, under which the complement lanes keep their
       Dst bits instead of having their denormals flushed.
     - (INT32_SM, INT32_SM), Wormhole: NOT-EQUAL in the negative-zero
       class, which -mtt-tensix-optimize-store-sink does not cover; the
       sink always refuses.
     - (SRCB, SRCB) is not swept and has no row.  SFPLOAD and SFPSTORE
       need not resolve MOD0_FMT_SRCB to the same format: on Blackhole
       SFPLOAD can take the SrcB format from
       ThreadConfig.SFPU_DEST_FMT_Base, and SFPSTORE has no such
       clause.  Of the six cross-resolution cells
       (sweep_store_sink_roundtrip.c --cross), four are width
       mismatches and two are NOT-EQUAL on 65530 of 65536 patterns,
       outside the denormal-flush class.
     - Any other pair, including mixed pairs, and any pair whose row
       was proven only on the other target, refuses
       store-fold-sink-format-unproven.

   The mask-subset argument admits only refinements between the load
   and the merge: SETCC/COMPC and the structured condition forms refine
   or complement within the pushed frame, so M stays a subset of E.
   SFPENCC can enable lanes outside E and a nested PUSHC/POPC breaks the
   single-frame restore, so both refuse.  Any statement between the
   load and the store with target side effects (other Dst traffic, RWC
   or configuration writes) refuses too, because the store must address
   the same Dst rows as the load; for the same reason the load's
   address mode must not increment RWC.

   -mtt-tensix-optimize-stochrnd-store-fold (default off).  An
   SFPSTOCHRND whose only consumer is a Dst store is a candidate for
   folding the explicit rounding into the store's own format
   conversion.  The sweep in tt/proofs/stochrnd-store-round/ shows they
   never agree: the store truncates toward zero and keeps the signs of
   -0 and denormals, while SFPSTOCHRND rounds to nearest and normalizes
   -0, denormals and NaN (2,155,741,184 of 2^32 inputs differ on the
   BF16 row, 268,435,456 on the FP16 row).  As a value-preserving fold
   every candidate therefore refuses: stochrnd-store-rounding-divergent
   for the float conversions, stochrnd-store-no-conversion-path for the
   integer ones, which no store mode performs.

   With -mtt-tensix-optimize-stochrnd-store-fold the candidate is
   instead checked as a value change onto the bare converting store.
   Each condition refuses by its own name:
     - the plain sfpstochrnd form; the _lv carriers merge under a mask
       and the descale forms are integer conversions
       (stochrnd-store-fold-carrier-unproven);
     - Mod1 exactly FP32_TO_FP16A or FP32_TO_FP16B
       (stochrnd-store-fold-format-mismatch);
     - deterministic round-to-nearest; stochastic rounding is outside
       the deterministic proof rows
       (stochrnd-store-fold-mode-unlicensed);
     - a store Mod0 of the matching precision that the hardware
       resolves statically, FP16B->BF16 or FP16A->FP16 (rows A and B
       of the proof), else stochrnd-store-fold-format-mismatch.  Mod0 0
       (SRCB) names no format: it is an indirection through the ALU
       configuration, which the compiler cannot observe, and at its
       FP32 resolution (rows C and D) the store is exact, so the fold
       would delete the rounding outright.  Rewriting the store's Mod0
       is no repair, since the FP16A store writes a 16-bit datum, which
       is wrong for a 32-bit Dst layout;
     - the round's result has the store as its only use
       (stochrnd-store-fold-multi-use);
     - the span from the round to the store is in one block and free
       of CC changes and side effects, so the store's lane mask is the
       round's (stochrnd-store-fold-span-clobbered);
     - the round's input is a storable LReg
       (stochrnd-store-fold-source-not-storable).
   Even when all of these hold the fold refuses
   (stochrnd-store-fold-entropy-stream): every SFPSTOCHRND, including
   the deterministic modes, advances the hardware PRNG, and a
   function-local analysis cannot show that no later consumer, possibly
   in a caller, observes the stream.  The flag therefore never changes
   the generated code.  It enables the pass on its own; the S1/S2 folds
   still require -mtt-tensix-optimize-store-fold.  Because the typed
   convert wrapper spells its rounding through an all-lanes merge, the
   recognizer also accepts store <- sfpassign_lv <- sfpstochrnd when
   the merge satisfies the S1 same-mask contract.

   The pass runs beside the ccmask and int-abs folds, before the
   invariant pass, while the structured CC forms are intact.  Every
   miss refuses by name and leaves the IL unchanged.

   S2 is a form of assignment sinking (J. Knoop, O. Ruthing and
   B. Steffen, "Partial dead code elimination", PLDI 1994) and has the
   shape of tree-ssa-sink.cc: find the statement's single consumer and
   move to it across a side-effect-free span.  It differs in that the
   merge is not partially dead and the store moves up into a CC region
   to inherit its narrower lane mask.  Legality is then a question of
   which lanes stop being written, i.e. of the format round trip, not
   of liveness, which is why the generic sinking pass cannot do it.  */

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
static unsigned n_stochrnd_folded;

/* -mtt-tensix-optimize-store-sink allows the value-changing S2 sink on
   the float format pairs (together with -mtt-tensix-optimize-store-fold).
   Without it those sites keep their named refusal and the generated
   code is unchanged.  */

static bool
rvtt_store_sink_licensed_p (void)
{
  return riscv_tt_opt_store_sink > 0;
}

/* -mtt-tensix-optimize-stochrnd-store-fold checks SFPSTOCHRND-into-store
   candidates as a value-changing fold.  It cannot authorize deleting
   the instruction's PRNG advance, so every candidate still refuses.  */

static bool
rvtt_stochrnd_store_fold_licensed_p (void)
{
  return riscv_tt_opt_stochrnd_store_fold > 0;
}

/* SFPLOAD/SFPSTORE Mod0 data-format selectors.  */
constexpr long SFPMEM_MOD0_FMT_SRCB  = 0;
constexpr long SFPMEM_MOD0_FMT_FP16  = 1;
constexpr long SFPMEM_MOD0_FMT_BF16  = 2;
constexpr long SFPMEM_MOD0_FMT_FP32  = 3;
constexpr long SFPMEM_MOD0_FMT_INT32 = 4;
constexpr long SFPMEM_MOD0_FMT_INT32_SM = 12;

/* SFPSTORE can only store L0-L11.  A merge source pinned to a higher
   LReg (the programmable-constant registers) is not a storable
   operand.  */
constexpr unsigned SFPSTORE_MAX_SRC_LREG = 12;

/* Format-pair verdict tables, generated from the exhaustive proof
   sweeps: tt/rvtt-storefold-verdicts.def is emitted by
   genrvtt-storefold from tt/proofs/store-sink-roundtrip/RESULT.txt and
   tt/proofs/stochrnd-store-round/RESULT.txt, and the build
   byte-compares the checked-in .def against a fresh generation
   (rvtt-storefold-verdicts.chk in tt/t-riscv-tt), so the rows cannot
   drift from their RESULT files.  A sink row's class is FIRE for an
   EQUAL pair, LICENSED for the denormal-flush class (fires only under
   -mtt-tensix-optimize-store-sink) and REFUSE for any other
   divergence; a pair without a row has no proof.  */

/* The target a generated row was proven on.  The pass admits BH or
   WH; the RESULT's rows do not all cover both, so a row that does not
   name this target is no row at all (see the sweep-coverage note at
   the top of this file).  */

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

/* The arch bit for the target being compiled; 0 outside the pass gate
   (which refuses store-fold-target-unproven before any row is read).  */

static inline unsigned
storefold_this_arch ()
{
  if (TARGET_XTT_TENSIX_BH)
    return STOREFOLD_ARCH_BH;
  if (TARGET_XTT_TENSIX_WH)
    return STOREFOLD_ARCH_WH;
  return STOREFOLD_ARCH_NONE;
}

struct stochrnd_store_row
{
  long conv, sfmt;
};

#define RVTT_STOREFOLD_PROOF(path, sha256)
#define RVTT_STOREFOLD_SINK_PAIR(lfmt, sfmt, verdict, divergence, license, \
				 arch, rsha, isha)	\
  { lfmt, sfmt, STOREFOLD_##license, arch },
#define RVTT_STOCHRND_STORE_PAIR(conv, sfmt, fsha, dsha)
static constexpr storefold_sink_row storefold_sink_rows[] = {
#include "rvtt-storefold-verdicts.def"
};
#undef RVTT_STOREFOLD_SINK_PAIR
#undef RVTT_STOCHRND_STORE_PAIR

#define RVTT_STOREFOLD_SINK_PAIR(lfmt, sfmt, verdict, divergence, license, \
				 arch, rsha, isha)
#define RVTT_STOCHRND_STORE_PAIR(conv, sfmt, fsha, dsha) { conv, sfmt },
static constexpr stochrnd_store_row stochrnd_store_rows[] = {
#include "rvtt-storefold-verdicts.def"
};
#undef RVTT_STOCHRND_STORE_PAIR
#undef RVTT_STOREFOLD_SINK_PAIR
#undef RVTT_STOREFOLD_PROOF
/* Book the named refusal REASON against STMT and dump it.  Always
   returns false so recognizers can bail with `return refuse
   (...)'.  */

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

/* True for the CC statements that only REFINE the mask within the
   current pushed frame: the structured condition forms and the raw
   SETCC/COMPC they lower to.  SFPENCC is deliberately absent (it can
   enable lanes beyond the enclosing mask), as are PUSHC/POPC (frame
   structure is handled explicitly by the callers).  */

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

/* The structured v_endif marker is a dependency token, not a delivered
   CC instruction (rvtt_sfpxpred expands to a move).  It can appear between
   the merge and the closing POPC after the explicit-predicate ABI change.
   Admit only the depth-one END form used by this pass's single-frame S2
   recognizer, with a dependency produced by the condition token.  The IF,
   ELSE and PUSH forms still change the region shape and remain barriers.  */

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

/* The merge source must expand to a storable LReg: reject sources
   pinned above the architectural store ceiling (reads of the
   programmable-constant residency registers).  */

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
   frame and the crossed popc is that frame's recorded exit.  A
   disagreement is an internal error under flag_checking and otherwise
   fails closed to the walk's refusal.  */

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
   sfppushc (0), then mask-refining and pure statements only.  Where
   the walk admits, the CC-region tree must agree that the assign
   executes under exactly one frame opened after the load (the load's
   frame is its parent frame), entered by the pushc the walk found,
   whose refinement chain is exactly the mask-refining statements the
   walk admitted.  */

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
	  /* Cross-check against the CC-region tree; a disagreement is
	     an internal error under flag_checking and otherwise
	     fails closed.  */
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
	 the mask-subset or address-identity premise is gone.  */
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
     must be proven the identity.  */
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
     depends on (capability fact; -1 = unproven, refuse).  */
  int noinc = rvtt_no_increment_address_mode ();
  if (noinc < 0 || rvtt_call_int_arg (load, 5) != noinc)
    return refuse ("store-fold-sink-addrmode-unproven", load);

  long lfmt = rvtt_call_int_arg (load, 4);
  long sfmt = rvtt_call_int_arg (store, 5);
  bool licensed = false;
  /* Format-pair admission by the generated verdict table (one row per
     exhaustively swept Dst round trip; tt/rvtt-storefold-verdicts.def,
     byte-checked against
     tt/proofs/store-sink-roundtrip/RESULT.txt every build).  A pair
     without a row has no round-trip proof, and neither does a pair
     whose row was proven only on the other target: the (INT32,INT32)
     and (FP32,FP32) rows are Blackhole Dst32b results, and the
     Wormhole INT32_SM row exists because Wormhole's integer Dst path
     differs from Blackhole's.  */
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
      /* EQUAL over the full input space: the sink is value-preserving
	 (the (INT32,INT32) BH raw pair).  */
      break;

    case STOREFOLD_LICENSED:
      /* Float pairs canonicalize Dst (the store conversion flushes
	 denormals; every round-trip mismatch is in the denormal class:
	 the NOT-EQUAL static-format float rows).  The predicated-store
	 form preserves those bits instead, a value change accepted only
	 under -mtt-tensix-optimize-store-sink.  Without it this is the
	 standing named refusal and the code is unchanged
	 (rvtt_refuse_licensed enforces both).  */
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
      /* The WH INT32_SM pair normalizes -0 ((12,12) row): an integer
	 sign-magnitude divergence class the store-sink license does
	 NOT cover (it is scoped to the float pairs' denormal-flush
	 class).  Refuses with or without the license token.  */
      return refuse ("store-fold-sink-format-canonicalizing", store);
    }

  if (!check_load_to_assign (ccr, load, assign))
    return false;

  /* Commit: predicated store of Z at the merge position; the original
     all-lanes store and the merge disappear.  */
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
  /* The sink minted NEWSTORE, a statement the frame analysis must be
     able to answer for if a later candidate's span reaches it (chained
     merges).  Frame structure itself did not move (the region's
     pushc/popc stand), but the statement map is per-statement:
     recompute.  */
  ccr->rebuild ();
  return true;
}

/* SFPSTOCHRND-into-store candidate handling.  The store's value
   operand is the rounding instruction's result.  Per
   tt/proofs/stochrnd-store-round/ no store conversion path reproduces
   any SFPSTOCHRND conversion, so as a value-preserving fold every
   instance refuses by name.  The license token can classify the
   deterministic-nearest matching-format candidate, but still refuses
   its hidden PRNG advance; it does not delete the round.  */

/* Per-variant argument positions: (mod1, rnd) gimple arg indices, or
   {-1,-1} when the variant is not an SFPSTOCHRND.  */

static bool
stochrnd_args (const rvtt_insn_data *insnd, int *mod1_pos, int *rnd_pos)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpstochrnd:
      *mod1_pos = 1; *rnd_pos = 2; return true;
    case rvtt_insn_data::sfpstochrnd_lv:
      *mod1_pos = 2; *rnd_pos = 3; return true;
    case rvtt_insn_data::sfpstochrnd_descale_i:
      *mod1_pos = 5; *rnd_pos = 6; return true;
    case rvtt_insn_data::sfpstochrnd_descale_i_lv:
      *mod1_pos = 6; *rnd_pos = 7; return true;
    case rvtt_insn_data::sfpstochrnd_descale_v:
      *mod1_pos = 2; *rnd_pos = 3; return true;
    case rvtt_insn_data::sfpstochrnd_descale_v_lv:
      *mod1_pos = 3; *rnd_pos = 4; return true;
    default:
      return false;
    }
}


/* Scan the same-block span from RND (exclusive) to STORE (exclusive):
   only CC-inert, side-effect-free statements may intervene, so the
   store's lane mask provably equals the round's.  */

static bool
stochrnd_span_inert_p (gcall *rnd, gcall *store)
{
  if (gimple_bb (rnd) != gimple_bb (store))
    return false;
  gimple_stmt_iterator gsi = gsi_for_stmt (rnd);
  gsi_next (&gsi);
  for (; !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == store)
	return true;
      if (inert_stmt_p (stmt) || pure_vector_stmt_p (stmt))
	continue;
      return false;
    }
  return false;
}

/* Handle a store whose value operand is an SFPSTOCHRND result --
   directly, or through a single all-lanes value merge (WRAP, the
   typed convert wrapper's assign_lv) when the merge satisfies the S1
   same-mask dataflow contract.  Every candidate currently refuses by
   name with the program bytes unchanged.  */

static bool
fold_stochrnd_store (rvtt_cc_region_tree *ccr, gcall *rnd, gcall *store,
		     gcall *wrap)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (rnd);
  int mod1_pos, rnd_pos;
  if (!stochrnd_args (insnd, &mod1_pos, &rnd_pos))
    return false;
  long mod1 = rvtt_call_int_arg (rnd, mod1_pos);
  unsigned conv = (unsigned) mod1 & SFPSTOCHRND_MOD1_CONV_MASK;
  bool float_row = mod1 >= 0
    && (conv == SFPSTOCHRND_MOD1_FP32_TO_FP16A
	|| conv == SFPSTOCHRND_MOD1_FP32_TO_FP16B);

  if (!float_row)
    return refuse ("stochrnd-store-no-conversion-path", store);

  if (!rvtt_stochrnd_store_fold_licensed_p ())
    /* The standing named refusal: as a value-preserving fold the cut
       is proven divergent (tt/proofs/stochrnd-store-round/).  */
    return refuse ("stochrnd-store-rounding-divergent", store);

  /* Under -mtt-tensix-optimize-stochrnd-store-fold each check refuses
     by its own name.  */
  if (insnd->id != rvtt_insn_data::sfpstochrnd)
    /* Carriers merge under a mask, while descale forms belong to a
       different conversion class.  */
    return refuse ("stochrnd-store-fold-carrier-unproven", store);

  /* The float class carries no extra mod1 bits (IMM8 is INT32-only);
     anything else is outside the proof rows.  */
  if (mod1 != (long) SFPSTOCHRND_MOD1_FP32_TO_FP16A
      && mod1 != (long) SFPSTOCHRND_MOD1_FP32_TO_FP16B)
    return refuse ("stochrnd-store-fold-format-mismatch", store);

  long rnd_mode = rvtt_call_int_arg (rnd, rnd_pos);
  if (rnd_mode != (long) SFPSTOCHRND_RND_EVEN)
    /* Stochastic (and the BH round-to-zero mode, and any non-constant
       mode) is not the proof's deterministic-nearest class.  */
    return refuse ("stochrnd-store-fold-mode-unlicensed", store);

  long mod0 = rvtt_call_int_arg (store, 5);
  /* Matching-precision pairing by the GENERATED table: the swept
     stochrnd proof rows that a STATICALLY resolved store Mod0 keys
     (tt/rvtt-storefold-verdicts.def, byte-checked against
     tt/proofs/stochrnd-store-round/RESULT.txt every build).
     Cross-precision static pairs have no row, and neither does the
     runtime-resolved SRCB store: the proof sweeps it (rows C and D)
     and refuses it, because an indirection is not one function --
     its FP32 resolution performs no conversion, so the fold there
     deletes the rounding rather than substituting the store's.  */
  bool pair_ok = false;
  for (const stochrnd_store_row &r : stochrnd_store_rows)
    if (r.conv == mod1 && r.sfmt == mod0)
      {
	pair_ok = true;
	break;
      }
  if (!pair_ok)
    return refuse ("stochrnd-store-fold-format-mismatch", store);

  tree v = gimple_call_arg (store, 1);
  if (!has_single_use (v))
    return refuse ("stochrnd-store-fold-multi-use", store);
  if (wrap)
    {
      /* The merge carries the rounded value: the round must feed ONLY
	 the merge, and the merge->store span must satisfy the S1
	 same-mask dataflow contract (the store writes exactly the
	 merge's lanes).  */
      tree z = gimple_call_arg (wrap, 1);
      if (!has_single_use (z))
	return refuse ("stochrnd-store-fold-multi-use", store);
      gcall *popc = nullptr;
      if (classify_assign_to_store (ccr, wrap, store, &popc)
	  != SPAN_SAME_MASK)
	return refuse ("stochrnd-store-fold-span-clobbered", store);
    }

  tree src = gimple_call_arg (rnd, 0);
  if (TREE_CODE (src) != SSA_NAME)
    return refuse ("stochrnd-store-fold-source-form", rnd);
  if (!storable_source_p (src))
    return refuse ("stochrnd-store-fold-source-not-storable", rnd);

  if (!stochrnd_span_inert_p (rnd, wrap ? wrap : store))
    return refuse ("stochrnd-store-fold-span-clobbered", store);

  /* Even a deterministic SFPSTOCHRND advances the hardware PRNG.  A
     function-local scan cannot observe a stochastic consumer in its
     caller after return, so deleting this word is never justified by
     the present license.  Preserve the instruction and its stream
     position until a whole-program entropy contract or an equivalent
     replacement advance is available.  */
  return refuse ("stochrnd-store-fold-entropy-stream", store);
}

/* Run the store folds over FUN: first the merge-into-store folds
   (only under the store-fold flag itself), then the
   stochrnd-into-store candidates on the settled stream -- those run
   even unlicensed so the standing named refusals are printed.  The
   CC-region tree is computed once and shared by both walks.  Returns
   whether the IL changed.  */

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;

  /* The CC-region tree: the frame structure computed once per function
     (rvtt-cc-region.h), cross-checked against the shape walks below.
     Rebuilt only after an S2 sink creates a new store statement.  */
  rvtt_cc_region_tree ccr (fun);

  /* Merge folds first (the S1/S2 flag's own transforms -- absent the
     -mtt-tensix-optimize-store-fold flag they stay off even when the
     stochrnd license opened the pass gate)...  */
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

  /* ...then the stochrnd-into-store candidates on the settled stream
     (a forwarded merge exposes the rounding instruction as the store's
     direct operand).  These only report named refusals, so they run
     whenever the pass does.  */
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
		{
		  gimple *def = SSA_NAME_DEF_STMT (v);
		  /* The typed convert wrapper spells the rounded value
		     through an all-lanes merge; under the license the
		     fold recognizes that wrapped shape directly (the
		     merge itself is validated against the S1 same-mask
		     contract inside the handler).  */
		  gcall *wrap = nullptr;
		  if (rvtt_stochrnd_store_fold_licensed_p ())
		    if (gcall *assign
			= rvtt_call_with_id (def, rvtt_insn_data::sfpassign_lv))
		      {
			tree z = gimple_call_arg (assign, 1);
			if (TREE_CODE (z) == SSA_NAME)
			  {
			    gimple *zdef = SSA_NAME_DEF_STMT (z);
			    if (rvtt_get_insn_data (zdef))
			      {
				wrap = assign;
				def = zdef;
			      }
			  }
		      }
		  const rvtt_insn_data *dinsnd = rvtt_get_insn_data (def);
		  if (dinsnd
		      && (dinsnd->id == rvtt_insn_data::sfpstochrnd
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_lv
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_descale_i
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_descale_i_lv
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_descale_v
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_descale_v_lv))
		    changed |= fold_stochrnd_store (&ccr, as_a <gcall *> (def),
					    store, wrap);
		}
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
    /* -mtt-tensix-optimize-stochrnd-store-fold enables the pass by
       itself so its refusals are reported; the merge folds inside stay
       gated on riscv_tt_opt_store_fold.  */
    return TARGET_XTT_TENSIX
	   && (riscv_tt_opt_store_fold > 0
	       || riscv_tt_opt_stochrnd_store_fold > 0);
  }

  unsigned execute (function *fn) final override
  {
    /* The S2 round-trip proofs cover Blackhole and Wormhole only; S1
       needs no proof but shares the gate.  Refuse on any other
       target.  */
    if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
      {
	rvtt_refuse (RVTT_REF_STORE_FOLD_TARGET_UNPROVEN, dump_file,
		     "store-fold refused (store-fold-target-unproven)\n");
	return 0;
      }
    n_forwarded = 0;
    n_sunk = 0;
    n_sunk_licensed = 0;
    n_stochrnd_folded = 0;
    bool changed = transform (fn);
    if (dump_file)
      fprintf (dump_file,
	       "store-fold: forwarded=%u sunk=%u sunk-licensed=%u "
	       "stochrnd-folded=%u\n",
	       n_forwarded, n_sunk, n_sunk_licensed, n_stochrnd_folded);
    return changed ? TODO_update_ssa_only_virtuals | TODO_verify_all : 0;
  }
};

} /* anonymous namespace */

/* Create the store-fold pass.  It runs early, while the structured CC
   forms are intact and before the invariant pass sees the shortened
   bodies.  */

gimple_opt_pass *
make_pass_rvtt_store_fold (gcc::context *ctxt)
{
  return new pass_rvtt_store_fold (ctxt);
}
