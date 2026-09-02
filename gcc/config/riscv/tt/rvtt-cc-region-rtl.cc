/* Tensix CC-region engine: the post-RA RTL view (laneKQ).
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

/* The RTL-side view of the CC (lane-enable) frame structure -- the
   engine's answer to the rename service's blanket no-CC-event-in-span
   refusal and its named successor note in rtl-rvtt-lreg-rename.cc.
   See the contract comment in rvtt-cc-region.h.

   Soundness ledger for the span verdicts (the consumer obligation the
   verdicts discharge is the item-#7 rename argument: a renamed web's
   target register carries the definition's value on exactly the lanes
   the definition wrote; a reader observes only lanes enabled at its
   own position; a kill-close must overwrite exactly the lanes the
   definition wrote; disabled-lane contents of the two worlds must
   never be observable):

   BALANCED (entry mask M unknown).  Interior invariant: every save
   taken inside the span is a subset of M and the current mask is a
   subset of M.  Induction: sfppushc (0) preserves the mask and pushes
   a subset-of-M save; the audited narrowing writers produce a subset
   of the current mask (the for_each_lane guard -- only currently
   enabled lanes can change their flags; tt/proofs/
   cc-narrowing-writers/RESULT.txt); SFPCOMPC inside an in-span frame
   produces a subset of the in-span save; sfppopc (0) of an in-span
   save restores a recorded subset of M.  So every reader executes
   under a subset of M and observes only definition-written lanes --
   the identical guarantee the blanket constant-mask rule provides.
   When the span end must equal M (a kill-close follows), any depth-0
   narrowing refuses (END_MASK): only frame-interior activity, whose
   popc restores M exactly, is admitted.

   ALL_LANES (entry proven the architectural all-lanes state).  The
   definition wrote EVERY lane of the target, so any interior mask is
   trivially a subset of M and every reader observes definition-written
   lanes regardless of interior mask activity; the only residual
   obligation is the end-state one, discharged word-exactly by the
   all-lanes SFPENCC (the mask after it IS the entry state).  Depth-0
   narrowing writers and SFPCOMPC are therefore admissible here, and
   the all-lanes SFPENCC itself is the one admitted RESTORING event.

   Fail-closed classes: an outside-save sfppopc rewinds to an
   unmodeled state; a non-all-lanes SFPENCC writes an unproven mask;
   nonzero pushc/popc operands are unmodeled shapes (the GIMPLE tree's
   unstructured rule); CC writers outside the audited vocabulary and
   every opaque instruction refuse.

   Scope note: the view answers the LaneFlags/lane-enable question --
   exactly the question the blanket rule asked.  Orthogonal lane
   predication surfaces (the LaneConfig ROW_MASK bits, SFPCONFIG dest
   15) are outside both rules and keep their own consumers' vetoes.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "cfgrtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-cc-region.h"

/* ==================================================================
   Event classification.  One insn, one verdict, refusing default.  */

enum cc_rtl_event
{
  CC_RTL_EV_NONE,	   /* no lane-enable effect */
  CC_RTL_EV_PUSH,	   /* sfppushc (0) */
  CC_RTL_EV_POP,	   /* sfppopc (0) */
  CC_RTL_EV_NARROW,	   /* audited narrowing writer */
  CC_RTL_EV_COMPC,	   /* SFPCOMPC (narrows relative to the save) */
  CC_RTL_EV_ENCC_ALL,	   /* word-exact all-lanes SFPENCC */
  CC_RTL_EV_ENCC_OTHER,	   /* any other SFPENCC */
  CC_RTL_EV_NONZERO_MOD,   /* pushc/popc with a nonzero operand */
  CC_RTL_EV_VOCAB,	   /* CC writer outside the vocabulary */
  CC_RTL_EV_OPAQUE,	   /* call, asm, raw word, unaudited pattern */
};

/* The single const_int operand of a pushc/popc is zero (the plain
   save/restore).  Anything else -- including a non-constant, which the
   templates exclude but the classifier does not trust -- refuses.  */

static bool
plain_pushc_popc_operand_p (rtx_insn *insn)
{
  extract_insn (insn);
  return recog_data.n_operands >= 1
	 && CONST_INT_P (recog_data.operand[0])
	 && INTVAL (recog_data.operand[0]) == 0;
}

/* Classify INSN's lane-enable event.  Mirrors the kind discipline of
   the rename pass's scan_block: zero-length bookkeeping ghosts and
   pinned-LREG protocol markers carry no CC effect; USE/CLOBBER notes
   and scalar insns are transparent; calls and asm are opaque.  */

static cc_rtl_event
classify_cc_rtl_event (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return CC_RTL_EV_NONE;
  if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
    return CC_RTL_EV_OPAQUE;
  if (GET_CODE (PATTERN (insn)) == USE
      || GET_CODE (PATTERN (insn)) == CLOBBER)
    return CC_RTL_EV_NONE;
  uint32_t marker_mask;
  if (rvtt_lreg_marker (insn, &marker_mask))
    return CC_RTL_EV_NONE;
  int code = recog_memoized (insn);
  if (code < 0 || get_attr_type (insn) != TYPE_TENSIX)
    return CC_RTL_EV_NONE;	/* scalar payload */
  if (!get_attr_length (insn))
    return CC_RTL_EV_NONE;	/* bookkeeping ghost */

  xtt_effect_set fx = rvtt_insn_effects (insn);
  if (fx.opaque)
    return CC_RTL_EV_OPAQUE;
  if (!fx.cc_write)
    return CC_RTL_EV_NONE;

  if (code == CODE_FOR_rvtt_sfppushc)
    return plain_pushc_popc_operand_p (insn)
	   ? CC_RTL_EV_PUSH : CC_RTL_EV_NONZERO_MOD;
  if (code == CODE_FOR_rvtt_sfppopc)
    return plain_pushc_popc_operand_p (insn)
	   ? CC_RTL_EV_POP : CC_RTL_EV_NONZERO_MOD;
  if (code == CODE_FOR_rvtt_sfpencc)
    return fx.cc_write_all_lanes ? CC_RTL_EV_ENCC_ALL : CC_RTL_EV_ENCC_OTHER;
  if (code == CODE_FOR_rvtt_sfpcompc)
    return CC_RTL_EV_COMPC;
  if (code == CODE_FOR_rvtt_sfpsetcc_i || code == CODE_FOR_rvtt_sfpsetcc_v
      || code == CODE_FOR_rvtt_sfpgt_cc || code == CODE_FOR_rvtt_sfple_cc)
    return CC_RTL_EV_NARROW;	/* tt/proofs/cc-narrowing-writers/ */
  bool lane_local_ccw;
  if (rvtt_lane_local_effects (insn, &lane_local_ccw) && lane_local_ccw)
    return CC_RTL_EV_NARROW;	/* audited lane-local for_each_lane class */
  return CC_RTL_EV_VOCAB;
}

/* ==================================================================
   The all-lanes entry proof (the RTL re-derivation of the GIMPLE
   tree's edge_entry_all_lanes_p kill-modeling walk).  */

/* Forward-scan verdict for the lane-enable state a block's positions
   see, relative to its entry state.  */

enum cc_rtl_flow
{
  CC_RTL_FLOW_ENTRY,	/* no event yet: the entry state */
  CC_RTL_FLOW_ALL,	/* last event: executed word-exact all-lanes ENCC */
  CC_RTL_FLOW_OTHER,	/* unproven */
};

/* Step the flow state across INSN.  POISONED is set (and stays set)
   once a replay-owner instruction was seen in the block: positions
   after it can be inside a record window and architecturally swallowed
   (stored, not executed -- rvtt-raw-boundary.h), so no later event may
   be trusted as a KILL; preserving-only classification remains sound
   under both readings.  */

static cc_rtl_flow
cc_rtl_flow_step (cc_rtl_flow st, rtx_insn *insn, bool *poisoned)
{
  if (!NONDEBUG_INSN_P (insn))
    return st;
  if (CALL_P (insn))
    return CC_RTL_FLOW_OTHER;
  if (asm_noperands (PATTERN (insn)) >= 0)
    {
      /* Raw `.ttinsn' words: the audited INERT and ALL_LANES classes
	 are ambient-PRESERVING only (never a kill; the word may be
	 swallowed by a replay record).  Every other asm refuses.  */
      uint32_t word;
      if (rvtt_raw_ttinsn_word (insn, &word)
	  && rvtt_raw_cc_word_ambient_preserving_p (word))
	return st;
      return CC_RTL_FLOW_OTHER;
    }
  if (GET_CODE (PATTERN (insn)) == USE
      || GET_CODE (PATTERN (insn)) == CLOBBER)
    return st;
  int code = recog_memoized (insn);
  if (code < 0 || get_attr_type (insn) != TYPE_TENSIX)
    return st;			/* scalar payload */
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    *poisoned = true;
  if (!get_attr_length (insn))
    return st;			/* bookkeeping ghost */
  uint32_t marker_mask;
  if (rvtt_lreg_marker (insn, &marker_mask))
    return st;
  xtt_effect_set fx = rvtt_insn_effects (insn);
  if (fx.opaque)
    return CC_RTL_FLOW_OTHER;
  if (!fx.cc_write)
    return st;
  if (fx.cc_write_all_lanes && !*poisoned)
    return CC_RTL_FLOW_ALL;
  /* Any other CC write -- including a possibly-swallowed all-lanes
     ENCC after a replay owner, and sfppushc, whose stack effect this
     walk does not model -- loses the proof.  */
  return CC_RTL_FLOW_OTHER;
}

/* Tri-state memo for the per-block entry fact.  */

enum cc_rtl_entry_memo
{
  CC_RTL_ENTRY_UNSEEN = 0,
  CC_RTL_ENTRY_IN_PROGRESS,
  CC_RTL_ENTRY_ALL,
  CC_RTL_ENTRY_NOT,
};

static bool cc_rtl_block_entry_all_p (basic_block,
				      hash_map<basic_block, int> &);

/* The lane-enable state at BB's EXIT is provably all-lanes.  */

static bool
cc_rtl_block_exit_all_p (basic_block bb, hash_map<basic_block, int> &memo)
{
  cc_rtl_flow st = CC_RTL_FLOW_ENTRY;
  bool poisoned = false;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    st = cc_rtl_flow_step (st, insn, &poisoned);
  if (st == CC_RTL_FLOW_ALL)
    return true;
  if (st == CC_RTL_FLOW_ENTRY)
    return cc_rtl_block_entry_all_p (bb, memo);
  return false;
}

/* The lane-enable state at BB's ENTRY is provably all-lanes: every
   predecessor path reaches the function entry (the all-lanes ambient
   axiom the GIMPLE side stands on) or an executed word-exact all-lanes
   SFPENCC before any other CC event.  Cycles fail closed.  */

static bool
cc_rtl_block_entry_all_p (basic_block bb, hash_map<basic_block, int> &memo)
{
  int &slot = memo.get_or_insert (bb);
  if (slot == CC_RTL_ENTRY_ALL)
    return true;
  if (slot == CC_RTL_ENTRY_NOT || slot == CC_RTL_ENTRY_IN_PROGRESS)
    return false;
  slot = CC_RTL_ENTRY_IN_PROGRESS;

  bool all = EDGE_COUNT (bb->preds) > 0;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    {
      if (e->flags & (EDGE_ABNORMAL | EDGE_EH))
	{
	  all = false;
	  break;
	}
      if (e->src == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;		/* the all-lanes ambient axiom */
      if (!cc_rtl_block_exit_all_p (e->src, memo))
	{
	  all = false;
	  break;
	}
    }
  /* get_or_insert's reference may be stale after recursive inserts;
     re-fetch through put.  */
  memo.put (bb, all ? CC_RTL_ENTRY_ALL : CC_RTL_ENTRY_NOT);
  return all;
}

/* Whether the lane-enable state at AT (i.e. just before it executes)
   is provably the architectural all-lanes state: no CC event between
   AT's block entry and AT, and every predecessor path reaches the
   function entry or an executed word-exact all-lanes SFPENCC -- or an
   unpoisoned all-lanes SFPENCC earlier in AT's own block.  Fails
   closed on cycles, abnormal edges and replay-poisoned blocks.  */

bool
rvtt_cc_rtl_entry_all_lanes_p (rtx_insn *at)
{
  basic_block bb = BLOCK_FOR_INSN (at);
  if (!bb)
    return false;
  cc_rtl_flow st = CC_RTL_FLOW_ENTRY;
  bool poisoned = false;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (insn == at)
	break;
      st = cc_rtl_flow_step (st, insn, &poisoned);
    }
  if (st == CC_RTL_FLOW_ALL)
    return true;
  if (st != CC_RTL_FLOW_ENTRY)
    return false;
  hash_map<basic_block, int> memo;
  return cc_rtl_block_entry_all_p (bb, memo);
}

/* ==================================================================
   The span classifier.  */

/* Classify the CC behaviour of BB's insn span (AFTER, UNTIL) -- see
   rvtt-cc-region.h for the verdict vocabulary.  Tracks PUSHC/POPC
   depth and mask narrowing across the span's CC events, walking the
   whole block for replay-owner poisoning (comment in body), and fails
   closed on opaque or out-of-vocabulary events.
   REQUIRE_ENTRY_MASK_AT_END demands the stronger verdict that the
   block-entry mask is re-established when the span ends (balanced
   depth, no depth-0 narrowing).  */

rvtt_cc_rtl_span_verdict
rvtt_cc_rtl_classify_span (basic_block bb, rtx_insn *after,
			   rtx_insn *until, bool require_entry_mask_at_end)
{
  gcc_assert (after && BLOCK_FOR_INSN (after) == bb);
  gcc_assert (!until || BLOCK_FOR_INSN (until) == bb);

  int depth = 0;
  bool saw_event = false;
  bool needs_all = false;	/* some event demands the entry proof */
  bool d0_narrowed = false;	/* depth-0 mask differs from M */
  bool inside = false;
  /* A replay-owner instruction anywhere in the block AT OR BEFORE the
     span end can open a record window covering interior positions:
     interior CC events may then be architecturally swallowed (stored,
     not executed), so the widened verdicts -- which reason about
     their EXECUTION -- fail closed.  The blanket NO_EVENT fact does
     not reason about execution and is unaffected.  */
  bool replay_poisoned = false;

  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (insn == until)
	break;
      if (NONDEBUG_INSN_P (insn) && !CALL_P (insn)
	  && asm_noperands (PATTERN (insn)) < 0
	  && recog_memoized (insn) >= 0
	  && get_attr_type (insn) == TYPE_TENSIX
	  && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	replay_poisoned = true;
      if (!inside)
	{
	  inside = (insn == after);
	  continue;
	}
      cc_rtl_event ev = classify_cc_rtl_event (insn);
      switch (ev)
	{
	case CC_RTL_EV_NONE:
	  continue;
	case CC_RTL_EV_PUSH:
	  ++depth;
	  break;
	case CC_RTL_EV_POP:
	  if (depth == 0)
	    return RVTT_CC_RTL_SPAN_REFUSE_OUTSIDE_POP;
	  --depth;
	  break;
	case CC_RTL_EV_NARROW:
	  if (depth == 0)
	    d0_narrowed = true;
	  break;
	case CC_RTL_EV_COMPC:
	  /* Narrows relative to the enclosing SAVE: bounded by M only
	     when the save is in-span.  */
	  if (depth == 0)
	    {
	      needs_all = true;
	      d0_narrowed = true;
	    }
	  break;
	case CC_RTL_EV_ENCC_ALL:
	  /* Widens (to all-lanes): admissible only against an
	     all-lanes entry, where it restores M word-exactly.  */
	  needs_all = true;
	  if (depth == 0)
	    d0_narrowed = false;
	  break;
	case CC_RTL_EV_ENCC_OTHER:
	  return RVTT_CC_RTL_SPAN_REFUSE_ENCC;
	case CC_RTL_EV_NONZERO_MOD:
	  return RVTT_CC_RTL_SPAN_REFUSE_NONZERO_MOD;
	case CC_RTL_EV_VOCAB:
	  return RVTT_CC_RTL_SPAN_REFUSE_VOCAB;
	case CC_RTL_EV_OPAQUE:
	  return RVTT_CC_RTL_SPAN_REFUSE_OPAQUE;
	}
      saw_event = true;
    }
  gcc_assert (inside);

  if (saw_event && replay_poisoned)
    return RVTT_CC_RTL_SPAN_REFUSE_REPLAY;

  if (require_entry_mask_at_end)
    {
      if (depth != 0)
	return RVTT_CC_RTL_SPAN_REFUSE_UNBALANCED;
      if (d0_narrowed)
	return RVTT_CC_RTL_SPAN_REFUSE_END_MASK;
    }
  if (needs_all)
    return rvtt_cc_rtl_entry_all_lanes_p (after)
	   ? RVTT_CC_RTL_SPAN_ALL_LANES
	   : RVTT_CC_RTL_SPAN_REFUSE_ENTRY_UNPROVEN;
  if (saw_event)
    return RVTT_CC_RTL_SPAN_BALANCED;
  return RVTT_CC_RTL_SPAN_NO_EVENT;
}

/* Dump-stable name of span verdict V, for -fopt-info notes and dump
   greps (the refuse-* names are the census vocabulary).  */

const char *
rvtt_cc_rtl_span_verdict_name (rvtt_cc_rtl_span_verdict v)
{
  switch (v)
    {
    case RVTT_CC_RTL_SPAN_NO_EVENT:
      return "no-event";
    case RVTT_CC_RTL_SPAN_BALANCED:
      return "balanced-frames";
    case RVTT_CC_RTL_SPAN_ALL_LANES:
      return "all-lanes-entry";
    case RVTT_CC_RTL_SPAN_REFUSE_OUTSIDE_POP:
      return "refuse-outside-pop";
    case RVTT_CC_RTL_SPAN_REFUSE_UNBALANCED:
      return "refuse-unbalanced";
    case RVTT_CC_RTL_SPAN_REFUSE_END_MASK:
      return "refuse-end-mask";
    case RVTT_CC_RTL_SPAN_REFUSE_ENTRY_UNPROVEN:
      return "refuse-entry-unproven";
    case RVTT_CC_RTL_SPAN_REFUSE_ENCC:
      return "refuse-encc-unproven";
    case RVTT_CC_RTL_SPAN_REFUSE_NONZERO_MOD:
      return "refuse-nonzero-mod";
    case RVTT_CC_RTL_SPAN_REFUSE_VOCAB:
      return "refuse-vocab-external";
    case RVTT_CC_RTL_SPAN_REFUSE_OPAQUE:
      return "refuse-opaque";
    case RVTT_CC_RTL_SPAN_REFUSE_REPLAY:
      return "refuse-replay-window";
    }
  gcc_unreachable ();
}
