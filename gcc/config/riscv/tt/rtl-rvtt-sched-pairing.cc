/* Tensix scheduling: cross-row pairing
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* The cross-row pairing unit of the Tensix scheduler
   (-mtt-tensix-optimize-crossrow-pairing and its -stall-words,
   -seed, crossrow-shared-reload and mve-expand arms): pairs two
   admitted iterations of a capturable single-row Dst loop into one
   doubled row and interleaves the halves under the counted
   replay-capture delivery shape.  Split from rtl-rvtt-schedule.cc;
   the algorithm essay lives there.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "cfgrtl.h"
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "insn-constants.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rtl-rvtt-sched-int.h"

/* ---- Cross-row pairing (-mtt-tensix-optimize-crossrow-pairing) ----

   The FI-3c mechanism: pair two consecutive iterations of a capturable
   single-row Dst loop into ONE doubled row whose iterations interleave,
   keeping the counted-loop replay capture shape so delivery stays
   record-plus-launch (halved launches) while the interleave fills the
   modeled dependency stalls the single row cannot (the roundingops
   mad->setcc distance-1 adjacency and the seam; capture rotation names
   and cannot fill them -- every single-row filler is CC-bearing).

   Admitted shape (everything else refuses by name, fail-closed, and the
   single row is kept byte-identically):

     row:   one constant-address no-increment Dst load FIRST, pure/CC
	    words, one matching constant-address no-increment Dst store
	    LAST, all words audited (typed effects, audited 0/1-slot
	    latency, one issue slot, SFPU-only DF references, fixed
	    replay encodings);
     CC:    flat atoms only -- each opens at a CC writer and closes at
	    the word-exact all-lanes SFPENCC restore (cc_write_all_lanes);
	    no Dst or RWC effect inside an atom; ambient state between
	    atoms is all-lanes, PROVEN at loop entry by a backward walk
	    (nearest reaching CC writer is the all-lanes restore, or the
	    walk reaches the function entry, whose all-lanes ambient is
	    the shipped structured-CC lowering contract: gimple-rvtt-cc.cc
	    removes the outermost PUSHC and closes every outermost region
	    with the exact all-lanes ENCC);
     step:  one trailing typed TTINCRWC (0, d, 0, 0) row separator, the
	    only RWC effect in the loop;
     ctrl:  the canonical scalar countdown (reg += -1; if (reg != 0)
	    backedge) whose register is referenced nowhere else, counting
	    down from a proven EVEN positive constant.

   Transform (one transaction; every later refusal restores exactly):

     1. row B = textual copy of row A, emitted after A;
     2. B's load/store Dst addresses rebase A -> A+d (the typed static
	offset the removed interior row step would have supplied), the
	shared trailing separator doubles d -> 2d, and the countdown
	halves -1 -> -2: the pair touches exactly the Dst rows and the
	RWC walk the two original iterations touched, in the same
	counter frame (disjointness: both footprints are constant-offset
	in one frame, A aligned 0 mod 4 and B at A+d with d = 2 address
	units, so the two rows' unit footprints cannot overlap);
     3. allocator-packed row-B webs rename to dead LREGs through the
	established transactional cyclic renamer, restricted to webs
	rooted in the ambient all-lanes state (rename-cc-domain: a
	fresh predicated definition renamed to a dead register would
	expose stale disabled-lane bits -- the adjudicated defect that
	rejected the round-cc-modulo prototype);
     4. pure spans of the two rows list-schedule together interval by
	interval; CC atoms stay indivisible, in original interior order,
	atom A before atom B (each atom computes its own lane state from
	its own row's data -- contiguity is the CC-state-equality
	placement proof); stores stay in architectural order;
     5. acceptance: strict modeled steady-state II decrease over the
	doubled baseline (the two logical iterations cost-compared in
	the same delivery mode) AND no pad-site increase (the nop
	inserter's probe) AND the doubled row still fits the replay
	buffer (2n <= XTT_DELIVERY_CAPTURE_SLOTS with the separator
	explicit), so the counted-loop capture downstream keeps firing
	and the transform never trades the replay delivery for issue
	slots (the adjudicated profitability defect of the prototype).

   Purely structural: no operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.
   Blackhole only (the audited latency/adjacency model family).  */

struct crp_loop
{
  basic_block bb;
  std::vector<ls_node> nodes;			/* row words in order */
  std::vector<std::pair<unsigned, unsigned> > atoms; /* inclusive */
  unsigned load = ~0u;
  unsigned store = ~0u;
  rtx_insn *separator = nullptr;
  rtx_insn *counter = nullptr;
  rtx_insn *jump = nullptr;
  unsigned counter_regno = ~0u;
  HOST_WIDE_INT trips = 0;
  HOST_WIDE_INT dst_addr = 0;
  HOST_WIDE_INT dst_step = 0;
};

/* Fire and dump the named cross-row pairing refusal WHY in BB, naming
   INSN when given.  Returns false so admission checks can refuse in a
   single return statement.  */

static bool
crp_refuse (basic_block bb, const char *why, rtx_insn *insn = nullptr)
{
  if (dump_file)
    {
      rvtt_refuse_by_name (why, dump_file,
			   "Crossrow pairing refused: %s", why);
      if (insn)
	fprintf (dump_file, " (uid=%d)", INSN_UID (insn));
      fprintf (dump_file, " in bb %d\n", bb->index);
    }
  return false;
}

/* Mirror of rtl-rvtt-replay.cc fixed_replay_rtx_p (the capture pass's
   own fixed-encoding admission): hard LREGs, constants and scratch are
   fixed; a GPR or MEM means the word cannot be recorded.  Kept in step
   so the capture-shape precondition proven here is the one the
   downstream counted-loop capture re-checks.  */

static bool
crp_fixed_word_p (const_rtx x)
{
  switch (GET_CODE (x))
    {
    case CONST_INT:
    case SCRATCH:
      return true;
    case REG:
      return SFPU_REG_P (REGNO (x));
    case SET:
      return crp_fixed_word_p (SET_DEST (x)) && crp_fixed_word_p (SET_SRC (x));
    case CLOBBER:
    case USE:
      return crp_fixed_word_p (XEXP (x, 0));
    case PARALLEL:
    case UNSPEC:
    case UNSPEC_VOLATILE:
      for (int ix = XVECLEN (x, 0); ix--;)
	if (!crp_fixed_word_p (XVECEXP (x, 0, ix)))
	  return false;
      return true;
    default:
      return false;
    }
}

/* Row-word admission into the pairing's scheduling vocabulary.  The
   node model is the list scheduler's own (audited 0/1-slot latencies,
   LREG dependence edges); CC words enter with their real LREG uses for
   RAW ordering -- their CC visibility is protected by the indivisible
   atom and its original interior order, and the architectural lag is
   exactly one (rvtt_macro::cc_visibility_lag).  */

static bool
crp_node (basic_block bb, rtx_insn *insn, ls_node *node)
{
  if (!issued_tensix_p (insn) || JUMP_P (insn)
      || get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    return crp_refuse (bb, "crossrow-pairing-foreign-word", insn);
  if (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
      || !crp_fixed_word_p (PATTERN (insn)))
    return crp_refuse (bb, "crossrow-pairing-noncapturable-word", insn);
  xtt_effect_set e = rvtt_insn_effects (insn);
  /* Next-slot acceptance-stall words (the SFPSWAP family) join the
     vocabulary under the sub-flag: the word is fully audited (its
     biased xtt_result_latency is on record; an unaudited one still
     refuses below) and its architectural stall is PRICED, not proven
     away -- two issue slots in the steady-state II model (the
     rvtt-cost.md consumer rule: one extra slot per occurrence),
     charged identically in the doubled sequential baseline and every
     candidate.  audited_latency () itself keeps returning -1 for
     these words: the interlock scheduler and capture
     rotation never gain them as fill participants.  */
  bool stall_word = e.next_slot_stall
    && riscv_tt_opt_crossrow_pairing_stall_words > 0;
  if (e.opaque || e.config_dests_written || e.config_dests_read
      || e.addr_mod_slot_write || (e.next_slot_stall && !stall_word)
      || get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    return crp_refuse (bb, "crossrow-pairing-effect-unproven", insn);
  /* Every DF reference must be an SFPU register: a scalar (GPR)
     dependence is outside this vocabulary and would be reordered
     untracked.  */
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    if (DF_REF_REGNO (ref) >= FIRST_PSEUDO_REGISTER
	|| !SFPU_REG_P (DF_REF_REGNO (ref)))
      return crp_refuse (bb, "crossrow-pairing-scalar-dependence", insn);
  /* A stall word bypasses audited_latency ()'s deliberate -1 (its
     refusal there is the fill-participation contract, not a missing
     audit): the biased attribute value is the audited result latency,
     and an UNAUDITED one (-1) still refuses through the range check
     below.  */
  node->lat = stall_word ? e.result_latency : audited_latency (insn);
  /* Pure flag writers have no LREG result whose latency enters the
     list model; their ordering is the atom's.  */
  if ((node->lat < 0 || node->lat > 1) && e.cc_write && !e.lreg_write
      && rvtt_macro::cc_visibility_lag () == 1)
    node->lat = 0;
  bool regs_ok = collect_sfpu_regs (insn, &node->regs);
  if (!regs_ok && (e.cc_write || e.dst_mem_write) && !e.lreg_write)
    {
      /* Defless CC/store words fail collect_sfpu_regs' ordinary
	 schedulable-node contract (no LREG destination); the explicit
	 SFPU-only DF proof above already excluded scalar forms, so
	 retain their real LREG uses for RAW ordering.  */
      sfpu_reg_refs (insn, &node->regs);
      regs_ok = true;
    }
  if (node->lat < 0 || node->lat > 1 || !regs_ok)
    return crp_refuse (bb, "crossrow-pairing-latency-or-lreg-unproven", insn);
  node->raw_defs = node->regs.defs;
  node->insn = insn;
  node->words = get_attr_length (insn) / 4;
  if (node->words != 1)
    return crp_refuse (bb, "crossrow-pairing-word-width-unproven", insn);
  /* The acceptance stall is an ISSUE fact, not a stream word: the
     node occupies two slots in the II/greedy time accounting while
     the capture-budget bound (a per-NODE count of recorded words)
     stays one.  */
  if (stall_word)
    node->words = 2;
  node->entry_pin = 0;
  node->pin_to_baseline = false;
  return true;
}

/* Prove the ambient lane state at the loop's entry is all-lanes: walk
   backward from the loop's dedicated preheader through single-
   predecessor blocks; the nearest reaching CC writer must be the
   word-exact all-lanes restore, or the walk reaches the function entry
   (the shipped structured-CC lowering contract's ambient).  Anything
   opaque to the CC vocabulary refuses.  */

static bool
crp_entry_all_lanes_p (basic_block bb, basic_block preheader)
{
  basic_block cur = preheader;
  while (true)
    {
      for (rtx_insn *insn = BB_END (cur);
	   insn && insn != PREV_INSN (BB_HEAD (cur));
	   insn = PREV_INSN (insn))
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	  if (GET_CODE (insn) != INSN
	      || GET_CODE (PATTERN (insn)) == USE
	      || GET_CODE (PATTERN (insn)) == CLOBBER)
	    continue;
	  if (recog_memoized (insn) < 0
	      || get_attr_type (insn) != TYPE_TENSIX)
	    continue;	/* scalar work carries no lane state */
	  if (!get_attr_length (insn))
	    continue;	/* bookkeeping ghost */
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	  if (e.cc_write_all_lanes)
	    return true;
	  if (e.cc_write)
	    return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven", insn);
	}
      if (!single_pred_p (cur))
	return crp_refuse (bb, "crossrow-pairing-entry-cc-unproven");
      cur = single_pred (cur);
      if (cur == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	return true;
    }
}

/* Structural admission of the whole loop.  Silent (returns false with
   no dump line) only when BB is not a self-loop at all.  */

static bool
crp_admit_loop (basic_block bb, crp_loop *lp)
{
  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;
  if (!TARGET_XTT_TENSIX_BH)
    return crp_refuse (bb, "crossrow-pairing-bh-only");
  if (EDGE_COUNT (bb->succs) != 2 || EDGE_COUNT (bb->preds) != 2)
    return crp_refuse (bb, "crossrow-pairing-row-shape");

  lp->bb = bb;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    return crp_refuse (bb, "crossrow-pairing-row-shape", insn);
	  lp->jump = insn;
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0
	  || contains_mem_rtx_p (PATTERN (insn)))
	return crp_refuse (bb, "crossrow-pairing-foreign-word", insn);
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	return crp_refuse (bb, "crossrow-pairing-bookkeeping-word", insn);
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    return crp_refuse (bb, "crossrow-pairing-ghost-word", insn);
	  if (lp->counter)
	    return crp_refuse (bb, "crossrow-pairing-counter-position", insn);
	  xtt_effect_set eff = rvtt_insn_effects (insn);
	  if (!eff.opaque && eff.rwc.kind == xtt_rwc_effect_t::INC
	      && !eff.lreg_read && !eff.lreg_write
	      && !eff.cc_read && !eff.cc_write
	      && !eff.dst_mem_read && !eff.dst_mem_write)
	    {
	      /* Typed row separator: exactly one, trailing.  */
	      if (lp->separator || lp->nodes.empty ()
		  || eff.rwc.dst_delta <= 0 || eff.rwc.cr_delta != 0)
		return crp_refuse (bb, "crossrow-pairing-row-step-shape",
				   insn);
	      lp->separator = insn;
	      lp->dst_step = eff.rwc.dst_delta;
	      continue;
	    }
	  if (lp->separator)
	    return crp_refuse (bb, "crossrow-pairing-word-after-row-step",
			       insn);
	  ls_node node;
	  if (!crp_node (bb, insn, &node))
	    return false;
	  node.orig = lp->nodes.size ();
	  lp->nodes.push_back (node);
	  continue;
	}
      /* Scalar RISC insn: admit exactly the canonical countdown, after
	 the separator.  */
      if (lp->counter || !lp->separator)
	return crp_refuse (bb, "crossrow-pairing-counter-shape", insn);
      rtx set = single_set (insn);
      if (!set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || GET_CODE (SET_SRC (set)) != PLUS
	  || !rtx_equal_p (XEXP (SET_SRC (set), 0), SET_DEST (set))
	  || !CONST_INT_P (XEXP (SET_SRC (set), 1))
	  || INTVAL (XEXP (SET_SRC (set), 1)) != -1)
	return crp_refuse (bb, "crossrow-pairing-counter-shape", insn);
      lp->counter = insn;
      lp->counter_regno = REGNO (SET_DEST (set));
    }

  if (!lp->separator || !lp->counter || !lp->jump)
    return crp_refuse (bb, "crossrow-pairing-row-shape");
  if (lp->nodes.size () < XTT_CROSSROW_MIN_ROW_WORDS)
    return crp_refuse (bb, "crossrow-pairing-row-too-short");
  if (2 * lp->nodes.size () > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
    return crp_refuse (bb, "crossrow-pairing-capture-budget");

  /* The backedge condition: if (counter != 0) goto header.  */
  rtx jset = single_set (lp->jump);
  rtx cond = jset ? SET_SRC (jset) : nullptr;
  if (!cond || GET_CODE (cond) != IF_THEN_ELSE
      || GET_CODE (XEXP (cond, 0)) != NE
      || !REG_P (XEXP (XEXP (cond, 0), 0))
      || REGNO (XEXP (XEXP (cond, 0), 0)) != lp->counter_regno
      || XEXP (XEXP (cond, 0), 1) != const0_rtx
      || GET_CODE (XEXP (cond, 1)) != LABEL_REF
      || XEXP (cond, 2) != pc_rtx)
    return crp_refuse (bb, "crossrow-pairing-counter-shape", lp->jump);

  /* Row shape: one load first, one store last, no other Dst or RWC
     traffic, flat atoms closed by the all-lanes restore.  */
  bool cc_open = false;
  unsigned cc_begin = 0;
  unsigned loads = 0, stores = 0;
  for (unsigned i = 0; i != lp->nodes.size (); ++i)
    {
      xtt_effect_set eff = rvtt_insn_effects (lp->nodes[i].insn);
      if (eff.rwc.kind != xtt_rwc_effect_t::NONE)
	return crp_refuse (bb, "crossrow-pairing-rwc-inside-row",
			   lp->nodes[i].insn);
      if (eff.cc_write_all_lanes)
	{
	  if (!cc_open)
	    return crp_refuse (bb, "crossrow-pairing-unmatched-restore",
			       lp->nodes[i].insn);
	  lp->atoms.push_back ({cc_begin, i});
	  cc_open = false;
	}
      else if (eff.cc_write && !cc_open)
	{
	  cc_open = true;
	  cc_begin = i;
	}
      if (eff.dst_mem_read || eff.dst_mem_write)
	{
	  if (cc_open)
	    return crp_refuse (bb, "crossrow-pairing-dst-in-cc-window",
			       lp->nodes[i].insn);
	  rtx addr, mode, am;
	  if (!rvtt_dst_access_operands (lp->nodes[i].insn, eff, &addr,
					 &mode, &am)
	      || !CONST_INT_P (addr) || !CONST_INT_P (mode)
	      || !CONST_INT_P (am)
	      || INTVAL (am) != rvtt_no_increment_address_mode ())
	    return crp_refuse (bb, "crossrow-pairing-dst-operands-unproven",
			       lp->nodes[i].insn);
	  if (eff.dst_mem_read)
	    lp->load = i, ++loads;
	  if (eff.dst_mem_write)
	    lp->store = i, ++stores;
	}
    }
  if (cc_open)
    return crp_refuse (bb, "crossrow-pairing-unclosed-cc-window");
  if (loads != 1 || stores != 1 || lp->load != 0
      || lp->store + 1 != lp->nodes.size ())
    return crp_refuse (bb, "crossrow-pairing-row-shape");

  /* Dst disjointness of the paired footprints: both accesses at one
     constant address A in one counter frame, the copy at A+d with the
     separator's own per-iteration advance d (2 address units = one
     row); alignment and range keep the unit footprints disjoint and
     the rebased address encodable.  */
  xtt_effect_set le = rvtt_insn_effects (lp->nodes[lp->load].insn);
  xtt_effect_set se = rvtt_insn_effects (lp->nodes[lp->store].insn);
  rtx la, lm, lam, sa, sm, sam;
  HOST_WIDE_INT addr_limit = 8191;	/* BH imm10-class Dst address */
  if (!rvtt_dst_access_operands (lp->nodes[lp->load].insn, le, &la, &lm,
				 &lam)
      || !rvtt_dst_access_operands (lp->nodes[lp->store].insn, se, &sa,
				    &sm, &sam)
      || INTVAL (la) != INTVAL (sa)
      || lp->dst_step != 2
      || (INTVAL (la) & 3) || INTVAL (la) < 0
      || INTVAL (la) > addr_limit - lp->dst_step)
    return crp_refuse (bb, "crossrow-pairing-dst-disjointness-unproven");
  lp->dst_addr = INTVAL (la);

  /* Counter provenance: the loop's dedicated preheader must seed the
     countdown with an even positive constant (halving -1 -> -2 then
     preserves the NE-0 exit exactly), and the register is referenced
     nowhere else in the row (the SFPU-only proof above covers every
     row word; the separator is all-constant).  */
  basic_block preheader = rotation_dedicated_preheader (bb);
  if (!preheader)
    return crp_refuse (bb, "crossrow-pairing-no-dedicated-preheader");
  rtx_insn *init = nullptr;
  for (rtx_insn *w = BB_END (preheader);
       w && w != PREV_INSN (BB_HEAD (preheader)); w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (CALL_P (w) || asm_noperands (PATTERN (w)) >= 0)
	return crp_refuse (bb, "crossrow-pairing-counter-init-unproven", w);
      if (reg_set_p (SET_DEST (single_set (lp->counter)), w))
	{
	  init = w;
	  break;
	}
    }
  rtx iset = init ? single_set (init) : nullptr;
  if (!iset || !REG_P (SET_DEST (iset))
      || REGNO (SET_DEST (iset)) != lp->counter_regno
      || !CONST_INT_P (SET_SRC (iset)))
    return crp_refuse (bb, "crossrow-pairing-counter-init-unproven");
  lp->trips = INTVAL (SET_SRC (iset));
  if (lp->trips < 2)
    return crp_refuse (bb, "crossrow-pairing-trips-unproven");
  if (lp->trips & 1)
    return crp_refuse (bb, "crossrow-pairing-trips-odd");

  /* Ambient lane state at loop entry.  */
  if (!crp_entry_all_lanes_p (bb, preheader))
    return false;

  return true;
}

/* Queue a typed Dst address replacement on a load/store copy.  Operand
   identity is used only after effect admission, mirroring
   rvtt_dst_access_operands' positional contract.  */

static void
crp_queue_dst_rebase (rtx_insn *insn, HOST_WIDE_INT value)
{
  int code = recog_memoized (insn);
  int addr_pos = code == CODE_FOR_rvtt_sfpload_lv_int ? 4 : 3;
  gcc_assert (code == CODE_FOR_rvtt_sfpload_lv_int
	      || code == CODE_FOR_rvtt_sfpstore_int);
  extract_insn (insn);
  validate_change (insn, recog_data.operand_loc[addr_pos], GEN_INT (value),
		   true);
}

/* Candidate order construction: a dependence-legal global list
   schedule over ITEMS, where each CC atom is one indivisible super-item
   (its words emit contiguously in original interior order -- the
   CC-state-equality placement proof) and every other row word is its
   own item.  Item dependence uses the aggregated register sets through
   the established ls_dependence vocabulary with original (sequential
   two-iteration) order as the dependence direction, so an UNRENAMED
   shared web serializes exactly as the two original iterations would
   (the round-cc-modulo prototype's span construction ignored these
   edges and could order a copy's redefinition ahead of the first row's
   store -- a WAR defect the reference simulator caught, removed here by
   construction).  */

struct crp_item
{
  std::vector<unsigned> members;	/* indices into ALL, in order */
  HARD_REG_SET uses, raw_defs;
  int words;
  int lat;				/* conservative max member latency */
  unsigned orig;			/* first member's original index */
};

/* Register dependence between super-items P (earlier) and C (later):
   RAW, WAW, or WAR overlap between their aggregated register sets.
   Ghost/marker references live in uses only, so pure readers never
   conflict.  */

static bool
crp_item_dep (const crp_item &p, const crp_item &c)
{
  return hard_reg_set_intersect_p (p.raw_defs, c.uses)
    || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs)
    || hard_reg_set_intersect_p (p.uses, c.raw_defs);
}

/* Compute the paired-row candidate emission order for the doubled
   body ALL, whose nodes GROUP assigns to indivisible atom instances
   (runs of one non-negative id; -1 nodes stand alone).  Aggregates
   each atom into a super-item and greedily list-schedules the items
   (details in the in-body comments); dependence edges always point in
   original order, so the returned insn order is legal by
   construction.  */

static std::vector<rtx_insn *>
crp_candidate_order (const std::vector<ls_node> &all,
		     const std::vector<int> &group)
{
  /* Build the item list in original (sequential two-iteration) order:
     GROUP assigns every node its atom instance (a maximal run of one
     non-negative id is one indivisible super-item; -1 nodes -- pure
     words and preservation seeds -- are their own items).  */
  std::vector<crp_item> items;
  unsigned total = all.size ();
  unsigned i = 0;
  while (i != total)
    {
      crp_item it;
      CLEAR_HARD_REG_SET (it.uses);
      CLEAR_HARD_REG_SET (it.raw_defs);
      it.words = 0;
      it.lat = 0;
      it.orig = i;
      unsigned end = i + 1;
      if (group[i] >= 0)
	while (end != total && group[end] == group[i])
	  ++end;
      for (unsigned k = i; k != end; ++k)
	{
	  const ls_node &nd = all[k];
	  it.members.push_back (k);
	  it.uses |= nd.regs.uses;
	  it.raw_defs |= nd.raw_defs;
	  it.words += nd.words;
	  if (nd.lat > it.lat)
	    it.lat = nd.lat;
	}
      items.push_back (std::move (it));
      i = end;
    }

  /* Deterministic greedy list schedule over the items: modeled issue
     time from the item dependence edges (latency-weighted like
     ls_dependence kind 1 for RAW/WAW; issue-order for WAR is the same
     conservative bound here since items are multi-word), earliest
     ready first, critical original order on ties.  Dependence direction
     is original order, so the result is legal by construction.

     Under the stall-words extension the selection among READY items is
     critical-path first (ls_list_order's own priority rule, applied at
     the item granularity): the plain earliest-ready rule drains the
     row-A tail while row B's longer remaining chain is the critical
     path, leaving row B's SFPMUL->SFPSWAP delay shadow bare at the end
     of the body -- one literal SFPNOP, which both costs the modeled
     slot and can push the doubled record past the replay capture
     budget (the capture-overflow belt below).  A critical-path
     selection interleaves the two tails the way the hand kernels do.
     Ties stay on original order; legality is unchanged (the belt
     re-verifies every dependence direction).  */
  unsigned m = items.size ();
  bool cp_priority = riscv_tt_opt_crossrow_pairing_stall_words > 0;
  std::vector<long> cp (m, 0);
  if (cp_priority)
    for (unsigned i = m; i--;)
      {
	long best = items[i].words + items[i].lat;
	for (unsigned j = i + 1; j != m; ++j)
	  {
	    if (!crp_item_dep (items[i], items[j]))
	      continue;
	    long via = cp[j] + items[i].words + items[i].lat;
	    if (via > best)
	      best = via;
	  }
	cp[i] = best;
      }
  std::vector<bool> placed (m, false);
  std::vector<int> finish (m, 0);
  std::vector<rtx_insn *> order;
  int t = 0;
  for (unsigned step = 0; step != m; ++step)
    {
      int best = -1;
      int best_ready = INT_MAX;
      for (unsigned i = 0; i != m; ++i)
	{
	  if (placed[i])
	    continue;
	  bool deps_done = true;
	  int ready = 0;
	  for (unsigned j = 0; j != m; ++j)
	    {
	      if (j == i || items[j].orig > items[i].orig)
		continue;
	      if (!crp_item_dep (items[j], items[i]))
		continue;
	      if (!placed[j])
		{
		  deps_done = false;
		  break;
		}
	      if (finish[j] > ready)
		ready = finish[j];
	    }
	  if (!deps_done)
	    continue;
	  bool take;
	  if (best < 0)
	    take = true;
	  else if (cp_priority)
	    {
	      /* Among items ready by the later of the two ready times,
		 prefer the longer remaining critical path; earlier
		 readiness only wins when the earlier item's issue
		 cannot overlap the other's wait (both comparisons stay
		 deterministic: ties fall to original order).  */
	      int now = t > best_ready ? t : best_ready;
	      int now_i = t > ready ? t : ready;
	      if (now_i != now)
		take = now_i < now;
	      else
		take = cp[i] > cp[best]
		  || (cp[i] == cp[best] && items[i].orig < items[best].orig);
	    }
	  else
	    take = ready < best_ready
	      || (ready == best_ready && items[i].orig < items[best].orig);
	  if (take)
	    {
	      best = (int) i;
	      best_ready = ready;
	    }
	}
      gcc_assert (best >= 0);
      if (best_ready > t)
	t = best_ready;
      t += items[best].words;
      finish[best] = t + items[best].lat;
      placed[best] = true;
      for (unsigned k : items[best].members)
	order.push_back (all[k].insn);
    }
  return order;
}

/* Legality belt: every original-order dependence must keep its
   direction in the candidate.  Returns false on any violation (the
   caller refuses by name; with the constructor above this cannot
   fire, but the pairing never trusts its own scheduler).  */

static bool
crp_order_legal_p (const std::vector<ls_node> &all,
		   const std::vector<rtx_insn *> &candidate)
{
  std::vector<int> pos (all.size (), -1);
  for (unsigned p = 0; p != candidate.size (); ++p)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == candidate[p])
	pos[i] = (int) p;
  for (unsigned i = 0; i != all.size (); ++i)
    if (pos[i] < 0)
      return false;
  for (unsigned i = 0; i != all.size (); ++i)
    for (unsigned j = i + 1; j != all.size (); ++j)
      if (ls_dependence (all[i], all[j]) && pos[i] > pos[j])
	return false;
  return true;
}

/* ---- Shared-reload dedupe (-mtt-tensix-optimize-crossrow-shared-reload)

   The lane-IC residual: the doubled row carries the copy half's
   duplicated in-loop constant materializations (the tanh anatomy: two
   loadi def-groups per half into ONE reload register) because the
   position-blind hard-reg web vocabulary cannot express one half's
   consumer reading the OTHER half's earlier definition.  A naive
   dedupe -- delete the copy's definitions, keep its consumers -- is
   wrong code BEFORE any scheduling: in the sequential original order
   the surviving consumer's nearest preceding definition of the shared
   register is the first half's NEXT-epoch materialization (tanh: row
   B's C3-mad would read row A's C1 loadi), and ls_dependence derives
   value flow from position alone; no edge in the vocabulary can say
   "read the earlier definition".

   The sound form makes position value-correct again: split both
   halves at each definition group into epoch segments and RE-SEQUENCE
   the pairing's original order epoch by epoch (the first half's
   segment, then the copy's with its definitions deleted).  In the
   merged order every surviving consumer sits between its own epoch's
   definition group and the next one, so the established name-based
   vocabulary derives exactly the sharing constraints from position --
   RAW from the epoch definition into both halves' consumers, WAR from
   the copy's consumers into the next epoch's definition -- the greedy
   scheduler cannot commit a value-breaking order (crp_order_legal_p
   re-verifies every edge), and the value-oracle belt below re-walks
   the committed order against the epoch assignment independently.

   Value equivalence of the merged order to the sequential doubled
   baseline: (1) the copy half's definition groups are word-for-word
   identical to the first half's (the textual-copy fact, re-verified
   byte-for-byte after every rename -- a rename that touched either
   web refuses), so a deleted definition's value IS the surviving one;
   (2) the shared register is dead into and out of the loop and every
   consumer follows its group's last member, so each consumer reads
   exactly its epoch's completed 32-bit image; (3) the merge only
   moves copy-half words ahead of LATER-segment first-half words, and
   any such reordered pair must be free of register interaction beyond
   the shared register itself (the interference refusal: the halves'
   other webs are disjoint after renaming, or read-only) -- disjoint
   accesses commute, and the shared register's cross-half readings are
   exactly (1)+(2).  Across iterations the committed record is one
   linear word stream: the next iteration's first definition group
   follows this iteration's last consumer in stream order, the same
   single-register recycling the original row performed.  */

struct crp_shared_reload_info
{
  unsigned reg = ~0u;			/* shared reload register, or ~0u */
  unsigned removed = 0;			/* deleted copy-half def words */
  /* Epoch (1-based) per participating insn, for the value-oracle
     re-verification of the committed order.  */
  std::vector<std::pair<rtx_insn *, unsigned> > def_epoch;
  std::vector<std::pair<rtx_insn *, unsigned> > consumer_epoch;
};

/* Fire and dump the composed shared-reload refusal
   "crossrow-shared-reload-WHY" against register R in BB, naming INSN
   when given.  */

static void
crp_sr_refuse (basic_block bb, const char *why, unsigned r,
	       rtx_insn *insn = nullptr)
{
  rvtt_refusal_fire_composed ("crossrow-shared-reload", why);
  if (dump_file)
    {
      fprintf (dump_file, "Crossrow shared-reload refused: "
	       "crossrow-shared-reload-%s reg %u", why, r);
      if (insn)
	fprintf (dump_file, " (uid=%d)", INSN_UID (insn));
      fprintf (dump_file, " in bb %d\n", bb->index);
    }
}

/* Candidate order plus modeled steady-state II over ALL/GROUP (the
   pairing's own construction and model); INT_MAX on any construction
   failure.  */

static int
crp_model_ii (const std::vector<ls_node> &all, const std::vector<int> &group)
{
  std::vector<rtx_insn *> cand = crp_candidate_order (all, group);
  if (!crp_order_legal_p (all, cand))
    return INT_MAX;
  std::vector<int> idx;
  for (rtx_insn *ci : cand)
    for (unsigned k = 0; k != all.size (); ++k)
      if (all[k].insn == ci)
	{
	  idx.push_back ((int) k);
	  break;
	}
  if (idx.size () != all.size ())
    return INT_MAX;
  return ls_cyclic_ii (all, idx);
}

/* Value-oracle re-verification of the committed candidate order: walk
   the final order and check that the definition groups appear whole,
   in epoch order, and that every surviving consumer's nearest
   preceding definition state is exactly its assigned, completed
   epoch.  Independent of the dependence engine (the pairing never
   trusts its own scheduler).  */

static bool
crp_shared_reload_order_sound_p (const crp_shared_reload_info &sr,
				 const std::vector<rtx_insn *> &candidate)
{
  if (sr.reg == ~0u)
    return true;
  unsigned max_epoch = 0;
  for (const auto &p : sr.def_epoch)
    if (p.second > max_epoch)
      max_epoch = p.second;
  std::vector<unsigned> remaining (max_epoch + 1, 0);
  for (const auto &p : sr.def_epoch)
    ++remaining[p.second];
  unsigned cur_epoch = 0;		/* highest def epoch started */
  unsigned consumed_epoch = 0;		/* highest consumer epoch seen */
  unsigned defs_seen = 0, consumers_seen = 0;
  for (rtx_insn *w : candidate)
    {
      unsigned e = 0;
      bool is_def = false, is_consumer = false;
      for (const auto &p : sr.def_epoch)
	if (p.first == w)
	  {
	    e = p.second;
	    is_def = true;
	    break;
	  }
      if (!is_def)
	for (const auto &p : sr.consumer_epoch)
	  if (p.first == w)
	    {
	      e = p.second;
	      is_consumer = true;
	      break;
	    }
      if (is_def)
	{
	  if (e < cur_epoch)
	    return false;		/* groups interleaved */
	  if (e > cur_epoch
	      && (e != cur_epoch + 1 || remaining[cur_epoch] != 0))
	    return false;		/* group started before the
					   previous one completed */
	  if (consumed_epoch >= e)
	    return false;		/* a consumer already read this
					   epoch's register image */
	  cur_epoch = e;
	  --remaining[e];
	  ++defs_seen;
	}
      else if (is_consumer)
	{
	  if (cur_epoch != e || remaining[e] != 0)
	    return false;		/* wrong or incomplete epoch */
	  if (e > consumed_epoch)
	    consumed_epoch = e;
	  ++consumers_seen;
	}
    }
  return defs_seen == sr.def_epoch.size ()
	 && consumers_seen == sr.consumer_epoch.size ();
}

/* The dedupe proper.  Analyzes the doubled row (ALL/GROUP hold 2*N
   nodes: the first half's words then the copy's), and on full
   admission deletes the copy half's definition groups, re-sequences
   ALL/GROUP into the epoch-merged original order, and fills *OUT for
   the value-oracle belt.  Every unproven piece refuses by name and
   leaves the duplicated pairing untouched.  */

static void
crp_shared_reload (basic_block bb, const crp_loop &lp,
		   std::vector<ls_node> &all, std::vector<int> &group,
		   std::vector<rtx_insn *> &copies, unsigned n,
		   crp_shared_reload_info *out)
{
  if (all.size () != 2 * n)
    {
      /* Preservation seeds were inserted: the index-mirror mapping
	 below (copy word I at N+I) no longer holds.  */
      crp_sr_refuse (bb, "seeded-row", ~0u);
      return;
    }

  struct sr_cand
  {
    unsigned r;
    std::vector<char> is_def;		/* row index -> group member */
    std::vector<int> epoch_of;		/* row index -> consumer epoch */
    std::vector<unsigned> seg_of;	/* row index -> segment */
    unsigned removed;
    unsigned epochs;
  };
  sr_cand best;
  best.removed = 0;

  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    {
      if (fixed_regs[r])
	continue;
      bool any_def = false;
      for (unsigned i = 0; i != n && !any_def; ++i)
	any_def = TEST_HARD_REG_BIT (all[i].raw_defs, r);
      if (!any_def)
	continue;

      sr_cand c;
      c.r = r;
      c.is_def.assign (n, 0);
      c.epoch_of.assign (n, -1);
      c.seg_of.assign (n, 0);

      /* Classify the first half's words against R: definition groups
	 (one fresh constant-only writer plus RMW completions) and
	 consumers, each consumer after its group's last member.  */
      int cur = -1;
      bool open = false;
      bool ok = true;
      for (unsigned i = 0; i != n && ok; ++i)
	{
	  bool d = TEST_HARD_REG_BIT (all[i].raw_defs, r);
	  bool u = TEST_HARD_REG_BIT (all[i].regs.uses, r);
	  if (!d && !u)
	    continue;
	  if (d)
	    {
	      HARD_REG_SET od = all[i].raw_defs;
	      HARD_REG_SET ou = all[i].regs.uses;
	      CLEAR_HARD_REG_BIT (od, r);
	      CLEAR_HARD_REG_BIT (ou, r);
	      if (!hard_reg_set_empty_p (od) || !hard_reg_set_empty_p (ou))
		{
		  /* Not a pure constant-materialization web.  Named
		     only when groups had already formed (a genuine
		     mixed candidate); a register whose defs are plain
		     computation is simply not a reload web.  */
		  if (cur >= 0)
		    crp_sr_refuse (bb, "materialization-shape", r,
				   all[i].insn);
		  ok = false;
		  break;
		}
	      if (!u)
		{
		  ++cur;
		  open = true;
		}
	      else if (cur < 0 || !open)
		{
		  /* An RMW completion outside its group would let a
		     consumer read a partial image.  */
		  crp_sr_refuse (bb, "rmw-outside-group", r, all[i].insn);
		  ok = false;
		  break;
		}
	      c.is_def[i] = 1;
	    }
	  else
	    {
	      if (cur < 0)
		{
		  /* First touch is a read (a live-in invariant, or a
		     value from outside the row): not a reload web;
		     the live-in barrier on real webs is named below.  */
		  ok = false;
		  break;
		}
	      open = false;
	      c.epoch_of[i] = cur;
	    }
	}
      if (!ok || cur < 0)
	continue;
      c.epochs = (unsigned) cur + 1;

      if (REGNO_REG_SET_P (df_get_live_in (bb), r))
	{
	  crp_sr_refuse (bb, "live-in", r);
	  continue;
	}
      if (REGNO_REG_SET_P (df_get_live_out (bb), r))
	{
	  crp_sr_refuse (bb, "live-out", r);
	  continue;
	}

      /* Copy-half mirror: identical R classification word for word,
	 and byte-identical patterns on every definition-group member
	 (a rename that touched either half's web refuses -- the value
	 identity is the textual-copy fact, re-verified, never
	 assumed).  */
      ok = true;
      for (unsigned i = 0; i != n && ok; ++i)
	{
	  bool da = TEST_HARD_REG_BIT (all[i].raw_defs, r);
	  bool ua = TEST_HARD_REG_BIT (all[i].regs.uses, r);
	  bool db = TEST_HARD_REG_BIT (all[n + i].raw_defs, r);
	  bool ub = TEST_HARD_REG_BIT (all[n + i].regs.uses, r);
	  if (da != db || ua != ub)
	    {
	      crp_sr_refuse (bb, "copy-shape", r, all[n + i].insn);
	      ok = false;
	    }
	  else if (c.is_def[i])
	    {
	      /* Byte-identity of the materializations, compared on the
		 word's single SET (the loadi patterns carry a scratch
		 clobber, and two SCRATCHes never compare equal).  */
	      rtx sa = single_set (all[i].insn);
	      rtx sb = single_set (all[n + i].insn);
	      if (!sa || !sb
		  || !rtx_equal_p (SET_DEST (sa), SET_DEST (sb))
		  || !rtx_equal_p (SET_SRC (sa), SET_SRC (sb)))
		{
		  crp_sr_refuse (bb, "web-mutated", r, all[n + i].insn);
		  ok = false;
		}
	    }
	}
      if (!ok)
	continue;

      /* Segments: 0 before the first group; each group opens a new
	 one.  */
      {
	unsigned seg = 0;
	bool in_group = false;
	for (unsigned i = 0; i != n; ++i)
	  {
	    if (c.is_def[i] && !in_group)
	      {
		++seg;
		in_group = true;
	      }
	    else if (!c.is_def[i])
	      in_group = false;
	    c.seg_of[i] = seg;
	  }
      }

      /* CC atoms: no participation, and no atom may span an epoch
	 boundary (the merge interleaves at segment granularity).  */
      ok = true;
      for (const auto &atom : lp.atoms)
	{
	  for (unsigned i = atom.first; i <= atom.second && ok; ++i)
	    if (c.is_def[i] || c.epoch_of[i] >= 0)
	      {
		crp_sr_refuse (bb, "atom-interior", r, all[i].insn);
		ok = false;
	      }
	  if (ok && c.seg_of[atom.first] != c.seg_of[atom.second])
	    {
	      crp_sr_refuse (bb, "atom-spans-epoch", r);
	      ok = false;
	    }
	  if (!ok)
	    break;
	}
      if (!ok)
	continue;

      /* Cross-half interference: the merge moves every surviving
	 copy-half word of segment S ahead of every first-half word of
	 a LATER segment; each such reordered pair must interact
	 through no register but R.  */
      ok = true;
      for (unsigned x = 0; x != n && ok; ++x)
	for (unsigned y = 0; y != n && ok; ++y)
	  {
	    if (c.seg_of[y] >= c.seg_of[x] || c.is_def[y])
	      continue;
	    HARD_REG_SET xd = all[x].raw_defs;
	    HARD_REG_SET xu = all[x].regs.uses;
	    HARD_REG_SET yd = all[n + y].raw_defs;
	    HARD_REG_SET yu = all[n + y].regs.uses;
	    CLEAR_HARD_REG_BIT (xd, r);
	    CLEAR_HARD_REG_BIT (xu, r);
	    CLEAR_HARD_REG_BIT (yd, r);
	    CLEAR_HARD_REG_BIT (yu, r);
	    if (hard_reg_set_intersect_p (xd, yu)
		|| hard_reg_set_intersect_p (xd, yd)
		|| hard_reg_set_intersect_p (xu, yd))
	      {
		crp_sr_refuse (bb, "crossrow-interference", r,
			       all[n + y].insn);
		ok = false;
	      }
	  }
      if (!ok)
	continue;

      c.removed = 0;
      for (unsigned i = 0; i != n; ++i)
	if (c.is_def[i])
	  ++c.removed;
      if (c.removed > best.removed)
	best = c;
    }

  if (best.removed == 0)
    return;

  /* Modeled gate: the deduplicated candidate must not exceed the
     duplicated candidate's steady-state II (the merge tightens the
     cross-half coupling; a shape where lockstep costs more than the
     removed words buy keeps the duplicated pairing).  */
  int ii_dup = crp_model_ii (all, group);
  std::vector<ls_node> merged;
  std::vector<int> merged_group;
  merged.reserve (2 * n - best.removed);
  merged_group.reserve (2 * n - best.removed);
  for (unsigned s = 0; s <= best.epochs; ++s)
    {
      for (unsigned i = 0; i != n; ++i)
	if (best.seg_of[i] == s)
	  {
	    merged.push_back (all[i]);
	    merged_group.push_back (group[i]);
	  }
      for (unsigned i = 0; i != n; ++i)
	if (best.seg_of[i] == s && !best.is_def[i])
	  {
	    merged.push_back (all[n + i]);
	    merged_group.push_back (group[n + i]);
	  }
    }
  gcc_assert (merged.size () == 2 * n - best.removed);
  for (unsigned k = 0; k != merged.size (); ++k)
    merged[k].orig = (int) k;
  int ii_dedup = crp_model_ii (merged, merged_group);
  if (ii_dedup == INT_MAX || ii_dedup > ii_dup)
    {
      crp_sr_refuse (bb, "ii-regression", best.r);
      return;
    }

  /* Fill the value oracle, then verify the merged model's own
     candidate before mutating anything.  */
  crp_shared_reload_info sr;
  sr.reg = best.r;
  sr.removed = best.removed;
  for (unsigned i = 0; i != n; ++i)
    {
      if (best.is_def[i])
	sr.def_epoch.emplace_back (all[i].insn, best.seg_of[i]);
      if (best.epoch_of[i] >= 0)
	{
	  sr.consumer_epoch.emplace_back (all[i].insn, best.seg_of[i]);
	  sr.consumer_epoch.emplace_back (all[n + i].insn, best.seg_of[i]);
	}
    }
  {
    std::vector<rtx_insn *> probe = crp_candidate_order (merged,
							 merged_group);
    if (!crp_order_legal_p (merged, probe)
	|| !crp_shared_reload_order_sound_p (sr, probe))
      {
	crp_sr_refuse (bb, "final-order-unproven", best.r);
	return;
      }
  }

  /* Commit: delete the copy half's definition words (this
     transaction's own copies -- any later whole-pairing refusal
     restores the original single row exactly, the deleted words
     included by never having survived), and install the merged
     original order.  */
  for (unsigned i = n; i-- != 0;)
    if (best.is_def[i])
      {
	rtx_insn *dead = all[n + i].insn;
	for (unsigned k = 0; k != copies.size (); ++k)
	  if (copies[k] == dead)
	    {
	      copies.erase (copies.begin () + k);
	      break;
	    }
	delete_insn (dead);
      }
  all.swap (merged);
  group.swap (merged_group);
  *out = sr;
  if (dump_file)
    fprintf (dump_file, "Crossrow shared-reload: reg %u epochs=%u "
	     "removed=%u II %d -> %d in bb %d\n",
	     best.r, best.epochs, best.removed, ii_dup, ii_dedup,
	     bb->index);
}

/* ---- Rule-B preservation seeds (-mtt-tensix-optimize-crossrow-pairing-seed)

   The Rule-B preservation-seed rename: a collision web whose fresh
   root executes INSIDE a
   flat CC atom cannot rename to a dead LREG directly (the predicated
   root writes only enabled lanes, so the dead register's stale
   disabled-lane bits would reach an all-lanes consumer -- the
   crossrow-pairing-rename-cc-domain refusal above).  It CAN rename
   when a typed all-lanes copy F = R (SFPMOV mod-2, the audited
   hidden-state-free assign: rvtt.md rvtt_sfpassign effect audit) is
   seeded immediately after the LAST definition of R that precedes the
   root: in the ambient position before the atom's first CC writer when
   R reaches the atom entry unwritten, or INSIDE the atom directly
   after R's last in-atom definition (e.g. the atom-opening compare
   whose result the predicated root preserves) -- the interior position
   is sound because SFPMOV mod-2 writes every lane REGARDLESS of the CC
   state, and the seed joins the atom's indivisible item so the
   original words keep their interior order and CC contexts:

     - at the seed, F receives R's complete lane image exactly as it
       stands (whatever mix of earlier all-lanes and predicated writes
       produced it -- the copy is a semantic identity on all 32 lanes);
     - between the seed and the fresh root neither R nor F changes
       (F is untouched by every row word -- the free-register search
       invariant -- and the seed sits after R's last preceding
       definition by placement), so F == R lane by lane at the root;
     - the root then writes the same enabled lanes it originally wrote
       into R, and the disabled lanes of F carry exactly the value the
       disabled lanes of R carried -- including a read-modify-write
       root, whose implicit read now consumes the lane-equal F;
     - every later member of the original web, across all later CC
       domains, is rewritten to F until the web's fresh terminator, so
       the equality is inductive and an all-lanes store observes the
       identical value.

   Each seed is one real issued word: it enters the node vector at its
   sequential position and is charged by the same steady-state II model
   and capture budget as every row word.  The seed set commits only on
   a STRICT modeled II improvement over the unseeded (Rule-A) candidate;
   a forward pass may accept an II-neutral seed only as an enabler, and
   the tail of accepted seeds after the last strict improvement rolls
   back (no rider seeds: every emitted seed either strictly improves the
   modeled II or enables a later seed that does).  Everything unproven
   refuses by name and keeps the unseeded pairing byte-identically.  */

/* Dump helper for the seed phase's named refusals.  */

static void
crp_seed_refuse (basic_block bb, const char *why, unsigned r, rtx_insn *insn)
{
  rvtt_refusal_fire_composed ("crossrow-pairing-seed", why);
  if (dump_file)
    fprintf (dump_file, "Crossrow pairing seed refused: "
	     "crossrow-pairing-seed-%s reg %u uid=%d in bb %d\n",
	     why, r, insn ? INSN_UID (insn) : -1, bb->index);
}

/* Rename the web rooted at node I (register R -> F, members I inclusive
   through EXTENT_END exclusive: every node referencing R, through RMW
   redefinitions) as one recorded transaction.  Mirrors the web-member
   rewrite of ls_cyclic_rename_collisions.  */

static bool
crp_apply_web_rename (std::vector<ls_node> &nodes, unsigned i,
		      unsigned extent_end, unsigned r, unsigned f,
		      std::vector<ls_rename> *record)
{
  ls_rename rn;
  rn.oldr = r;
  rn.newr = f;
  ls_queue_reg_replacements (nodes[i].insn, &PATTERN (nodes[i].insn), r, f);
  rn.insns.push_back (nodes[i].insn);
  for (unsigned k = i + 1; k != extent_end; ++k)
    if (TEST_HARD_REG_BIT (nodes[k].regs.uses, r)
	|| TEST_HARD_REG_BIT (nodes[k].raw_defs, r))
      {
	ls_queue_reg_replacements (nodes[k].insn, &PATTERN (nodes[k].insn),
				   r, f);
	rn.insns.push_back (nodes[k].insn);
      }
  if (!apply_change_group ())
    return false;
  for (rtx_insn *ins : rn.insns)
    df_insn_rescan (ins);
  record->push_back (std::move (rn));
  return true;
}

/* Undo exactly the LAST recorded rename (the seed phase's per-candidate
   rollback; each web's F was untouched elsewhere, so F -> R over the
   web is unambiguous).  */

static void
crp_undo_last_rename (std::vector<ls_rename> *record)
{
  gcc_assert (!record->empty ());
  ls_rename &rn = record->back ();
  for (rtx_insn *ins : rn.insns)
    ls_queue_reg_replacements (ins, &PATTERN (ins), rn.newr, rn.oldr);
  bool ok = apply_change_group ();
  gcc_assert (ok);
  for (rtx_insn *ins : rn.insns)
    df_insn_rescan (ins);
  record->pop_back ();
}

/* ---- MVE kernel-unroll realization (modulo variable expansion, default off) ----

   -mtt-tensix-optimize-mve-expand PERFORMS the modulo-variable-
   expansion the bookkeeping tier (stage 1) only priced: on the counted
   replay-formable kernel the cross-row pairing admits -- that shape IS
   the crp-parity obligation, structurally: Dst rebase, doubled row
   step, halved countdown, capture budget, so the realized kernel stays
   replay-formable by the same admission that lets the pairing fire --
   the doubled row is ordered by an ITEM-LEVEL modulo placement of the
   single kernel (each CC atom one indivisible multi-word item, pure
   words their own items; the same aggregation the pairing's greedy
   scheduler uses) with the second iteration offset by one placement II
   (Lam PLDI-88, kmin = 2 realized as the pairing's two copies), instead
   of the greedy interleave.  Register rotation for the copy's colliding
   webs routes through the du-chain rename service
   (rvtt_lreg_rename_chain: block-free targets first, the temporal tier
   where block-free registers are exhausted), the expansion demand is
   priced against the register file net of loop-live invariants exactly
   as stage 1 prices it, and an independent producer-lockstep belt
   re-verifies every rename's value web (each consumer's nearest-
   preceding producer must keep its identity across the renames -- the
   check a mis-rotated register cannot pass).  The realized order
   commits only under the pairing's own acceptance authority (strict
   modeled steady-state II decrease over the doubled sequential
   baseline, pad-site and capture belts unchanged) AND only when it
   strictly beats the greedy pairing candidate; every unproven piece
   refuses by name and leaves the established pairing path untouched
   (the service webs are undone exactly).

   Refusals by name:
     mve-expand-row-mutated            seeds or shared-reload dedupe
					broke the pure textual pairing
     mve-expand-kmin-beyond-pairing    lifetimes demand > 2 copies
     mve-rename-exhausted              demand does not fit the file net
					of loop-live invariants (stage
					1's name; the realization site)
     mve-expand-lockstep-divergence    the producer-lockstep belt caught
					a diverging rename web
     mve-expand-order-hazard           an unrenamed shared web would
					reverse an original-order
					dependence in the realized order
     mve-expand-no-ii-decrease         the realized order does not
					strictly beat the greedy
					candidate and the baseline  */

/* Nearest-preceding-producer map over ALL in sequential index order:
   for each node, the sorted multiset of producing node indices of its
   register uses (-1 = live into the row).  Rename-invariant for any
   value-preserving whole-web rename; a mis-rotated or partially
   renamed web changes some consumer's producer identity.  */

static std::vector<std::vector<int> >
mve_producer_map (const std::vector<ls_node> &all)
{
  std::vector<std::vector<int> > map (all.size ());
  for (unsigned v = 0; v != all.size (); ++v)
    {
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r))
	    continue;
	  int producer = -1;
	  for (unsigned j = v; j-- != 0;)
	    if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
	      {
		producer = (int) j;
		break;
	      }
	  map[v].push_back (producer);
	}
      for (unsigned i = 1; i < map[v].size (); ++i)  /* insertion sort */
	{
	  int x = map[v][i];
	  unsigned j = i;
	  while (j > 0 && map[v][j - 1] > x)
	    {
	      map[v][j] = map[v][j - 1];
	      --j;
	    }
	  map[v][j] = x;
	}
    }
  return map;
}

/* The realization arm.  ALL/GROUP hold the pairing's doubled row after
   the established collision renames; N is the single-kernel node
   count; BASE_II the doubled sequential baseline.  On success returns
   true with *CANDIDATE_OUT the realized order, *II_OUT its modeled
   steady-state II (strictly below both the greedy candidate's and
   BASE_II), *PLACE_II_OUT the kernel placement II, and *WEBS_OUT the
   committed service webs (the caller undoes them exactly on any later
   refusal).  On every refusal returns false with the state restored
   exactly as at entry.  */

static bool
crp_mve_expand_arm (basic_block bb, std::vector<ls_node> &all,
		    std::vector<int> &group, unsigned n, int base_ii,
		    bool row_mutated,
		    std::vector<rtx_insn *> *candidate_out, int *ii_out,
		    int *place_ii_out,
		    std::vector<rvtt_lreg_rename_web> *webs_out)
{
  if (row_mutated || all.size () != 2 * n)
    {
      rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_MUTATED, dump_file,
		   "Crossrow mve-expand refused: mve-expand-row-mutated "
		   "in bb %d\n", bb->index);
      return false;
    }
  /* Copy-mirror belt: the two halves' item structure must be index-
     isomorphic (it is by construction; the realization never trusts
     its own bookkeeping).  */
  for (unsigned k = 0; k != n; ++k)
    {
      bool a_open = group[k] >= 0 && k > 0 && group[k - 1] == group[k];
      bool b_open = group[n + k] >= 0 && group[n + k - 1] == group[n + k];
      if ((group[k] >= 0) != (group[n + k] >= 0)
	  || (k > 0 && a_open != b_open))
	{
	  rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_MUTATED, dump_file,
		       "Crossrow mve-expand refused: mve-expand-row-mutated "
		       "(halves not item-isomorphic) in bb %d\n", bb->index);
	  return false;
	}
    }

  /* Items over the single kernel (first half): each atom instance one
     indivisible multi-word item, pure words their own items -- the
     pairing's own aggregation, marshalled into the one timing
     vocabulary (data only; the engine's marshaller and placement do
     the rest).  */
  struct mve_item
  {
    std::vector<unsigned> members;	/* indices into ALL, first half */
    HARD_REG_SET uses, raw_defs;
    int words, lat;
  };
  std::vector<mve_item> items;
  {
    unsigned i = 0;
    while (i != n)
      {
	mve_item it;
	CLEAR_HARD_REG_SET (it.uses);
	CLEAR_HARD_REG_SET (it.raw_defs);
	it.words = 0;
	it.lat = 0;
	unsigned end = i + 1;
	if (group[i] >= 0)
	  while (end != n && group[end] == group[i])
	    ++end;
	for (unsigned k = i; k != end; ++k)
	  {
	    it.members.push_back (k);
	    it.uses |= all[k].regs.uses;
	    it.raw_defs |= all[k].raw_defs;
	    it.words += all[k].words;
	    if (all[k].lat > it.lat)
	      it.lat = all[k].lat;
	  }
	items.push_back (std::move (it));
	i = end;
      }
  }
  const unsigned m = items.size ();

  /* Item-granularity seqs for the engine: aggregated register sets
     through the established dependence classification (latency
     conservative at the item maximum -- refusing-direction: an
     overestimated delta only spaces the placement wider).  The INTRA
     matrix carries the full storage classification (within one
     iteration the physical registers are what they are); the CROSS
     matrix carries only the constraints that survive the rotation --
     interactions through registers LIVE INTO THE ROW (invariants and
     loop-carried values, which no rotation renames).  Everything else
     wrapping the kernel onto itself is storage the expansion's whole
     purpose is to rotate away; the optimism is candidate-generation
     only (the exact acceptance model, the legality belt, and the
     producer-lockstep belt judge the realized order downstream).  */
  HARD_REG_SET nonrot;
  CLEAR_HARD_REG_SET (nonrot);
  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    if (REGNO_REG_SET_P (df_get_live_in (bb), r))
      SET_HARD_REG_BIT (nonrot, r);
  rvtt_timing::seq s, cross;
  s.ops.resize (m);
  s.dep.resize (m * m);
  cross.dep.resize (m * m);
  for (unsigned a = 0; a != m; ++a)
    {
      s.ops[a].words = items[a].words;
      s.ops[a].lat = items[a].lat;
      s.ops[a].entry_pin = 0;
      HARD_REG_SET da = items[a].raw_defs;
      HARD_REG_SET ua = items[a].uses;
      da &= nonrot;
      ua &= nonrot;
      for (unsigned b = 0; b != m; ++b)
	{
	  s.dep[a * m + b] = (unsigned char) rvtt_timing::classify_dependence
	    (hard_reg_set_intersect_p (items[a].raw_defs, items[b].uses)
	     || hard_reg_set_intersect_p (items[a].raw_defs,
					  items[b].raw_defs),
	     hard_reg_set_intersect_p (items[a].uses, items[b].raw_defs));
	  cross.dep[a * m + b]
	    = (unsigned char) rvtt_timing::classify_dependence
	      (hard_reg_set_intersect_p (da, items[b].uses)
	       || hard_reg_set_intersect_p (da, items[b].raw_defs),
	       hard_reg_set_intersect_p (ua, items[b].raw_defs));
	}
    }
  cross.ops = s.ops;
  rvtt_timing::mod_prob prob = rvtt_timing::make_mod_prob (s, cross);

  /* Value-lifetime problem for the expansion pricing: RAW flow only.
     The placement problem above must keep the merged RAW/WAW storage
     constraints (within one iteration the physical registers are what
     they are), but kmin and the live-copy demand price what the
     ROTATION must supply -- value copies (Lam's definition); a
     WAW-only span is storage the renames dissolve, and counting it
     would refuse expansions the rename service can trivially fit.
     Fail-closed: an optimistic count here can only admit a candidate
     the legality/lockstep belts and the exact acceptance then judge.  */
  rvtt_timing::seq raws, rawc;
  raws.ops = s.ops;
  rawc.ops = s.ops;
  raws.dep.assign (m * m, (unsigned char) rvtt_timing::DEP_NONE);
  rawc.dep.assign (m * m, (unsigned char) rvtt_timing::DEP_NONE);
  {
    /* Value flow is nearest-definition flow, not register-mask
       intersection: the allocator's register reuse would otherwise
       alias distinct values into one endless "lifetime" (a late
       reader of a REUSED register is not a reader of the early
       value).  */
    std::vector<unsigned> item_of (n, 0);
    for (unsigned a = 0; a != m; ++a)
      for (unsigned k : items[a].members)
	item_of[k] = a;
    for (unsigned v = 0; v != n; ++v)
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r))
	    continue;
	  int producer = -1;
	  for (unsigned j = v; j-- != 0;)
	    if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
	      {
		producer = (int) j;
		break;
	      }
	  if (producer >= 0)
	    raws.dep[item_of[producer] * m + item_of[v]]
	      = (unsigned char) rvtt_timing::DEP_LATENCY;
	  else
	    /* Upward-exposed use: a loop-carried value when the row
	       itself redefines the register later (true iteration-
	       distance-1 flow), an invariant otherwise (no producer
	       edge; its register is in NONROT either way).  */
	    for (unsigned j = v + 1; j != n; ++j)
	      if (TEST_HARD_REG_BIT (all[j].raw_defs, r))
		{
		  rawc.dep[item_of[j] * m + item_of[v]]
		    = (unsigned char) rvtt_timing::DEP_LATENCY;
		  break;
		}
	}
  }
  rvtt_timing::mod_prob prob_raw = rvtt_timing::make_mod_prob (raws, rawc);
  int res = rvtt_timing::resmii (prob);
  int rec = rvtt_timing::recmii (prob);
  int mii = res > rec ? res : rec;
  if (dump_file)
    {
      unsigned omega1 = 0;
      for (unsigned k = 0; k != prob.edges.size (); ++k)
	omega1 += prob.edges[k].omega;
      fprintf (dump_file, "Crossrow mve-expand model: bb %d items=%u "
	       "ResMII=%d RecMII=%d cross-edges=%u\n",
	       bb->index, m, res, rec, omega1);
    }

  /* The greedy pairing candidate this arm must strictly beat at the
     final compare (the shipping selection when the arm refuses); the
     PLACEMENT bound is the baseline only -- the greedy candidate is
     priced on the pre-rotation state, and the rotation renames below
     (the service's temporal tier especially) can lower the realized
     II below orders the pre-rotation state could ever express.  */
  int crp_ii = crp_model_ii (all, group);
  int bound = (base_ii - 1) / 2;	/* realized II ~ 2 * place-II */
  if (rec < 0 || mii > bound)
    {
      rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		   "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		   "in bb %d (MII %d, place bound %d)\n",
		   bb->index, mii, bound);
      return false;
    }
  /* The expansion demand is priced exactly as stage 1 prices it: peak
     simultaneously-live value copies vs the file net of loop-live
     invariants (live into the row, defined by no kernel node).  */
  unsigned invariants = 0;
  for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
    {
      if (!REGNO_REG_SET_P (df_get_live_in (bb), r))
	continue;
      bool defined = false;
      for (unsigned i = 0; i != n && !defined; ++i)
	defined = TEST_HARD_REG_BIT (all[i].raw_defs, r);
      if (!defined)
	++invariants;
    }
  unsigned capacity = rvtt_pressure_capacity ();
  unsigned net = capacity > invariants ? capacity - invariants : 0;

  /* Lam's smallest-fitting-II rule: place at each II from MII up to
     the bound and take the FIRST placement whose expansion fits --
     raising the II shortens the overlap, so the live-copy demand only
     falls; a placement whose demand does not fit at the lowest II may
     fit one slot higher and still beat the greedy candidate.  Refusal
     naming keeps the doorway fact of the LOWEST placed II (the
     8-LREG-wall census convention).  */
  int budget = riscv_tt_ims_budget > 0 ? (int) riscv_tt_ims_budget
					: 8 * (int) m;
  rvtt_timing::mod_placement pl;
  int kmin = 0;
  unsigned demand = 0;
  bool any_budget_exhausted = false, fits = false;
  int first_kmin = 0;
  unsigned first_demand = 0;
  int first_ii = 0;
  for (int ii = mii; ii <= bound && !fits; ++ii)
    {
      rvtt_timing::mod_placement at
	= rvtt_timing::ims_schedule (prob, ii, ii, budget);
      if (!at.scheduled)
	{
	  any_budget_exhausted |= at.budget_exhausted;
	  continue;
	}
      int k = rvtt_timing::mve_kmin (prob_raw, at);
      unsigned d = rvtt_timing::mve_live_demand (prob_raw, at);
      if (k == 1)
	{
	  /* No lifetime exceeds this placement's II: nothing to expand
	     at or above it.  When NO lower II owed an expansion this is
	     simply not a stage-2 row (a dump note, not a refusal --
	     the established candidates own that ground); when a lower
	     II did owe one, fall through so its unfittable demand is
	     adjudicated by name (the doorway fact).  */
	  if (!first_ii && dump_file)
	    fprintf (dump_file, "Crossrow mve-expand: bb %d kmin=1 at "
		     "place-II=%d -- no expansion owed, greedy candidate "
		     "proceeds\n", bb->index, at.ii);
	  if (!first_ii)
	    return false;
	  break;
	}
      if (!first_ii)
	{
	  first_ii = at.ii;
	  first_kmin = k;
	  first_demand = d;
	}
      if (k > 2 || d > net)
	continue;
      pl = at;
      kmin = k;
      demand = d;
      fits = true;
    }
  if (!fits)
    {
      if (first_ii && first_kmin > 2)
	rvtt_refuse (RVTT_REF_MVE_EXPAND_KMIN_BEYOND_PAIRING, dump_file,
		     "Crossrow mve-expand refused: "
		     "mve-expand-kmin-beyond-pairing in bb %d (kmin=%d at "
		     "place-II=%d)\n", bb->index, first_kmin, first_ii);
      else if (first_ii)
	rvtt_refuse (RVTT_REF_MVE_RENAME_EXHAUSTED, dump_file,
		     "Crossrow mve-expand refused: mve-rename-exhausted "
		     "in bb %d (kmin=%d demand=%u capacity=%u invariants=%u "
		     "at place-II=%d)\n",
		     bb->index, first_kmin, first_demand, capacity,
		     invariants, first_ii);
      else if (any_budget_exhausted)
	rvtt_refuse (RVTT_REF_IMS_BUDGET_EXHAUSTED, dump_file,
		     "Crossrow mve-expand refused: ims-budget-exhausted "
		     "in bb %d (MII %d, bound %d, budget %d)\n",
		     bb->index, mii, bound, budget);
      else
	rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		     "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		     "in bb %d (no feasible placement below %d)\n",
		     bb->index, bound);
      return false;
    }

  /* Producer-lockstep reference, captured before any rotation
     rename.  */
  std::vector<std::vector<int> > producers_before = mve_producer_map (all);

  /* Rotation renames: every copy-half fresh definition whose register
     the first iteration also defines is a colliding web the realized
     interleave cannot ride unrenamed; targets route through the
     rename service (block-free first, the temporal tier where
     block-free registers are exhausted -- the service carries the
     complete legality proof and refuses by name inside).  One attempt
     per root insn; committed webs are recorded for exact undo.  */
  std::vector<rvtt_lreg_rename_web> webs;
  auto undo_webs = [&] ()
    {
      for (unsigned i = webs.size (); i--;)
	rvtt_lreg_rename_web_undo (webs[i]);
      if (!webs.empty () && dump_file)
	fprintf (dump_file, "Crossrow mve-expand: undid %zu rotation "
		 "rename(s) in bb %d\n", webs.size (), bb->index);
      webs.clear ();
      ls_refresh_node_regs (all);
    };
  /* Rotated register ASSIGNMENT by placement-slot arithmetic (Lam's
     rotation, kmin = 2): every register's occupancy in the realized
     steady state is the union of its webs' placement-slot windows
     (copy B offset by one placement II, the whole pattern repeating
     every 2*II); a copy-B web moves to the register whose windows are
     free across the web's own window modulo 2*II.  The arithmetic
     only CHOOSES the target -- the rename service then carries the
     complete sequential-order legality proof for the edit (typed
     effects, span/CC rules, death proof, temporal-tier admission
     where the target is busy elsewhere in the block, post-commit
     re-verify), refusing by name inside; and the realized-order
     acceptance downstream prices whatever could not rotate.  */
  const long period = 2 * (long) pl.ii;
  std::vector<unsigned> item_of_node (n, 0);
  for (unsigned a = 0; a != m; ++a)
    for (unsigned k : items[a].members)
      item_of_node[k] = a;
  auto node_slot = [&] (unsigned v) -> long	/* v indexes ALL */
    {
      unsigned half = v < n ? 0 : 1;
      return pl.sigma[item_of_node[v - half * n]] + (long) half * pl.ii;
    };
  /* Occupancy windows per register over the CURRENT doubled row.  */
  struct mve_win { long lo, hi; };
  std::vector<std::vector<mve_win> > occ (SFPU_REG_LAST + 1);
  auto add_windows = [&] ()
    {
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  occ[r].clear ();
	  for (unsigned half = 0; half != 2; ++half)
	    {
	      bool any = false;
	      long lo = 0, hi = 0;
	      for (unsigned k = 0; k != n; ++k)
		{
		  unsigned v = half * n + k;
		  if (!TEST_HARD_REG_BIT (all[v].regs.uses, r)
		      && !TEST_HARD_REG_BIT (all[v].raw_defs, r))
		    continue;
		  long s = node_slot (v);
		  long e = s + all[v].words + all[v].lat;
		  if (!any)
		    {
		      lo = s;
		      hi = e;
		      any = true;
		    }
		  else
		    {
		      if (s < lo)
			lo = s;
		      if (e > hi)
			hi = e;
		    }
		}
	      if (any)
		occ[r].push_back ({lo, hi});
	    }
	}
    };
  auto wins_conflict = [&] (const mve_win &a, const mve_win &b) -> bool
    {
      for (int k = -1; k != 2; ++k)
	{
	  long b1 = b.lo + k * period, b2 = b.hi + k * period;
	  if (a.lo <= b2 && b1 <= a.hi)
	    return true;
	}
      return false;
    };
  for (unsigned k = 0; k != n; ++k)
    for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
      {
	if (!TEST_HARD_REG_BIT (all[n + k].raw_defs, r)
	    || TEST_HARD_REG_BIT (all[n + k].regs.uses, r))
	  continue;		/* not a fresh-definition root */
	bool collision = false;
	for (unsigned j = 0; j != n && !collision; ++j)
	  collision = TEST_HARD_REG_BIT (all[j].raw_defs, r);
	if (!collision)
	  continue;
	/* The copy web's own realized window: the fresh def through the
	   last copy-half touch before the next fresh redefinition.  */
	mve_win w;
	w.lo = node_slot (n + k);
	w.hi = w.lo + all[n + k].words + all[n + k].lat;
	for (unsigned j = k + 1; j != n; ++j)
	  {
	    unsigned v = n + j;
	    if (TEST_HARD_REG_BIT (all[v].raw_defs, r)
		&& !TEST_HARD_REG_BIT (all[v].regs.uses, r))
	      break;		/* next fresh writer: web ends */
	    if (TEST_HARD_REG_BIT (all[v].regs.uses, r)
		|| TEST_HARD_REG_BIT (all[v].raw_defs, r))
	      {
		long e = node_slot (v) + all[v].words + all[v].lat;
		if (e > w.hi)
		  w.hi = e;
	      }
	  }
	add_windows ();
	for (unsigned t = SFPU_REG_FIRST; t <= SFPU_REG_LAST; ++t)
	  {
	    if (t == r || fixed_regs[t] || TEST_HARD_REG_BIT (nonrot, t))
	      continue;
	    bool free_across = true;
	    for (const mve_win &o : occ[t])
	      if (wins_conflict (w, o))
		{
		  free_across = false;
		  break;
		}
	    if (!free_across)
	      continue;
	    rvtt_lreg_rename_web web;
	    if (!rvtt_lreg_rename_chain (bb, all[n + k].insn,
					 (int) (t - SFPU_REG_FIRST), &web))
	      continue;		/* refused by name in the service; the
				   next slot-free target may prove */
	    webs.push_back (web);
	    ls_refresh_node_regs (all);
	    if (dump_file)
	      fprintf (dump_file, "Crossrow mve-expand: rotation rename "
		       "L%d -> L%d at uid=%d in bb %d (window %ld..%ld "
		       "mod %ld)\n",
		       web.old_l, web.new_l, INSN_UID (all[n + k].insn),
		       bb->index, w.lo, w.hi, period);
	    break;
	  }
	break;			/* one rename attempt per root insn */
      }

  /* Deliberate mis-rotation under the testing knob: the last committed
     web's WRITER is re-pointed back at its old register while its
     readers keep the new one -- the classic partial-web wrong code the
     belt below must catch (the red/green proof).  */
  bool sabotaged = false;
  rtx_insn *sab_insn = nullptr;
  unsigned sab_oldr = 0, sab_newr = 0;
  if (riscv_tt_mve_expand_sabotage && !webs.empty ())
    {
      const rvtt_lreg_rename_web &w = webs.back ();
      sab_insn = w.insns[0];
      sab_oldr = SFPU_REG_FIRST + w.new_l;
      sab_newr = SFPU_REG_FIRST + w.old_l;
      ls_queue_reg_replacements (sab_insn, &PATTERN (sab_insn), sab_oldr,
				 sab_newr);
      if (apply_change_group ())
	{
	  df_insn_rescan (sab_insn);
	  ls_refresh_node_regs (all);
	  sabotaged = true;
	}
    }
  auto undo_sabotage = [&] ()
    {
      if (!sabotaged)
	return;
      ls_queue_reg_replacements (sab_insn, &PATTERN (sab_insn), sab_newr,
				 sab_oldr);
      bool ok = apply_change_group ();
      gcc_assert (ok);
      df_insn_rescan (sab_insn);
      ls_refresh_node_regs (all);
      sabotaged = false;
    };

  /* The producer-lockstep belt: every consumer's producer multiset
     must be exactly what it was -- a value-preserving whole-web rename
     cannot change it; a mis-rotated or partial web must.  Independent
     of the dependence engine (recomputed from the raw register sets).  */
  std::vector<std::vector<int> > producers_after = mve_producer_map (all);
  if (producers_after != producers_before)
    {
      gcc_checking_assert (sabotaged);	/* the service cannot diverge */
      undo_sabotage ();
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_LOCKSTEP_DIVERGENCE, dump_file,
		   "Crossrow mve-expand refused: "
		   "mve-expand-lockstep-divergence in bb %d (renames "
		   "undone)\n", bb->index);
      return false;
    }
  /* A clean lockstep under the sabotage knob means the sabotage edit
     never applied; drop it either way before judging the order.  */
  undo_sabotage ();

  /* Realized order: both halves' items at their modulo issue slots,
     the copy offset by one placement II; members emit contiguously in
     original interior order (atoms stay indivisible).  The emission is
     a SLOT-KEYED TOPOLOGICAL walk over the doubled row's item
     dependences, re-aggregated AFTER the rotation renames: an item
     issues at its placement slot unless a dependence that survived the
     renames still holds it back (an unrenamed shared web), in which
     case it waits for its predecessors -- legal by construction, and
     the exact acceptance below prices whatever the surviving storage
     cost the placement.  */
  struct mve_ditem
  {
    HARD_REG_SET uses, defs;
    long slot;
    unsigned half, idx;
  };
  std::vector<mve_ditem> ditems (2 * m);
  for (unsigned half = 0; half != 2; ++half)
    for (unsigned a = 0; a != m; ++a)
      {
	mve_ditem &d = ditems[half * m + a];
	d.half = half;
	d.idx = a;
	CLEAR_HARD_REG_SET (d.uses);
	CLEAR_HARD_REG_SET (d.defs);
	for (unsigned k : items[a].members)
	  {
	    d.uses |= all[half * n + k].regs.uses;
	    d.defs |= all[half * n + k].raw_defs;
	  }
	d.slot = pl.sigma[a] + (long) half * pl.ii;
      }
  auto ditem_dep = [&] (unsigned j, unsigned i) -> bool
    {
      /* Original-order dependence j -> i (j earlier).  */
      return hard_reg_set_intersect_p (ditems[j].defs, ditems[i].uses)
	     || hard_reg_set_intersect_p (ditems[j].defs, ditems[i].defs)
	     || hard_reg_set_intersect_p (ditems[j].uses, ditems[i].defs);
    };
  candidate_out->clear ();
  std::vector<bool> emitted (2 * m, false);
  for (unsigned step = 0; step != 2 * m; ++step)
    {
      int best = -1;
      for (unsigned i = 0; i != 2 * m; ++i)
	{
	  if (emitted[i])
	    continue;
	  bool ready = true;
	  for (unsigned j = 0; j != i && ready; ++j)
	    if (!emitted[j] && ditem_dep (j, i))
	      ready = false;
	  if (!ready)
	    continue;
	  if (best < 0
	      || ditems[i].slot < ditems[best].slot
	      || (ditems[i].slot == ditems[best].slot
		  && (ditems[i].half < ditems[best].half
		      || (ditems[i].half == ditems[best].half
			  && ditems[i].idx < ditems[best].idx))))
	    best = (int) i;
	}
      gcc_assert (best >= 0);	/* original order is always admissible */
      emitted[best] = true;
      for (unsigned k : items[ditems[best].idx].members)
	candidate_out->push_back (all[ditems[best].half * n + k].insn);
    }

  /* Legality belt: original-order dependences that survive the
     rotation renames must keep their direction (an unrenamed shared
     web serializes the copies -- the realized interleave cannot ride
     it).  */
  if (!crp_order_legal_p (all, *candidate_out))
    {
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_ORDER_HAZARD, dump_file,
		   "Crossrow mve-expand refused: mve-expand-order-hazard "
		   "in bb %d (renames undone)\n", bb->index);
      return false;
    }

  /* The realized kernel's steady state, judged by the one wrapped
     acceptance model; it must strictly beat both the greedy candidate
     and the doubled sequential baseline.  */
  std::vector<int> idx;
  for (rtx_insn *ci : *candidate_out)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == ci)
	{
	  idx.push_back ((int) i);
	  break;
	}
  int mve_ii = idx.size () == all.size () ? ls_cyclic_ii (all, idx)
					   : INT_MAX;
  if (mve_ii >= (crp_ii < base_ii ? crp_ii : base_ii))
    {
      undo_webs ();
      rvtt_refuse (RVTT_REF_MVE_EXPAND_NO_II_DECREASE, dump_file,
		   "Crossrow mve-expand refused: mve-expand-no-ii-decrease "
		   "in bb %d (realized %d vs greedy %d, base %d)\n",
		   bb->index, mve_ii, crp_ii, base_ii);
      return false;
    }

  if (dump_file)
    fprintf (dump_file, "Crossrow mve-expand: bb %d items=%u "
	     "ResMII=%d RecMII=%d place-II=%d kmin=%d demand=%u "
	     "invariants=%u renames=%zu realized II %d (greedy %d, "
	     "base %d)\n",
	     bb->index, m, res, rec, pl.ii, kmin, demand, invariants,
	     webs.size (), mve_ii, crp_ii, base_ii);
  *ii_out = mve_ii;
  *place_ii_out = pl.ii;
  *webs_out = webs;
  return true;
}

/* The transform proper.  Returns true when the pairing committed.  */

static bool
crp_pair_loop (basic_block bb, std::vector<basic_block> &visited)
{
  crp_loop lp;
  if (!crp_admit_loop (bb, &lp))
    return false;

  unsigned n = lp.nodes.size ();

  /* Phase 2a: emit row B as a textual copy of row A, after A's last
     word and before the separator.  Every later refusal deletes the
     copies, leaving the original stream byte-identical (A's own words
     are never mutated before the commit point).  */
  std::vector<rtx_insn *> copies;
  rtx_insn *after = lp.nodes[n - 1].insn;
  for (unsigned i = 0; i != n; ++i)
    {
      rtx_insn *cp = emit_insn_after (copy_insn (PATTERN (lp.nodes[i].insn)),
				      after);
      df_insn_rescan (cp);
      copies.push_back (cp);
      after = cp;
    }
  auto crp_delete_copies = [&copies] ()
    {
      for (rtx_insn *cp : copies)
	delete_insn (cp);
    };

  /* Phase 2b: rebase row B's Dst accesses to the second row of the
     shared counter frame.  */
  crp_queue_dst_rebase (copies[lp.load], lp.dst_addr + lp.dst_step);
  crp_queue_dst_rebase (copies[lp.store], lp.dst_addr + lp.dst_step);
  if (!apply_change_group ())
    {
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-dst-rebase-constraint");
    }
  df_insn_rescan (copies[lp.load]);
  df_insn_rescan (copies[lp.store]);

  /* Phase 2c: node vectors over the doubled row.  The copies re-admit
     through the same vocabulary (they are textual copies with typed
     constant rewrites); a failure here is fail-closed, not an ICE.  */
  std::vector<ls_node> all = lp.nodes;
  for (unsigned i = 0; i != n; ++i)
    {
      ls_node node;
      if (!crp_node (bb, copies[i], &node))
	{
	  crp_delete_copies ();
	  return crp_refuse (bb, "crossrow-pairing-copy-unproven",
			     copies[i]);
	}
      node.orig = n + i;
      all.push_back (node);
    }

  /* Baseline: the two logical iterations in their original sequential
     order (exactly the stream the doubled loop would execute), judged
     by the same cyclic steady-state model as the candidate.  Measured
     BEFORE any rename.  */
  std::vector<int> base_order (2 * n);
  for (unsigned i = 0; i != 2 * n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (all, base_order);
  unsigned pads_before = ls_pad_sites (visited, bb, all);

  /* Phase 2d: break allocator-packed false recurrences; fresh webs may
     root only in the ambient all-lanes state (never inside an atom).  */
  std::vector<bool> start_allowed (2 * n, true);
  for (unsigned half = 0; half != 2; ++half)
    for (const auto &atom : lp.atoms)
      for (unsigned i = atom.first; i <= atom.second; ++i)
	start_allowed[half * n + i] = false;
  std::vector<ls_rename> renames;
  /* Under the stall-words extension the copy half's webs take the free
     LREGs first: breaking row-B serialization is the pairing's whole
     benefit, and an intra-row false-recurrence rename must not starve
     it (the tanh anatomy: three free LREGs, three row-B webs, and the
     row-A loadi WAW web grabbing one leaves the row-B accumulator
     serialized -- II gate refuses and the transform dies).  */
  std::vector<unsigned> scan_order;
  if (riscv_tt_opt_crossrow_pairing_stall_words > 0)
    {
      for (unsigned i = n; i != 2 * n; ++i)
	scan_order.push_back (i);
      for (unsigned i = 0; i != n; ++i)
	scan_order.push_back (i);
    }
  ls_cyclic_rename_collisions (bb, all, &renames, &start_allowed,
			       scan_order.empty () ? nullptr : &scan_order);

  /* Item grouping over the doubled row: every atom instance is one
     indivisible super-item id; pure words (and any later preservation
     seeds) are -1 singletons.  */
  std::vector<int> group (2 * n, -1);
  {
    int gid = 0;
    for (unsigned half = 0; half != 2; ++half)
      for (const auto &atom : lp.atoms)
	{
	  for (unsigned i = atom.first; i <= atom.second; ++i)
	    group[half * n + i] = gid;
	  ++gid;
	}
  }

  /* Modeled steady-state II of the current candidate order (INT_MAX on
     a construction failure -- the caller's belts refuse).  */
  auto crp_current_ii = [&all, &group] () -> int
    {
      std::vector<rtx_insn *> cand = crp_candidate_order (all, group);
      if (!crp_order_legal_p (all, cand))
	return INT_MAX;
      std::vector<int> idx;
      for (rtx_insn *ci : cand)
	for (unsigned k = 0; k != all.size (); ++k)
	  if (all[k].insn == ci)
	    {
	      idx.push_back ((int) k);
	      break;
	    }
      if (idx.size () != all.size ())
	return INT_MAX;
      return ls_cyclic_ii (all, idx);
    };

  /* Phase 2d': Rule-B preservation seeds (sub-flag; see the header
     comment above crp_seed_refuse).  Fail-closed: any refusal keeps
     the unseeded Rule-A state exactly.  */
  std::vector<rtx_insn *> seed_insns;
  auto crp_delete_seeds = [&seed_insns] ()
    {
      for (unsigned k = seed_insns.size (); k--;)
	delete_insn (seed_insns[k]);
      seed_insns.clear ();
    };
  if (riscv_tt_opt_crossrow_pairing_seed)
    {
      int rule_a_ii = crp_current_ii ();
      int cur_ii = rule_a_ii;
      int strict_ii = rule_a_ii;	/* best strictly-improved II */
      unsigned strict_commits = 0;	/* commits kept at that point */
      /* One entry per accepted Rule-B rename, parallel to the tail of
	 RENAMES: the emitted seed word, or null for a full-lane root
	 (a bare all-lanes copy needs no preservation seed -- it writes
	 every lane itself, so the fresh register never exposes dead
	 bits; preservation-seed Rule A carried into the atom interior by the
	 mod-2 lane-immunity fact).  */
      std::vector<rtx_insn *> commits;
      bool progress = rule_a_ii != INT_MAX;
      while (progress)
	{
	  progress = false;
	  for (unsigned i = 0; i != all.size () && !progress; ++i)
	    {
	      if (group[i] < 0)
		continue;	/* Rule-B roots live inside atoms */
	      unsigned af = i;
	      while (af && group[af - 1] == group[i])
		--af;
	      if (af == i)
		continue;	/* the atom-opening CC writer roots no
				   Rule-B web (no ambient point between
				   it and the seed would separate them) */
	      for (unsigned r = SFPU_REG_FIRST;
		   r <= SFPU_REG_LAST && !progress; ++r)
		{
		  if (!TEST_HARD_REG_BIT (all[i].raw_defs, r))
		    continue;
		  bool earlier = false;
		  for (unsigned j = 0; j != i && !earlier; ++j)
		    earlier = TEST_HARD_REG_BIT (all[j].raw_defs, r);
		  if (!earlier)
		    continue;	/* no collision */
		  /* A FULL-LANE root needs no seed: the bare all-lanes
		     copy (SFPMOV mod-2, the audited full-copy-semantics
		     spill vocabulary) writes every lane regardless of
		     the CC state, so the fresh register carries the
		     complete value from the root on and no disabled
		     lane can expose dead bits (preservation-seed Rule A,
		     carried into the atom interior by the mod-2
		     lane-immunity fact).  */
		  bool full_lane_root = bare_lreg_copy_p (all[i].insn)
		    && !TEST_HARD_REG_BIT (all[i].regs.uses, r);
		  /* Seed placement (predicated roots): the root must
		     observe exactly the value the seed captured, so the
		     seed sits after the LAST definition of R that
		     precedes the root -- in the ambient position
		     immediately before the atom when R reaches the atom
		     entry unwritten, or INSIDE the atom immediately
		     after R's last in-atom definition (e.g. the
		     atom-opening compare that produces the value the
		     predicated root preserves).  The interior position
		     is sound because the seed word itself is lane-
		     immune -- the bare-SET SFPMOV mod-2 writes every
		     lane regardless of the CC state (the audited
		     hidden-state-free fact) -- and it joins the atom's
		     indivisible item, so the original words' interior
		     order and CC contexts are untouched.  */
		  unsigned seed_pos = af;
		  for (unsigned k = af; k != i; ++k)
		    if (TEST_HARD_REG_BIT (all[k].raw_defs, r))
		      seed_pos = k + 1;
		  int seed_group = seed_pos == af ? -1 : group[i];
		  if (REGNO_REG_SET_P (df_get_live_in (bb), r))
		    {
		      crp_seed_refuse (bb, "live-in", r, all[i].insn);
		      continue;
		    }
		  /* Web extent: through RMW redefinitions, exclusive
		     before the next fresh writer (the established web
		     discipline).  */
		  unsigned extent_end = all.size ();
		  bool fresh_terminator = false;
		  for (unsigned k = i + 1; k != all.size (); ++k)
		    if (TEST_HARD_REG_BIT (all[k].raw_defs, r)
			&& !TEST_HARD_REG_BIT (all[k].regs.uses, r))
		      {
			extent_end = k;
			fresh_terminator = true;
			break;
		      }
		  if (!fresh_terminator
		      && REGNO_REG_SET_P (df_get_live_out (bb), r))
		    {
		      crp_seed_refuse (bb, "live-out", r, all[i].insn);
		      continue;
		    }
		  int f = -1;
		  for (unsigned c = SFPU_REG_FIRST;
		       c <= SFPU_REG_LAST && f < 0; ++c)
		    {
		      if (fixed_regs[c])
			continue;
		      bool touched = false;
		      for (unsigned j = 0; j != all.size () && !touched; ++j)
			touched = TEST_HARD_REG_BIT (all[j].regs.uses, c)
				  || TEST_HARD_REG_BIT (all[j].raw_defs, c);
		      if (touched
			  || REGNO_REG_SET_P (df_get_live_in (bb), c)
			  || REGNO_REG_SET_P (df_get_live_out (bb), c))
			continue;
		      f = (int) c;
		    }
		  if (f < 0)
		    {
		      crp_seed_refuse (bb, "no-free-lreg", r, all[i].insn);
		      continue;
		    }
		  /* The doubled row plus every seed must still fit the
		     replay capture buffer (mirror of the admission
		     bound), or the counted-loop capture downstream
		     stops firing.  */
		  if (!full_lane_root
		      && all.size () + 1
			 > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
		    {
		      crp_seed_refuse (bb, "capture-budget", r, all[i].insn);
		      continue;
		    }
		  /* Emit the all-lanes preservation copy at the chosen
		     position (predicated roots only), then re-admit it
		     through the row vocabulary.  */
		  rtx_insn *seed = nullptr;
		  ls_node seed_node;
		  if (!full_lane_root)
		    {
		      seed = emit_insn_before (gen_rvtt_sfpassign
						 (gen_rtx_REG (XTT32SImode,
							       (unsigned) f),
						  gen_rtx_REG (XTT32SImode,
							       r)),
					       all[seed_pos].insn);
		      df_insn_rescan (seed);
		      if (!crp_node (bb, seed, &seed_node))
			{
			  delete_insn (seed);
			  crp_seed_refuse (bb, "word-unproven", r,
					   all[i].insn);
			  continue;
			}
		      seed_node.orig = (int) seed_pos;
		    }
		  if (!crp_apply_web_rename (all, i, extent_end, r,
					     (unsigned) f, &renames))
		    {
		      if (seed)
			delete_insn (seed);
		      crp_seed_refuse (bb, "rename-constraint", r,
				       all[i].insn);
		      continue;
		    }
		  if (seed)
		    {
		      all.insert (all.begin () + seed_pos, seed_node);
		      group.insert (group.begin () + seed_pos, seed_group);
		    }
		  ls_refresh_node_regs (all);
		  int ii = crp_current_ii ();
		  if (ii > cur_ii)
		    {
		      /* The charged seed does not pay here (an
			 II-neutral commit is retained only as a possible
			 enabler; a worse one never).  */
		      if (seed)
			{
			  all.erase (all.begin () + seed_pos);
			  group.erase (group.begin () + seed_pos);
			}
		      crp_undo_last_rename (&renames);
		      if (seed)
			delete_insn (seed);
		      ls_refresh_node_regs (all);
		      crp_seed_refuse (bb, "no-ii-improvement", r,
				       all[i].insn);
		      continue;
		    }
		  commits.push_back (seed);
		  if (dump_file)
		    {
		      char seed_desc[32];
		      if (seed)
			snprintf (seed_desc, sizeof seed_desc, "uid=%d",
				  INSN_UID (seed));
		      else
			snprintf (seed_desc, sizeof seed_desc,
				  "none-full-lane-root");
		      fprintf (dump_file, "Crossrow pairing seed: reg %u -> "
			       "%u web at uid=%d (%zu insns) seed %s "
			       "II %d -> %d in bb %d\n",
			       r, (unsigned) f,
			       INSN_UID (renames.back ().insns[0]),
			       renames.back ().insns.size (), seed_desc,
			       cur_ii, ii, bb->index);
		    }
		  cur_ii = ii;
		  if (cur_ii < strict_ii)
		    {
		      strict_ii = cur_ii;
		      strict_commits = commits.size ();
		    }
		  progress = true;
		}
	    }
	}
      /* No rider commits: roll back everything after the last STRICT
	 modeled improvement (all of it when nothing improved).  */
      if (commits.size () > strict_commits)
	{
	  rvtt_refuse (RVTT_REF_CROSSROW_PAIRING_SEED_NO_II_IMPROVEMENT,
		       dump_file,
		       "Crossrow pairing seeds rolled back: "
		       "crossrow-pairing-seed-no-ii-improvement in bb %d "
		       "(kept=%u of %zu, II %d)\n",
		       bb->index, strict_commits, commits.size (),
		       strict_ii);
	  while (commits.size () > strict_commits)
	    {
	      rtx_insn *seed = commits.back ();
	      commits.pop_back ();
	      if (seed)
		for (unsigned k = 0; k != all.size (); ++k)
		  if (all[k].insn == seed)
		    {
		      all.erase (all.begin () + k);
		      group.erase (group.begin () + k);
		      break;
		    }
	      crp_undo_last_rename (&renames);
	      if (seed)
		delete_insn (seed);
	    }
	  ls_refresh_node_regs (all);
	}
      for (rtx_insn *seed : commits)
	if (seed)
	  seed_insns.push_back (seed);
    }

  /* Phase 2d'': shared-reload dedupe (sub-flag; see the header comment
     above crp_shared_reload).  Fail-closed both ways: any admission
     refusal keeps the duplicated pairing exactly, and a committed
     dedupe that any LATER belt refuses abandons the whole pairing
     transaction -- the deleted words were this transaction's own
     copies, so the restore paths below still return the original
     single row byte-identically.  */
  crp_shared_reload_info shared_reload;
  if (riscv_tt_opt_crossrow_shared_reload > 0)
    crp_shared_reload (bb, lp, all, group, copies, n, &shared_reload);

  /* Phase 2e-mve (modulo variable expansion, sub-flag): the kernel-unroll
     realization -- an item-level modulo placement of the single kernel
     orders the doubled row with the copy offset by one placement II,
     rotation renames routed through the rename service.  Fail-closed:
     every refusal inside restores the state exactly and the greedy
     path below proceeds untouched; a success hands over the realized
     order plus the committed service webs (undone exactly on any later
     belt's refusal).  */
  std::vector<rvtt_lreg_rename_web> mve_webs;
  auto crp_undo_mve_webs = [&mve_webs, &all] ()
    {
      for (unsigned i = mve_webs.size (); i--;)
	rvtt_lreg_rename_web_undo (mve_webs[i]);
      if (!mve_webs.empty ())
	{
	  mve_webs.clear ();
	  ls_refresh_node_regs (all);
	}
    };
  bool used_mve = false;
  int mve_ii = 0, mve_place_ii = 0;
  std::vector<rtx_insn *> candidate;
  if (riscv_tt_opt_mve_expand)
    used_mve = crp_mve_expand_arm (bb, all, group, n, base_ii,
				   !seed_insns.empty ()
				   || shared_reload.reg != ~0u,
				   &candidate, &mve_ii, &mve_place_ii,
				   &mve_webs);

  /* Phase 2e: candidate order -- the dependence-legal global item
     schedule (atoms indivisible, unrenamed shared webs serialize).  */
  if (!used_mve)
    candidate = crp_candidate_order (all, group);
  if (!crp_order_legal_p (all, candidate))
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-order-hazard");
    }
  if (!crp_shared_reload_order_sound_p (shared_reload, candidate))
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-shared-reload-final-order");
    }

  std::vector<int> cand_index;
  for (rtx_insn *ci : candidate)
    for (unsigned i = 0; i != all.size (); ++i)
      if (all[i].insn == ci)
	{
	  cand_index.push_back (i);
	  break;
	}
  int cand_ii = cand_index.size () == all.size ()
    ? ls_cyclic_ii (all, cand_index) : base_ii;
  if (cand_ii >= base_ii)
    {
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      if (dump_file)
	fprintf (dump_file, "Crossrow pairing refused: no modeled "
		 "steady-state II decrease in bb %d (%d -> %d)\n",
		 bb->index, base_ii, cand_ii);
      return false;
    }

  /* Phase 2f: exact-restore record, then commit the order.  */
  rtx_insn *anchor = PREV_INSN (lp.nodes[0].insn);
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == lp.separator)
	break;
    }
  rtx_insn *tail = anchor;
  for (rtx_insn *ci : candidate)
    {
      if (PREV_INSN (ci) != tail)
	reorder_insns (ci, ci, tail);
      tail = ci;
    }
  if (PREV_INSN (lp.separator) != tail)
    reorder_insns (lp.separator, lp.separator, tail);

  auto crp_restore_chain = [&chain, anchor] ()
    {
      rtx_insn *at = anchor;
      for (rtx_insn *ci : chain)
	{
	  if (PREV_INSN (ci) != at)
	    reorder_insns (ci, ci, at);
	  at = ci;
	}
    };

  unsigned pads_after = ls_pad_sites (visited, bb, all);
  if (pads_after > pads_before)
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-pad-site-increase");
    }
  /* Capture-overflow belt (stall-words extension): the doubled record
     the downstream counted-loop capture will see is every row word
     PLUS every pad the nop inserter still owes the final order; at
     2n == XTT_DELIVERY_CAPTURE_SLOTS a single surviving pad site
     silently trades the record-plus-launch delivery for a rolled
     issue stream (the adjudicated round-cc-modulo profitability
     defect).  Refuse rather than roll.  */
  if (riscv_tt_opt_crossrow_pairing_stall_words > 0
      && all.size () + pads_after > (unsigned) XTT_DELIVERY_CAPTURE_SLOTS)
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-capture-overflow");
    }

  /* Phase 2g: shared control rewrite -- the separator advances both
     rows at once and the countdown halves.  */
  extract_insn (lp.separator);
  validate_change (lp.separator, recog_data.operand_loc[1],
		   GEN_INT (2 * lp.dst_step), true);
  rtx cset = single_set (lp.counter);
  validate_change (lp.counter, &XEXP (SET_SRC (cset), 1), GEN_INT (-2),
		   true);
  if (!apply_change_group ())
    {
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-control-rewrite-constraint");
    }
  df_insn_rescan (lp.separator);
  df_insn_rescan (lp.counter);

  /* Post-commit belt: the doubled separator must still derive as the
     typed row step at exactly twice the advance.  */
  xtt_effect_set sep_eff = rvtt_insn_effects (lp.separator);
  if (sep_eff.opaque || sep_eff.rwc.kind != xtt_rwc_effect_t::INC
      || sep_eff.rwc.dst_delta != 2 * lp.dst_step
      || sep_eff.rwc.cr_delta != 0)
    {
      extract_insn (lp.separator);
      validate_change (lp.separator, recog_data.operand_loc[1],
		       GEN_INT (lp.dst_step), true);
      cset = single_set (lp.counter);
      validate_change (lp.counter, &XEXP (SET_SRC (cset), 1), GEN_INT (-1),
		       true);
      bool restored = apply_change_group ();
      gcc_assert (restored);
      df_insn_rescan (lp.separator);
      df_insn_rescan (lp.counter);
      crp_restore_chain ();
      crp_undo_mve_webs ();
      ls_undo_renames (renames);
      crp_delete_seeds ();
      crp_delete_copies ();
      return crp_refuse (bb, "crossrow-pairing-row-step-shape",
			 lp.separator);
    }

  if (dump_file)
    {
      fprintf (dump_file, "Crossrow pairing: bb %d rows=2 nodes=%zu "
	       "II %d -> %d renames=%zu seeds=%zu dst-addr=%ld/%ld "
	       "step=%ld->%ld trips=%ld->%ld target=bh\n",
	       bb->index, all.size (), base_ii, cand_ii, renames.size (),
	       seed_insns.size (),
	       (long) lp.dst_addr, (long) (lp.dst_addr + lp.dst_step),
	       (long) lp.dst_step, (long) (2 * lp.dst_step),
	       (long) lp.trips, (long) (lp.trips / 2));
      if (used_mve)
	fprintf (dump_file, "Crossrow pairing mve-expand committed: bb %d "
		 "kmin=2 place-II=%d rotation-renames=%zu realized II %d\n",
		 bb->index, mve_place_ii, mve_webs.size (), mve_ii);
    }
  return true;
}

/* Cross-row pairing driver: refresh dataflow, then attempt the pairing
   transaction on every block of FN (non-self-loop blocks fall out of
   admission silently).  */

void
crossrow_pair_rows (function *fn)
{
  df_analyze ();
  std::vector<basic_block> visited;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      visited.reserve (n_basic_blocks_for_fn (fn));
      crp_pair_loop (bb, visited);
    }
}
