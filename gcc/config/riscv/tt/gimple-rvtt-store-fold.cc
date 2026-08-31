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

   A lane-predicated value merge whose only consumer is a Dst store
   costs one delivered word (the predicated SFPMOV the merge expands
   to) that a handwritten kernel avoids by storing the merge source
   directly.  Two structured shapes, with distinct proof obligations:

   S1 -- same-mask forwarding (dataflow proof only):

       r2 = sfpassign_lv (r1, z)     ; merge under the live mask M
       ... no CC or target side effects ...
       sfpstore (A, r2)              ; store under the SAME mask M

   The store writes only M lanes in both forms and the merged value
   equals z exactly on M lanes; the complement lanes are untouched by
   both forms.  The rewrite sfpstore (A, z) + dead-merge deletion is
   value-preserving for every data format on every lane -- it is the
   compiler's own lane-masked IR contract for assign_lv/sfpstore, no
   architectural table involved.

   S2 -- post-region store sink (format round-trip proof required):

       x = sfpload (A)               ; at the enclosing mask E
       sfppushc; <mask refinement>   ; region mask M (subset of E)
       r2 = sfpassign_lv (x, z)      ; merge under M
       sfppopc                       ; state restored to E
       sfpstore (A, r2)              ; store under E

   rewritten to a predicated store AT the merge position:

       x = sfpload (A)
       sfppushc; <mask refinement>
       sfpstore (A, z)               ; store under M
       sfppopc

   On the M lanes both forms store z's conversion.  On the lanes of
   E outside M the original form writes store_convert (load_widen (d))
   back over the Dst datum d while the sunk form leaves d untouched:
   the sink is legal exactly when that round trip is the identity for
   EVERY Dst bit pattern of the (load mod0, store mod0) format pair.
   The exhaustive per-pair sweep (semantics lifted verbatim from the
   pinned simulator) ships in tt/proofs/store-sink-roundtrip/:

     - (INT32, INT32) raw pair: EQUAL over 2^32 -- conversion-free in
       both directions on Blackhole.  LICENSED.
     - every float pair (SRCB/FP16/BF16/FP32): NOT-EQUAL -- the store
       conversion canonicalizes Dst (denormal flush; BF16 254/2^16,
       FP16 2046/2^16, FP32 16777214/2^32 witnesses).  Eliding the
       write-back preserves the original bits, so the sink refuses
       store-sink-format-canonicalizing.  This is a REAL semantic
       distinction, not a delivery artifact: a handwritten kernel that
       stores only under the predicate has architecturally different
       Dst-canonicalization behavior than the all-lanes write-back its
       semantic twin compiles to.

   THE STORE-SINK LICENSE (-mtt-tensix-optimize-store-sink, owner
   ratification 2026-08-26): the float-pair refusal above is the
   certified word floor of the threshold/hardshrink semantic class (one
   predicated-merge word per SIMD row), and on those rows the value the
   extra word buys is one the framework golden does NOT want -- torch
   keeps pass-through lanes' values exactly, while the all-lanes
   write-back flushes their denormals (every float-pair round-trip
   mismatch is in the denormal class; no other divergence exists).  The
   owner therefore licensed the S2 sink for the float pairs: with BOTH
   -mtt-tensix-optimize-store-fold and the license token given, the
   sink fires on a float pair exactly as on the proven INT32 pair, and
   the licensed fire prints its own named dump line.  The admission is
   SHAPE-GENERAL -- the same S2 recognizer, no operation identity or
   magic constant is consulted -- and scope-bounded by the PROOF's
   divergence class, not by intent: the WH INT32_SM pair diverges in an
   integer negative-zero class the ratification does not cover and
   refuses regardless; mixed pairs stay store-fold-sink-format-unproven;
   every shape refusal of the sink applies to licensed fires unchanged.
   License absent, behavior is byte-identical by construction (the same
   named refusal on the same statement).  Licensed cells follow the
   licensed-knob discipline (LICENSED booking, device-golden authority
   at the row's documented tolerance, LICENSED-EXPECTED paired-CRAQ
   disposition).

   The mask-subset argument for S2 admits only region refinements
   between the load and the merge (SETCC/COMPC and the structured
   x-forms refine or complement within the pushed frame, so the mask
   stays a subset of E); SFPENCC can enable lanes beyond E and any
   nested PUSHC/POPC breaks the single-frame restore argument -- both
   refuse by name.  Every statement between the load and the store
   with target side effects (other Dst traffic, RWC/config mutation --
   all VOLATILE builtins) refuses: the address-identity premise needs
   the RWC state at the store to be the load's.

   Related recognition, same anchor: an SFPSTOCHRND whose only consumer
   is a Dst store is the "fold the explicit rounding into the store's
   own conversion" candidate (a handwritten idiom this pass was asked
   to reproduce).  The exhaustive sweep tt/proofs/stochrnd-store-round/
   proves the store's conversion path DIVERGES from the explicit
   instruction on every float row (the store truncates toward zero and
   preserves -0/denormal signs; SFPSTOCHRND rounds to nearest-ties-away
   and normalizes -0/denormal to +0 and NaN to signed infinity;
   2,155,741,184 / 2^32 mismatches on the BF16 row).  The candidate
   therefore always refuses stochrnd-store-rounding-divergent (float
   rows, citing the sweep) or stochrnd-store-no-conversion-path (the
   integer conversions, which no store mode performs at all).  Per the
   tt/proofs README contract the NOT-EQUAL result is a standing named
   refusal as a VALUE-PRESERVING fold: the bit-exact cut is never
   re-mined.

   THE STOCHRND-STORE-FOLD LICENSE (-mtt-tensix-optimize-stochrnd-
   store-fold, lane HZ under the owner overnight mandate 2026-08-27):
   the divergence above is exactly the delta between the semantic
   body's explicit rounding and the HANDWRITTEN idiom the row's hand
   kernel ships (bare converting store, binary-float class: one
   delivered word per SIMD row).  The owner-side accuracy authority
   (laneCX golden re-spec: golden = proven hw cast behavior) accepts
   the hand arm's bits -- the hand cell is the row's passing
   correctness arm -- so folding the semantic body ONTO the hand
   idiom's instruction stream is a licensed value change whose
   accuracy certificate is bit-identity with the hand kernel's own
   store path, quantified by the standing sweep's census (BF16 row
   2,155,741,184 / 2^32: finite round-up 2,130,706,432, -0 1,
   denormal-sign 8,388,607, NaN->Inf 16,646,144; FP16 row
   268,435,456 / 2^32).  With BOTH -mtt-tensix-optimize-store-fold and
   the license token given, an SFPSTOCHRND fires the fold ONLY when
   every one of these holds (each miss refuses by name, token or not):
     - plain typed form (sfpstochrnd_i): the lv-carrier forms merge
       under a mask (stochrnd-store-fold-carrier-unproven) and the
       vector-descale forms are integer-class;
     - deterministic nearest rounding (RND_NEAREST/EVEN encoding 0):
       stochastic rounding is a semantic entropy feature and the
       proof's rows are deterministic
       (stochrnd-store-fold-mode-unlicensed);
     - float conversion mod1 exactly FP32_TO_FP16A or FP32_TO_FP16B
       (else the standing integer refusal);
     - the store's Mod0 targets the MATCHING precision: FP16B->BF16,
       FP16A->FP16, or the runtime-resolved SRCB store (the swept
       rows; the row's ALU config owns the SRCB resolution and the
       licensed booking's device-golden gate is the authority --
       the laneHL SRCB precedent) -- cross-precision static pairs
       refuse (stochrnd-store-fold-format-mismatch);
     - the round's result has the store as its ONLY consumer
       (stochrnd-store-fold-multi-use);
     - the span from the round to the store is same-block and
       CC/side-effect inert, so the store's lane mask is the round's
       (stochrnd-store-fold-span-clobbered);
     - the round's input is a storable LReg source
       (stochrnd-store-fold-source-not-storable);
     - the function contains NO PRNG-stream consumer (RND_STOCH
       rounds, stochastic INT32->FP32 casts): deleting the
       instruction removes one hidden PRNG advance
       (stochrnd-store-fold-entropy-stream).
   The fire deletes the rounding instruction and forwards its input
   into the unchanged store; license absent, behavior is
   byte-identical by construction (the same standing named refusal on
   the same statement).  Licensed cells follow the licensed-knob
   discipline (LICENSED booking, device-golden authority at the row's
   documented tolerance, LICENSED-EXPECTED paired-CRAQ disposition).

   The license token gates the pass BY ITSELF (the knob leg's delta
   must read as the license's own effect): with only the token given,
   the value-preserving S1/S2 merge folds stay OFF -- lane HZ's
   silicon A/B showed the S1 forward alone re-shapes the production
   (hand) binary-float TU's replay window (0,4,1,1 x8 -> 0,6,1,1 x4,
   25766 -> 19498 cycles), so a knob string carrying the parent flag
   would move BOTH arms and conflate two mechanisms.  Because the
   typed convert wrapper spells its rounding through an all-lanes
   merge, the licensed fold recognizes the merge-wrapped shape
   directly (store <- assign_lv <- sfpstochrnd) under exactly the S1
   same-mask dataflow contract plus every licensed belt, and commits
   the forward+fold atomically -- no S1 fire ever escapes the license
   on a non-candidate shape.  With both flags given the S1 forward
   runs first and the fold sees the exposed shape; the result is the
   same words.

   The pass runs beside the ccmask/int-abs folds before the invariant
   pass, while the structured CC forms are intact.  Every miss refuses
   by name with the program bytes unchanged.  */

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

namespace {

static unsigned n_forwarded;
static unsigned n_sunk;
static unsigned n_sunk_licensed;
static unsigned n_stochrnd_folded;

/* The S2 sink license key (owner ratification 2026-08-26): the
   value-changing float-pair sink fires ONLY when the user passed the
   dedicated default-off license token -mtt-tensix-optimize-store-sink
   (in addition to the pass's own -mtt-tensix-optimize-store-fold).
   Token absent = the standing named refusal on every such site and
   byte-identical codegen.  */

static bool
rvtt_store_sink_licensed_p (void)
{
  return riscv_tt_opt_store_sink > 0;
}

/* The SFPSTOCHRND-into-store fold license key (lane HZ, owner overnight
   mandate 2026-08-27): the value-changing rounding-elision fold fires
   ONLY when the user passed the dedicated default-off license token
   -mtt-tensix-optimize-stochrnd-store-fold (in addition to the pass's
   own -mtt-tensix-optimize-store-fold).  Token absent = the standing
   named refusal on every such site and byte-identical codegen.  */

static bool
rvtt_stochrnd_store_fold_licensed_p (void)
{
  return riscv_tt_opt_stochrnd_store_fold > 0;
}

/* SFPLOAD/SFPSTORE Mod0 data-format selectors (capability data:
   BlackholeA0 SFPSTORE.md/SFPLOAD.md supporting definitions; pinned
   craq-sim tensix.cpp sfpstore_values/TENSIX_EXECUTE_SFPLOAD arms).  */
constexpr long SFPMEM_MOD0_FMT_SRCB  = 0;
constexpr long SFPMEM_MOD0_FMT_FP16  = 1;
constexpr long SFPMEM_MOD0_FMT_BF16  = 2;
constexpr long SFPMEM_MOD0_FMT_FP32  = 3;
constexpr long SFPMEM_MOD0_FMT_INT32 = 4;
constexpr long SFPMEM_MOD0_FMT_INT32_SM = 12;

/* SFPSTORE data-source LReg ceiling: the architecture stores L0-L11
   (SFPSTORE.md functional model; pinned simulator hard-verifies
   lreg_ind < 12).  A merge source pinned to a higher LReg (the
   programmable-constant residency file) is not a storable operand.  */
constexpr unsigned SFPSTORE_MAX_SRC_LREG = 12;

/* Format-pair verdict tables, GENERATED from the exhaustive proof
   sweeps: tt/rvtt-storefold-verdicts.def is emitted by
   genrvtt-storefold from tt/proofs/store-sink-roundtrip/RESULT.txt and
   tt/proofs/stochrnd-store-round/RESULT.txt, and the build
   byte-compares the checked-in .def against a fresh generation
   (tt/t-riscv-tt rvtt-storefold-verdicts.chk) -- these rows cannot
   drift from their RESULT files without failing the build.  The fire
   class per row is the reviewed pass policy (FIRE = EQUAL pair;
   LICENSED = denormal-flush class under -mtt-tensix-optimize-
   store-sink; REFUSE = divergence outside the ratified class); a pair
   without a row has no proof on record.  */

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
};

struct stochrnd_store_row
{
  long conv, sfmt;
};

#define RVTT_STOREFOLD_PROOF(path, sha256)
#define RVTT_STOREFOLD_SINK_PAIR(lfmt, sfmt, verdict, divergence, license, \
				 rsha, isha)	\
  { lfmt, sfmt, STOREFOLD_##license },
#define RVTT_STOCHRND_STORE_PAIR(conv, sfmt, fsha, dsha)
static constexpr storefold_sink_row storefold_sink_rows[] = {
#include "rvtt-storefold-verdicts.def"
};
#undef RVTT_STOREFOLD_SINK_PAIR
#undef RVTT_STOCHRND_STORE_PAIR

#define RVTT_STOREFOLD_SINK_PAIR(lfmt, sfmt, verdict, divergence, license, \
				 rsha, isha)
#define RVTT_STOCHRND_STORE_PAIR(conv, sfmt, fsha, dsha) { conv, sfmt },
static constexpr stochrnd_store_row stochrnd_store_rows[] = {
#include "rvtt-storefold-verdicts.def"
};
#undef RVTT_STOCHRND_STORE_PAIR
#undef RVTT_STOREFOLD_SINK_PAIR
#undef RVTT_STOREFOLD_PROOF

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
  if (dump_file)
    {
      fprintf (dump_file, "store-fold refused (%s): ", reason);
      print_gimple_stmt (dump_file, stmt, 0);
    }
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
    case rvtt_insn_data::sfpxvif:
    case rvtt_insn_data::sfpxbool:
    case rvtt_insn_data::sfpxcondb:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpcompc:
      return true;
    default:
      return false;
    }
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
  if (gcall *read = is_rvtt_call (def, rvtt_insn_data::sfpreadlreg))
    {
      long idx = int_arg (read, 0);
      if (idx < 0 || idx >= (long) SFPSTORE_MAX_SRC_LREG)
	return false;
    }
  return true;
}

/* Walk result for the assign->store span.  */

enum span_kind { SPAN_BAD, SPAN_SAME_MASK, SPAN_REGION_CLOSED };

/* Scan from the statement after ASSIGN to STORE, classifying the CC
   delta.  Handles the same-block layout and the v_endif diamond (the
   counted CC-frame destructor): body block -> join with exactly two
   predecessors, the other being a popc-only block, and the store in
   the join's other successor.  POPC_OUT receives the closing popc when
   the span crosses one.  */

static span_kind
classify_assign_to_store (gcall *assign, gcall *store, gcall **popc_out)
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
	return *popc_out ? SPAN_REGION_CLOSED : SPAN_SAME_MASK;
      if (inert_stmt_p (stmt))
	continue;
      if (gcall *popc = is_rvtt_call (stmt, rvtt_insn_data::sfppopc))
	{
	  if (*popc_out || int_arg (popc, 0) != 0)
	    return SPAN_BAD;
	  *popc_out = popc;
	  continue;
	}
      if (pure_vector_stmt_p (stmt))
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
      if (gcall *pc = is_rvtt_call (pstmt, rvtt_insn_data::sfppopc))
	{
	  if (popc || int_arg (pc, 0) != 0)
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
	return SPAN_REGION_CLOSED;
      if (inert_stmt_p (sstmt))
	continue;
      return SPAN_BAD;
    }
  return SPAN_BAD;
}

/* Scan backward context for S2: from LOAD (exclusive) to ASSIGN
   (exclusive), same block: a side-effect-free prefix, then exactly one
   sfppushc (0), then mask-refining and pure statements only.  */

static bool
check_load_to_assign (gcall *load, gcall *assign)
{
  if (gimple_bb (load) != gimple_bb (assign))
    return refuse ("store-fold-sink-region-shape", assign);
  bool in_region = false;
  gimple_stmt_iterator gsi = gsi_for_stmt (load);
  gsi_next (&gsi);
  for (; !gsi_end_p (gsi); gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == assign)
	return in_region ? true
			 : refuse ("store-fold-sink-region-shape", assign);
      if (inert_stmt_p (stmt))
	continue;
      if (gcall *pushc = is_rvtt_call (stmt, rvtt_insn_data::sfppushc))
	{
	  if (in_region || int_arg (pushc, 0) != 0)
	    return refuse ("store-fold-sink-region-shape", stmt);
	  in_region = true;
	  continue;
	}
      if (in_region && mask_refining_stmt_p (stmt))
	continue;
      if (pure_vector_stmt_p (stmt))
	continue;
      /* SFPENCC, nested frames, other Dst/RWC/config traffic, raw asm:
	 the mask-subset or address-identity premise is gone.  */
      return refuse ("store-fold-sink-span-clobbered", stmt);
    }
  return refuse ("store-fold-sink-region-shape", assign);
}

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
fold_merge_store (gcall *assign, gcall *store)
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
  span_kind kind = classify_assign_to_store (assign, store, &popc);

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
  gcall *load = is_rvtt_call (SSA_NAME_DEF_STMT (prev),
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
  if (noinc < 0 || int_arg (load, 5) != noinc)
    return refuse ("store-fold-sink-addrmode-unproven", load);

  long lfmt = int_arg (load, 4);
  long sfmt = int_arg (store, 5);
  bool licensed = false;
  /* Format-pair admission by the GENERATED verdict table (one row per
     exhaustively swept Dst round trip plus the runtime-resolved SRCB
     row; tt/rvtt-storefold-verdicts.def, byte-checked against
     tt/proofs/store-sink-roundtrip/RESULT.txt every build).  A pair
     without a row has no round-trip proof on record.  */
  const storefold_sink_row *pair = nullptr;
  for (const storefold_sink_row &r : storefold_sink_rows)
    if (r.lfmt == lfmt && r.sfmt == sfmt)
      {
	pair = &r;
	break;
      }
  if (flag_checking)
    {
      /* One-pin assert-equal phase (FABLE_GOES_BURR item #3
	 compatibility contract): recompute the pre-table hand
	 ladder's verdict and assert the generated table reproduces it
	 exactly.  DELETE with the next pin.  */
      bool ladder_known = true;
      storefold_license ladder = STOREFOLD_REFUSE;
      if (lfmt == SFPMEM_MOD0_FMT_INT32 && sfmt == SFPMEM_MOD0_FMT_INT32)
	ladder = STOREFOLD_FIRE;
      else if ((lfmt == SFPMEM_MOD0_FMT_SRCB || lfmt == SFPMEM_MOD0_FMT_FP16
		|| lfmt == SFPMEM_MOD0_FMT_BF16
		|| lfmt == SFPMEM_MOD0_FMT_FP32)
	       && lfmt == sfmt)
	ladder = STOREFOLD_LICENSED;
      else if (lfmt == SFPMEM_MOD0_FMT_INT32_SM && lfmt == sfmt)
	ladder = STOREFOLD_REFUSE;
      else
	ladder_known = false;
      gcc_assert (ladder_known == (pair != nullptr)
		  && (!pair || ladder == pair->license));
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
	 the NOT-EQUAL float rows, SRCB resolves at runtime to one of
	 the swept float paths).  The predicated-store form preserves
	 those bits -- admitted ONLY under the
	 -mtt-tensix-optimize-store-sink license token (owner
	 ratification 2026-08-26: the sunk form is the golden-closer
	 semantics -- torch keeps pass-through lanes exactly, the
	 write-back flushes them).  Token absent = the standing named
	 refusal, byte-identical.  */
      if (!rvtt_store_sink_licensed_p ())
	return refuse ("store-fold-sink-format-canonicalizing", store);
      licensed = true;
      break;

    case STOREFOLD_REFUSE:
      /* The WH INT32_SM pair normalizes -0 ((12,12) row): an integer
	 sign-magnitude divergence class the store-sink license does
	 NOT cover (it is scoped to the float pairs' denormal-flush
	 class).  Refuses with or without the license token.  */
      return refuse ("store-fold-sink-format-canonicalizing", store);
    }

  if (!check_load_to_assign (load, assign))
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
  return true;
}

/* SFPSTOCHRND-into-store candidate handling.  The store's value
   operand is the rounding instruction's result.  Per
   tt/proofs/stochrnd-store-round/ no store conversion path reproduces
   any SFPSTOCHRND conversion, so as a value-preserving fold every
   instance refuses by name; under the
   -mtt-tensix-optimize-stochrnd-store-fold license token (see the
   file comment) the deterministic-nearest float rows whose store
   targets the matching precision FIRE as a licensed value change --
   the folded stream is the handwritten idiom's bare converting
   store.  */

/* Per-variant argument positions: (mod1, rnd) gimple arg indices, or
   {-1,-1} when the variant is not an SFPSTOCHRND.  */

static bool
stochrnd_args (const rvtt_insn_data *insnd, int *mod1_pos, int *rnd_pos)
{
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpstochrnd_i:
      *mod1_pos = 5; *rnd_pos = 6; return true;
    case rvtt_insn_data::sfpstochrnd_i_lv:
      *mod1_pos = 6; *rnd_pos = 7; return true;
    case rvtt_insn_data::sfpstochrnd_v:
      *mod1_pos = 2; *rnd_pos = 3; return true;
    case rvtt_insn_data::sfpstochrnd_v_lv:
      *mod1_pos = 3; *rnd_pos = 4; return true;
    default:
      return false;
    }
}

/* True when the function contains any statement whose VALUE depends on
   the PRNG stream: a stochastic-mode SFPSTOCHRND or a stochastic
   INT32->FP32 SFPCAST.  Every SFPSTOCHRND advances the PRNG even in
   the deterministic modes, so deleting one shifts the stream every
   later consumer samples; the licensed fold fails closed when any
   consumer exists (non-constant mode operands count as consumers).  */

static bool
fn_has_prng_consumer_p (function *fun)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	const rvtt_insn_data *insnd = rvtt_get_insn_data (gsi_stmt (gsi));
	if (!insnd)
	  continue;
	gcall *call = as_a <gcall *> (gsi_stmt (gsi));
	int mod1_pos, rnd_pos;
	if (stochrnd_args (insnd, &mod1_pos, &rnd_pos))
	  {
	    long rnd_mode = int_arg (call, rnd_pos);
	    if (rnd_mode != (long) SFPSTOCHRND_RND_EVEN
		&& rnd_mode != 2 /* BH round-to-zero: deterministic */)
	      return true;
	  }
	else if (insnd->id == rvtt_insn_data::sfpcast
		 || insnd->id == rvtt_insn_data::sfpcast_lv)
	  {
	    long mod1 = int_arg (call, insnd->id == rvtt_insn_data::sfpcast
					? 1 : 2);
	    if (mod1 < 0 || mod1 == (long) SFPCAST_MOD1_INT32_TO_FP32_RNS)
	      return true;
	  }
      }
  return false;
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
   same-mask dataflow contract.  Returns true when the program changed
   (the licensed fire); every miss refuses by name with the program
   bytes unchanged.  */

static bool
fold_stochrnd_store (gcall *rnd, gcall *store, bool fn_prng_consumer,
		     gcall *wrap)
{
  const rvtt_insn_data *insnd = rvtt_get_insn_data (rnd);
  int mod1_pos, rnd_pos;
  if (!stochrnd_args (insnd, &mod1_pos, &rnd_pos))
    return false;
  long mod1 = int_arg (rnd, mod1_pos);
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

  /* Licensed path.  Each belt refuses by its own name.  */
  if (insnd->id != rvtt_insn_data::sfpstochrnd_i)
    /* lv carriers merge under a mask; deleting the instruction would
       lose the carrier semantics, not just the rounding.  */
    return refuse ("stochrnd-store-fold-carrier-unproven", store);

  /* The float class carries no extra mod1 bits (IMM8 is INT32-only);
     anything else is outside the proof rows.  */
  if (mod1 != (long) SFPSTOCHRND_MOD1_FP32_TO_FP16A
      && mod1 != (long) SFPSTOCHRND_MOD1_FP32_TO_FP16B)
    return refuse ("stochrnd-store-fold-format-mismatch", store);

  long rnd_mode = int_arg (rnd, rnd_pos);
  if (rnd_mode != (long) SFPSTOCHRND_RND_EVEN)
    /* Stochastic (and the BH round-to-zero mode, and any non-constant
       mode) is not the proof's deterministic-nearest class.  */
    return refuse ("stochrnd-store-fold-mode-unlicensed", store);

  long mod0 = int_arg (store, 5);
  /* Matching-precision pairing by the GENERATED table (the swept
     stochrnd proof rows plus the runtime-resolved SRCB store per
     conversion; tt/rvtt-storefold-verdicts.def, byte-checked against
     tt/proofs/stochrnd-store-round/RESULT.txt every build).
     Cross-precision static pairs have no row.  */
  bool pair_ok = false;
  for (const stochrnd_store_row &r : stochrnd_store_rows)
    if (r.conv == mod1 && r.sfmt == mod0)
      {
	pair_ok = true;
	break;
      }
  if (flag_checking)
    /* One-pin assert-equal phase (FABLE_GOES_BURR item #3
       compatibility contract): the generated pairing table must
       reproduce the pre-table hand expression exactly.  DELETE with
       the next pin.  */
    gcc_assert (pair_ok
		== ((mod0 == SFPMEM_MOD0_FMT_SRCB)
		    || (mod1 == (long) SFPSTOCHRND_MOD1_FP32_TO_FP16B
			&& mod0 == SFPMEM_MOD0_FMT_BF16)
		    || (mod1 == (long) SFPSTOCHRND_MOD1_FP32_TO_FP16A
			&& mod0 == SFPMEM_MOD0_FMT_FP16)));
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
      if (classify_assign_to_store (wrap, store, &popc) != SPAN_SAME_MASK)
	return refuse ("stochrnd-store-fold-span-clobbered", store);
    }

  tree src = gimple_call_arg (rnd, 1);
  if (TREE_CODE (src) != SSA_NAME)
    return refuse ("stochrnd-store-fold-source-form", rnd);
  if (!storable_source_p (src))
    return refuse ("stochrnd-store-fold-source-not-storable", rnd);

  if (!stochrnd_span_inert_p (rnd, wrap ? wrap : store))
    return refuse ("stochrnd-store-fold-span-clobbered", store);

  if (fn_prng_consumer)
    return refuse ("stochrnd-store-fold-entropy-stream", store);

  /* Commit: the store keeps its own conversion path (the handwritten
     idiom); the explicit rounding word -- and the wrapper merge, when
     present -- disappears.  */
  gimple_call_set_arg (store, 1, src);
  update_stmt (store);
  if (dump_file)
    {
      fprintf (dump_file,
	       "store-fold: licensed stochrnd fold (-mtt-tensix-optimize-"
	       "stochrnd-store-fold: the store's own conversion delivers "
	       "the hand idiom's truncating bits in place of the explicit "
	       "nearest-ties-away round; divergence census "
	       "tt/proofs/stochrnd-store-round/%s) into ",
	       wrap ? "; wrapper merge folded under the S1 same-mask "
		      "contract" : "");
      print_gimple_stmt (dump_file, store, 0);
    }
  if (wrap)
    remove_stmt (wrap);
  remove_stmt (rnd);
  n_stochrnd_folded++;
  return true;
}

static bool
transform (function *fun)
{
  bool changed = false;
  basic_block bb;

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
	    if (gcall *store = is_rvtt_call (gsi_stmt (gsi),
					     rvtt_insn_data::sfpstore))
	      {
		tree v = gimple_call_arg (store, 1);
		if (TREE_CODE (v) == SSA_NAME)
		  if (gcall *assign
		      = is_rvtt_call (SSA_NAME_DEF_STMT (v),
				      rvtt_insn_data::sfpassign_lv))
		    changed |= fold_merge_store (assign, store);
	      }
	    gsi = next;
	  }
      }

  /* ...then the stochrnd-into-store candidates on the settled stream
     (a forwarded merge exposes the rounding instruction as the store's
     direct operand).  Unlicensed this only prints the standing named
     refusals; licensed it can transform, so it runs unconditionally
     (the PRNG-consumer census is per-function, computed once).  */
  bool fn_prng_consumer
    = rvtt_stochrnd_store_fold_licensed_p () && fn_has_prng_consumer_p (fun);
  FOR_EACH_BB_FN (bb, fun)
    {
      gimple_stmt_iterator gsi = gsi_start_bb (bb);
      while (!gsi_end_p (gsi))
	{
	  gimple_stmt_iterator next = gsi;
	  gsi_next (&next);
	  if (gcall *store = is_rvtt_call (gsi_stmt (gsi),
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
			= is_rvtt_call (def, rvtt_insn_data::sfpassign_lv))
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
		      && (dinsnd->id == rvtt_insn_data::sfpstochrnd_i
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_i_lv
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_v
			  || dinsnd->id == rvtt_insn_data::sfpstochrnd_v_lv))
		    changed |= fold_stochrnd_store (as_a <gcall *> (def),
						    store, fn_prng_consumer,
						    wrap);
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
  OPTGROUP_NONE,
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
    /* The stochrnd license token opens the pass by itself (its knob
       leg must not carry the S1/S2 flag: the delta has to read as the
       license's own effect); the merge folds inside stay gated on
       riscv_tt_opt_store_fold.  */
    return TARGET_XTT_TENSIX
	   && (riscv_tt_opt_store_fold > 0
	       || riscv_tt_opt_stochrnd_store_fold > 0);
  }

  unsigned execute (function *fn) final override
  {
    /* The S2 round-trip proof ran against the shared TT_VERSION<=1
       simulator arm both pinned oracles compile (BH and WH); the S1
       forwarding is the IR contract itself but shares the pass gate
       for a single audited fire surface.  Fail closed elsewhere.  */
    if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
      {
	if (dump_file)
	  fprintf (dump_file,
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

} // anonymous namespace

gimple_opt_pass *
make_pass_rvtt_store_fold (gcc::context *ctxt)
{
  return new pass_rvtt_store_fold (ctxt);
}
