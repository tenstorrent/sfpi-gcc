/* Tensix Dst auto-increment: the scan and model half.

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

/* Split out of rtl-rvtt-dst-autoincr.cc; see
   rtl-rvtt-dst-autoincr-int.h for what crosses.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "print-rtl.h"
#include "cfgloop.h"
#include "cfgrtl.h"
#include "dominance.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-raw-boundary.h"
#include "rtl-rvtt-dst-autoincr-int.h"

namespace rvtt_autoincr {


/* The current target's auto-increment capability entry: modifier
   encodings, owned slot registers, and the audited frontend distance
   and occupancy quantities (WH and BH only; QSR returns available ==
   false, refusing the whole pass).  */

autoincr_caps
target_autoincr_caps ()
{
  /* Named-member construction (the delivery-cost API): the audited
     frontend quantities come from the rvtt-cost.md define_constants
     they document (single-sourced at build time); the per-target
     modifier encodings and slot registers are the ISA facts the file
     comment above adjudicates.  QSR has no capability entry and
     refuses (every field zero).  */
  autoincr_caps caps = autoincr_caps ();
  if (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH)
    {
      caps.available = true;
      caps.nslots = 1;
      caps.min_config_distance = XTT_AUTOINCR_MIN_CONFIG_DISTANCE;
      caps.drained_frontend_window = XTT_MODWRITE_DRAINED_FRONTEND_WINDOW;
      caps.config_issue_slots = XTT_CONFIG_ISSUE_SLOTS;
      if (TARGET_XTT_TENSIX_BH)
	{
	  caps.noinc_mode = 7;
	  caps.scratch_mode = 6;
	  caps.slots[0].src_reg = 18;
	  caps.slots[0].dst_reg = 34;
	  caps.slots[0].bias_reg = 53;
	}
      else
	{
	  caps.noinc_mode = 3;
	  caps.scratch_mode = 2;
	  caps.slots[0].src_reg = 19;
	  caps.slots[0].dst_reg = 29;
	  caps.slots[0].bias_reg = 54;
	  caps.n_watch = 1;
	  caps.watch_reg = 2;
	}
    }
  return caps;
}


/* Typed Dst accesses whose address-modifier operand is an explicit RTL
   operand.  The operand indexes come from the machine description.  The
   dynamically-addressed alternatives synthesize their base opcode word at
   expand time, so their modifier operand cannot be retargeted; they remain
   classified accesses (their no-increment behavior is unchanged) but can
   never carry the implicit advance.  */

bool
classify_access (rtx_insn *insn, int code, access_info *acc)
{
  int mode_opno;
  int static_opno; /* mem_or_0 operand: const0 selects the static form.  */
  switch (code)
    {
    case CODE_FOR_rvtt_sfpstore_int:
      mode_opno = 6;
      static_opno = 0;
      break;
    case CODE_FOR_rvtt_sfpload_lv_int:
      mode_opno = 8;
      static_opno = 1;
      break;
    default:
      return false;
    }
  extract_insn (insn);
  rtx mode = recog_data.operand[mode_opno];
  if (!CONST_INT_P (mode))
    return false;
  acc->mode = UINTVAL (mode);
  acc->mode_opno = mode_opno;
  acc->retargetable = recog_data.operand[static_opno] == const0_rtx;
  return true;
}

/* Classify INSN into the autoincr_class vocabulary, filling *ACC for a
   typed Dst access.  Calls, opaque asm, unrecognized insns, scalar or
   Dst-accessing code with unprovable effects, and Tensix instructions
   without the replay-safe attribute contract all take the refusing
   AIC_FOREIGN default; raw .ttinsn words are admitted only when the
   audited decode proves the pure Dst/RWC counter class.  */

autoincr_class
classify_insn (rtx_insn *insn, access_info *acc)
{
  if (CALL_P (insn))
    return AIC_FOREIGN;
  if (asm_noperands (PATTERN (insn)) >= 0)
    {
      /* Raw `.ttinsn' constant words: the audited architectural decode
	 (rvtt-raw-boundary.cc) proves the pure Dst/RWC counter class --
	 the same class as the typed face advance below: no
	 modifier-slot or LREG effect, but RWC state changes, so it
	 separates rows and never absorbs an increment.  Every other
	 asm keeps the refusing default.  */
      xtt_rwc_effect_t rwc;
      if (rvtt_raw_pure_dst_rwc (insn, &rwc))
	return AIC_RWC_STEP;
      return AIC_FOREIGN;
    }
  if (JUMP_P (insn))
    /* Branches have no Dst-RWC or configuration effect.  They bound the
       block, so they can only trail a region.  */
    return contains_mem_rtx_p (PATTERN (insn)) ? AIC_FOREIGN : AIC_NEUTRAL;
  if (GET_CODE (insn) != INSN)
    return AIC_FOREIGN;

  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return AIC_NEUTRAL;

  int code = recog_memoized (insn);
  if (code < 0)
    return AIC_FOREIGN;

  if (get_attr_type (insn) != TYPE_TENSIX)
    /* Scalar work cannot touch Tensix state, but a memory access could be a
       synthesized instruction-buffer issue.  */
    return contains_mem_rtx_p (pattern) ? AIC_FOREIGN : AIC_NEUTRAL;

  if (code == CODE_FOR_rvtt_ttincrwc)
    return AIC_INCRWC;
  if (code == CODE_FOR_rvtt_ttdstface_wh_bh)
    /* Typed Dst/RWC face advance: advances RWC counters only; the
       address-modifier configuration slots are untouched by identity of
       the typed pattern.  (Raw `.ttinsn' words of the same architectural
       class are admitted above through the audited field decode.)  */
    return AIC_RWC_STEP;
  if (code == CODE_FOR_rvtt_ttreplay_int)
    return AIC_REPLAY;
  if (classify_access (insn, code, acc))
    return AIC_ACCESS;
  if (code == CODE_FOR_rvtt_sfploaddiscard_int
      || code == CODE_FOR_rvtt_sfploadmacro_int)
    /* Dst accesses whose modifier is baked into an opaque encoding.  */
    return AIC_FOREIGN;

  /* Machine-described replay-safe instructions have no hidden CC, Dst, RWC,
     template, or replay ownership effects (rvtt.md attribute contract), so
     the remaining LREG-only compute is neutral here.  Everything else keeps
     the refusing default.  */
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_SAFE)
    return AIC_NEUTRAL;
  return AIC_FOREIGN;
}

/* A typed TTINCRWC advancing only Dst by a constant stride.  */

bool
pure_dst_increment_p (rtx_insn *insn, HOST_WIDE_INT *stride)
{
  rtx pattern = PATTERN (insn);
  rtx cr = XVECEXP (pattern, 0, 0);
  rtx d = XVECEXP (pattern, 0, 1);
  rtx b = XVECEXP (pattern, 0, 2);
  rtx a = XVECEXP (pattern, 0, 3);
  if (!CONST_INT_P (cr) || !CONST_INT_P (d) || !CONST_INT_P (b)
      || !CONST_INT_P (a))
    return false;
  if (INTVAL (cr) != 0 || INTVAL (b) != 0 || INTVAL (a) != 0)
    return false;
  *stride = INTVAL (d);
  /* The architectural field is four bits; zero advances nothing.  */
  return *stride > 0 && *stride <= 15;
}



/* Load-carrier word counting (riscv_tt_opt_dst_autoincr_load_carrier,
   the load-carrier extension): a canonical single-constant `.ttinsn' asm (the TTI_ macro
   shape the LLK library issues its raw boundary words in) is by
   construction exactly one 32-bit Tensix word in the issue stream, so
   it occupies exactly one replay slot and one frontend issue slot.
   This is a COUNTING fact only, taken from the audited extraction
   (rvtt_raw_ttinsn_word, rvtt-raw-boundary.cc); no classification
   happens here, and the words keep every refusing default elsewhere:
   classify_insn still answers AIC_FOREIGN (or the audited pure-RWC
   class), so raw words never become rewritable payload members,
   gap-legal items, or configuration-window-legal items.

   The prior behavior this knob replaces counted raw words as ZERO
   slots, so a replay recording whose shadow is raw words (every LLK
   datacopy envelope record) overran its block and refused the whole
   function ("replay capture crosses block") -- the adjudicated blocker
   of the load-carrier class (a microbenchmark probe: unit-stride
   `load dst_reg[0]; dst_reg += 1' walks emitted 32 raw TTINCRWC with
   zero capture while the very same rows fire in a record-free
   function).  Knob off preserves that behavior byte-identically.  */

static bool
raw_ttinsn_slot_word_p (rtx_insn *insn)
{
  if (!riscv_tt_opt_dst_autoincr_load_carrier)
    return false;
  uint32_t word;
  return rvtt_raw_ttinsn_word (insn, &word);
}

/* Non-empty Tensix instructions occupy replay slots; everything else is
   transparent to the recording shadow.  Under the load-carrier knob,
   audited raw `.ttinsn' constant words count too (see above).  */

bool
occupies_replay_slot_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return raw_ttinsn_slot_word_p (insn);
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  return get_attr_type (insn) == TYPE_TENSIX && get_attr_length (insn) != 0;
}

/* Slot-occupying Tensix words issued strictly between a replay row's lead
   position (the launch or executing capture) and the execution of the
   payload terminator: the launch word itself plus the payload prefix.  */

static unsigned
payload_consume_prefix (const capture_rec *cap)
{
  unsigned words = 1;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    {
      if (cap->members[ix] == cap->terminator)
	break;
      if (occupies_replay_slot_p (cap->members[ix]))
	++words;
    }
  return words;
}

/* Vet a capture's payload for carrying the implicit advance: every member
   must be neutral or a statically-encoded no-increment access, and the last
   access is the terminator.  */

static void
vet_payload (capture_rec *cap, const autoincr_caps &caps)
{
  cap->payload_ok = false;
  if (!cap->valid)
    return;
  int last_access = -1;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    switch (cap->member_cls[ix])
      {
      case AIC_NEUTRAL:
	break;
      case AIC_ACCESS:
	if (cap->member_acc[ix].mode != caps.noinc_mode)
	  return;
	last_access = ix;
	break;
      default:
	return;
      }
  if (last_access < 0)
    return;
  if (!cap->member_acc[last_access].retargetable)
    return;
  cap->payload_ok = true;
  cap->terminator = cap->members[last_access];
  cap->terminator_acc = cap->member_acc[last_access];
}

/* Linearize one block: classify instructions, fold capture shadows, resolve
   launches, and record candidate rows.  */

void
scan_block (function_scan &fn, basic_block bb, const autoincr_caps &caps)
{
  fn.blocks.emplace_back ();
  bb_scan &scan = fn.blocks.back ();
  scan.bb = bb;

  rtx_insn *insn = BB_HEAD (bb);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (; insn != end; insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      access_info acc;
      autoincr_class cls = classify_insn (insn, &acc);

      if (cls == AIC_REPLAY)
	{
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx begin = XVECEXP (pattern, 0, 5);
	  rtx exec = XVECEXP (pattern, 0, 6);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (begin)
	      || !CONST_INT_P (exec) || !CONST_INT_P (load))
	    {
	      /* A variable capture makes buffer contents unprovable.  */
	      fn.bail = true;
	      fn.bail_reason = "variable replay capture";
	      return;
	    }
	  if (INTVAL (load) != 0)
	    {
	      /* Capture: fold the recording shadow.  */
	      capture_rec *cap = new capture_rec;
	      cap->insn = insn;
	      cap->bb = bb;
	      cap->begin = UINTVAL (begin);
	      cap->len = UINTVAL (len);
	      cap->exec = INTVAL (exec) != 0;
	      if (!cap->exec)
		fn.noexec_captures.push_back (cap);
	      unsigned remaining = cap->len;
	      unsigned raw_words = 0;
	      while (remaining)
		{
		  insn = NEXT_INSN (insn);
		  if (!insn || BLOCK_FOR_INSN (insn) != bb)
		    {
		      cap->valid = false;
		      fn.bail = true;
		      fn.bail_reason = "replay capture crosses block";
		      fn.captures.push_back (cap);
		      return;
		    }
		  if (!NONDEBUG_INSN_P (insn))
		    continue;
		  access_info macc;
		  autoincr_class mcls = classify_insn (insn, &macc);
		  cap->members.push_back (insn);
		  cap->member_cls.push_back (mcls);
		  cap->member_acc.push_back (macc);
		  if (raw_ttinsn_slot_word_p (insn))
		    ++raw_words;
		  if (occupies_replay_slot_p (insn))
		    --remaining;
		}
	      if (raw_words && dump_file)
		fprintf (dump_file, "Dst-autoincr: raw-word capture shadow "
			 "counted (%u raw words of %u, bb %d; load-carrier)\n",
			 raw_words, cap->len, bb->index);
	      vet_payload (cap, caps);
	      fn.captures.push_back (cap);
	      bb_item item;
	      item.insn = cap->insn;
	      item.cls = AIC_REPLAY;
	      item.cap = cap;
	      scan.items.push_back (item);
	      continue;
	    }
	  launch_rec *launch = new launch_rec;
	  launch->insn = insn;
	  launch->begin = UINTVAL (begin);
	  launch->len = UINTVAL (len);
	  launch->payload = nullptr;
	  fn.launches.push_back (launch);
	  bb_item item;
	  item.insn = insn;
	  item.cls = AIC_REPLAY;
	  item.launch = launch;
	  scan.items.push_back (item);
	  continue;
	}

      bb_item item;
      item.insn = insn;
      item.cls = cls;
      item.acc = acc;
      scan.items.push_back (item);
    }
}

/* Resolve every launch to the unique capture recording its exact span, and
   invalidate payloads with overlapping-but-different uses.  */

void
resolve_replay (function_scan &fn)
{
  for (launch_rec *launch : fn.launches)
    {
      capture_rec *found = nullptr;
      for (capture_rec *cap : fn.captures)
	if (cap->begin == launch->begin && cap->len == launch->len)
	  {
	    if (found)
	      {
		found = nullptr; /* ambiguous */
		break;
	      }
	    found = cap;
	  }
      launch->payload = found;
    }

  /* Overlapping spans make buffer contents unprovable for both parties.  */
  auto overlap = [] (unsigned b0, unsigned l0, unsigned b1, unsigned l1)
  { return b0 < b1 + l1 && b1 < b0 + l0; };
  for (capture_rec *cap : fn.captures)
    {
      for (capture_rec *other : fn.captures)
	if (other != cap
	    && overlap (cap->begin, cap->len, other->begin, other->len))
	  cap->payload_ok = false;
      for (launch_rec *launch : fn.launches)
	if (overlap (cap->begin, cap->len, launch->begin, launch->len)
	    && !(launch->begin == cap->begin && launch->len == cap->len))
	  cap->payload_ok = false;
    }

  for (capture_rec *cap : fn.captures)
    {
      cap->exec_sites = cap->exec ? 1 : 0;
      for (launch_rec *launch : fn.launches)
	if (launch->payload == cap)
	  ++cap->exec_sites;
    }
}

/* Find candidate rows in one linearized block.  */

void
find_candidates (bb_scan &scan, const autoincr_caps &caps)
{
  for (unsigned ix = 0; ix != scan.items.size (); ++ix)
    {
      bb_item &item = scan.items[ix];
      if (item.cls != AIC_INCRWC)
	continue;
      HOST_WIDE_INT stride;
      if (!pure_dst_increment_p (item.insn, &stride))
	continue;

      /* The architecturally preceding execution.  Neutral instructions
	 cannot consume Dst; skip them.  */
      int jx = ix;
      while (--jx >= 0 && scan.items[jx].cls == AIC_NEUTRAL)
	continue;
      if (jx < 0)
	continue;
      bb_item &lead = scan.items[jx];

      candidate cand;
      cand.lead_item = jx;
      cand.incr_item = ix;
      cand.increment = item.insn;
      cand.stride = stride;
      cand.payload = nullptr;

      if (lead.cls == AIC_ACCESS)
	{
	  if (lead.acc.mode != caps.noinc_mode || !lead.acc.retargetable)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_EXPLICIT;
	  cand.terminator = lead.insn;
	  cand.terminator_acc = lead.acc;
	}
      else if (lead.cls == AIC_REPLAY && lead.launch)
	{
	  capture_rec *cap = lead.launch->payload;
	  if (!cap || !cap->payload_ok)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_LAUNCH;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	  cand.consume_prefix = payload_consume_prefix (cap);
	}
      else if (lead.cls == AIC_REPLAY && lead.cap)
	{
	  capture_rec *cap = lead.cap;
	  if (!cap->exec || !cap->payload_ok)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: not a candidate row "
			 "(no owned terminator access before increment, "
			 "bb %d)\n", scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_CAPTURE_EXEC;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	  cand.consume_prefix = payload_consume_prefix (cap);
	}
      else
	{
	  if (dump_file)
	    fprintf (dump_file, "Dst-autoincr: not a candidate row "
		     "(no owned terminator access before increment, "
		     "bb %d)\n", scan.bb->index);
	  continue;
	}

      scan.candidates.push_back (cand);
    }
}

/* Can GAP items sit between the configuration point and a later row without
   breaking ownership or RWC-state equivalence?  Neutral instructions and
   untransformed accesses through modifiers other than the scratch modifier
   qualify: their behavior is bitwise identical in both worlds and they
   cannot write modifier slots.  */

bool
gap_item_ok (const bb_item &item, const autoincr_caps &caps)
{
  switch (item.cls)
    {
    case AIC_NEUTRAL:
      return true;
    case AIC_ACCESS:
      return item.acc.mode != caps.scratch_mode;
    default:
      return false;
    }
}

/* Configuration-window legality: may ITEM sit between an already-programmed
   slot configuration and a later consuming row without invalidating the
   program?  This is weaker than gap legality: the item only needs to be
   provably unable to write the scratch slot's configuration registers or
   consume the scratch modifier.  A typed TTINCRWC advances RWC counters,
   not address-modifier configuration; replay recordings and launches are
   legal when their payload contents are known and themselves legal.  Any
   call, opaque asm, or unclassified Tensix effect (including any foreign
   TTSETC16) keeps the refusing default.  */

static bool
capture_members_config_ok (const capture_rec *cap, const autoincr_caps &caps)
{
  if (!cap->valid)
    return false;
  for (unsigned ix = 0; ix != cap->members.size (); ++ix)
    switch (cap->member_cls[ix])
      {
      case AIC_NEUTRAL:
      case AIC_INCRWC:
	break;
      case AIC_ACCESS:
	if (cap->member_acc[ix].mode == caps.scratch_mode)
	  return false;
	break;
      default:
	return false;
      }
  return true;
}

/* Configuration-window legality of one scanned ITEM (see the comment
   above capture_members_config_ok): neutral items, RWC counter steps,
   and accesses through modifiers other than CAPS's scratch modifier
   qualify; replay recordings and launches qualify when their payload is
   known and its members do.  Everything else refuses.  */

bool
config_window_item_ok (const bb_item &item, const autoincr_caps &caps)
{
  switch (item.cls)
    {
    case AIC_NEUTRAL:
    case AIC_INCRWC:
    case AIC_RWC_STEP:
      /* RWC counter steps (per-row increments, the typed face advance)
	 cannot write address-modifier configuration.  They do change RWC
	 state, so they are only window-legal, never gap-legal.  */
      return true;
    case AIC_ACCESS:
      return item.acc.mode != caps.scratch_mode;
    case AIC_REPLAY:
      if (item.cap)
	/* A recording only executes its members when it is an executing
	   capture.  */
	return !item.cap->exec || capture_members_config_ok (item.cap, caps);
      if (item.launch)
	return item.launch->payload
	       && capture_members_config_ok (item.launch->payload, caps);
      return false;
    default:
      return false;
    }
}

/* Slot-occupying Tensix words issued by ITEM (recordings issue their
   members; a launch conservatively counts only its own word).  */

unsigned
item_issue_words (const bb_item &item)
{
  unsigned words = occupies_replay_slot_p (item.insn) ? 1 : 0;
  if (item.cap)
    for (rtx_insn *member : item.cap->members)
      if (occupies_replay_slot_p (member))
	++words;
  return words;
}

/* An audited issue-time RWC writer (explicit TTINCRWC, typed face
   advance) standing between CAND's terminator and the end of the block
   re-anchors the backedge crossing: it is the last RWC writer the
   backedge sees and its own producer adjacency is in-stream (continuous
   words, hand-witnessed).  */

bool
crossing_reanchored_p (const bb_scan &scan, const candidate &cand)
{
  for (unsigned ix = cand.incr_item + 1; ix != scan.items.size (); ++ix)
    {
      const bb_item &item = scan.items[ix];
      if (item.cls == AIC_INCRWC || item.cls == AIC_RWC_STEP)
	return true;
    }
  return false;
}

/* Frontend issue-slot words of the scalar INSN, zero for Tensix
   instructions (those are counted by the audited slot-word side) and for
   anything unrecognized (undercounting the covering distance only widens
   the charge -- conservative).  Scalar words occupy the same frontend
   issue slots that elapse while a mod-write retires, so they cover
   crossing distance exactly like Tensix words do (the covered-crossing fit:
   the five-witness fit is over whole-iteration slot counts with scalar
   included).  */

static unsigned
scalar_issue_words (rtx_insn *insn)
{
  if (JUMP_P (insn))
    /* One word: the pass runs before branch shortening, where jump
       lengths are worst-case layout maxima (far-branch expansions), not
       issue counts.  The floor stays conservative -- undercounting the
       covering distance only widens the charge -- and matches the
       witnesses' loop control (one compare-and-branch word).  */
    return 1;
  if (GET_CODE (insn) != INSN)
    return 0;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return 0;
  if (recog_memoized (insn) < 0)
    return 0;
  if (get_attr_type (insn) == TYPE_TENSIX)
    return 0;
  return get_attr_length (insn) / 4;
}

/* Frontend issue-slot cover of one raw INSN: the ONE cover spelling
   (the delivery-cost API) behind both the per-item view below and the
   raw-insn block walks: slot words occupy frontend issue
   slots, and scalar words cover crossing distance exactly like Tensix
   words do (the covered-crossing fit).  */

unsigned
insn_frontend_cover_words (rtx_insn *insn)
{
  return (occupies_replay_slot_p (insn) ? 1 : 0) + scalar_issue_words (insn);
}

/* Frontend issue-slot words of ITEM: the Tensix slot words (recordings
   issue their members; a launch keeps the audited one-word conservative
   floor of the measured 1.3-1.8-cycle launch boundary) plus the scalar
   words of the item and of any recording's scalar members -- the item
   sum of insn_frontend_cover_words over the item's own insn and any
   recording's members, so the per-item and per-raw-insn views price
   the same cover by construction, not by comment.  */

unsigned
item_frontend_words (const bb_item &item)
{
  unsigned words = insn_frontend_cover_words (item.insn);
  if (item.cap)
    for (rtx_insn *member : item.cap->members)
      words += insn_frontend_cover_words (member);
  return words;
}

} /* namespace rvtt_autoincr */
