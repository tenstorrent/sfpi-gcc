/* Pass to prove Dst auto-increment ownership and absorb per-row TTINCRWC.
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

#define INCLUDE_ALGORITHM
#define INCLUDE_MAP
#define INCLUDE_VECTOR
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

/* Semantic SFPI code performs every logical Dst access through the target
   no-increment address modifier and advances the Dst RWC with an explicit
   typed TTINCRWC after each row.  When a straight-line unrolled row sequence
   or a counted loop advances Dst by a constant stride between rows this way,
   the hardware can instead perform the advance implicitly: an address
   modifier slot programmed with a Dst increment makes the row's final Dst
   access post-increment the RWC, and the explicit per-row TTINCRWC becomes
   dead.

   This pass performs that replacement generically:

     - Rows are discovered from typed-insn dataflow only: a typed TTINCRWC
       with a pure constant Dst stride, whose architecturally preceding Dst
       access is a typed, statically-encoded access through the no-increment
       modifier.  The access may be explicit, or it may be the final access
       of a replay payload executed by a preceding typed TTREPLAY launch or
       an executing capture.  No operation names, coefficient fingerprints,
       raw instruction words, or fixed calendars participate in any decision.

     - Ownership of the Dst address-modifier configuration is proven on every
       path between the configuration point and the last transformed row:
       any call, opaque asm, or unclassified Tensix instruction in that
       region refuses.  For loop-shaped regions the configuration is placed
       in the dedicated preheader and the whole loop body must be proven
       clean, because every iteration is a path from the configuration to a
       row.

     - The modifier slot written is the target's compiler-owned scratch slot
       (see the capability table below).  Its three configuration registers
       are fully programmed (Src, Dst+fidelity, bias) so arbitrary incoming
       state and reset state are equivalent.  The no-increment slot that
       SFPI's programming model contract guarantees is never written, so all
       untransformed accesses keep their architectural no-op behavior.

     - The Dst RWC value is preserved exactly at every program point outside
       the half-open windows (terminator access, removed TTINCRWC]: each
       removed explicit increment is replaced by exactly one implicit
       increment of the same stride at the terminator access, and nothing
       between the two points consumes Dst.  Replay payloads are only
       rewritten when EVERY execution site of the payload is a transformed
       row; otherwise the RWC state at the uncovered site would be
       unrestorably changed and the pass refuses.

     - Profitability compares the configuration cost (SETC16 words from the
       capability table) against the number of dynamically removed per-row
       increments.  Loop regions use the same trip-count estimate as replay
       hoisting.  No numeric row thresholds appear anywhere; break-evens fall
       out of the model.

   All refusals leave the function byte-identical.  */

namespace {

/* Target capability table for Dst auto-increment ownership.

   Register addresses are the architectural SETC16 configuration addresses of
   the address-modifier sections, from the per-target configuration space
   (cfg_defines.h): ADDR_MOD_AB_SEC<k>_SrcAIncr_ADDR32,
   ADDR_MOD_DST_SEC<k>_DestIncr_ADDR32 (fidelity shares this register) and
   ADDR_MOD_BIAS_SEC<k>_BiasIncr_ADDR32.

   Blackhole: SFPLOAD/SFPSTORE carry a three-bit modifier field selecting
   physical slot k directly; SrcA/B 12+k, Dst 28+k, bias 47+k.  The SFPI
   contract reserves slot 7 as the architectural no-op modifier and the
   platform reserves slot 6 as the compiler-owned auto-increment scratch slot
   (the same slot the macro formation contract owns).

   Wormhole: the modifier field is two bits wide and the active bank base
   selects physical slot m or m+4.  Base ownership is not provable here, so
   both physical slots of the scratch modifier are programmed (the dual-slot
   rule as data): SrcA/B 7+2k, Dst 23+k, bias 48+k for k in {2, 6}.  The SFPI
   no-op modifier is 3.

   QSR has no capability entry and therefore refuses.  */

struct autoincr_slot
{
  unsigned src_reg;
  unsigned dst_reg;   /* Dst increment; fidelity shares the register.  */
  unsigned bias_reg;
};

struct autoincr_caps
{
  bool available;
  unsigned noinc_mode;   /* SFPI no-increment modifier value.  */
  unsigned scratch_mode; /* compiler-owned modifier value to retarget to.  */
  unsigned nslots;       /* physical slots behind scratch_mode.  */
  autoincr_slot slots[2];
};

static autoincr_caps
target_autoincr_caps ()
{
  if (TARGET_XTT_TENSIX_BH)
    return { true, 7, 6, 1, { { 18, 34, 53 }, { 0, 0, 0 } } };
  if (TARGET_XTT_TENSIX_WH)
    return { true, 3, 2, 2, { { 11, 25, 50 }, { 19, 29, 54 } } };
  return { false, 0, 0, 0, { { 0, 0, 0 }, { 0, 0, 0 } } };
}

/* Classification of one instruction by architectural effect, derived from
   typed instruction identity and machine attributes only.  */

enum autoincr_class
{
  AIC_NEUTRAL,  /* provably no Dst-RWC or modifier-slot effect */
  AIC_ACCESS,   /* typed Dst access through an address modifier */
  AIC_INCRWC,   /* typed TTINCRWC */
  AIC_REPLAY,   /* typed TTREPLAY capture or launch */
  AIC_FOREIGN,  /* call, opaque asm, or unclassified effect: refuses */
};

struct access_info
{
  unsigned mode = 0;        /* constant address-modifier operand */
  int mode_opno = -1;
  bool retargetable = false; /* statically encoded: operand is authoritative */
};

/* Typed Dst accesses whose address-modifier operand is an explicit RTL
   operand.  The operand indexes come from the machine description.  The
   dynamically-addressed alternatives synthesize their base opcode word at
   expand time, so their modifier operand cannot be retargeted; they remain
   classified accesses (their no-increment behavior is unchanged) but can
   never carry the implicit advance.  */

static bool
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

static autoincr_class
classify_insn (rtx_insn *insn, access_info *acc)
{
  if (CALL_P (insn))
    return AIC_FOREIGN;
  if (asm_noperands (PATTERN (insn)) >= 0)
    return AIC_FOREIGN;
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

static bool
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

/* Replay bookkeeping.  Captures record the instructions in their shadow;
   launches execute the payload recorded for their exact buffer span.  */

struct capture_rec
{
  rtx_insn *insn = nullptr;
  basic_block bb = nullptr;
  unsigned begin = 0;
  unsigned len = 0;
  bool exec = false;   /* capture also executes the payload */
  bool valid = true;

  std::vector<rtx_insn *> members;
  std::vector<autoincr_class> member_cls;
  std::vector<access_info> member_acc;

  /* Derived payload facts.  */
  bool payload_ok = false;
  rtx_insn *terminator = nullptr;
  access_info terminator_acc;

  unsigned exec_sites = 0;    /* executions: launches + executing captures */
  unsigned covered_sites = 0; /* executions that are candidate rows */
};

struct launch_rec
{
  rtx_insn *insn;
  unsigned begin;
  unsigned len;
  capture_rec *payload; /* resolved capture, or null */
};

/* One linearized block element.  Payload members of a capture are folded
   into the capture's record and do not appear as items.  */

struct bb_item
{
  rtx_insn *insn;
  autoincr_class cls;
  access_info acc;
  capture_rec *cap = nullptr;   /* AIC_REPLAY capture */
  launch_rec *launch = nullptr; /* AIC_REPLAY launch */
};

enum row_kind { ROW_EXPLICIT, ROW_LAUNCH, ROW_CAPTURE_EXEC };

struct candidate
{
  row_kind kind;
  unsigned lead_item;      /* item index where the row's execution starts */
  unsigned incr_item;      /* item index of the typed TTINCRWC */
  rtx_insn *increment;     /* the TTINCRWC insn */
  HOST_WIDE_INT stride;
  rtx_insn *terminator;    /* access insn carrying the implicit advance */
  access_info terminator_acc;
  capture_rec *payload;    /* non-null for ROW_LAUNCH / ROW_CAPTURE_EXEC */
  bool dropped = false;
};

struct bb_scan
{
  basic_block bb;
  std::vector<bb_item> items;
  std::vector<candidate> candidates;
};

struct function_scan
{
  std::vector<capture_rec *> captures;
  std::vector<launch_rec *> launches;
  std::vector<bb_scan> blocks;
  bool bail = false;
  const char *bail_reason = nullptr;
};

/* Non-empty Tensix instructions occupy replay slots; everything else is
   transparent to the recording shadow.  */

static bool
occupies_replay_slot_p (rtx_insn *insn)
{
  if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0)
    return false;
  rtx pattern = PATTERN (insn);
  if (GET_CODE (pattern) == USE || GET_CODE (pattern) == CLOBBER)
    return false;
  return get_attr_type (insn) == TYPE_TENSIX && get_attr_length (insn) != 0;
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

static void
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
	      unsigned remaining = cap->len;
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
		  if (occupies_replay_slot_p (insn))
		    --remaining;
		}
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

static void
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

static void
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
		fprintf (dump_file, "Dst-autoincr refusal: no owned "
			 "terminator access before increment (bb %d)\n",
			 scan.bb->index);
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
		fprintf (dump_file, "Dst-autoincr refusal: no owned "
			 "terminator access before increment (bb %d)\n",
			 scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_LAUNCH;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	}
      else if (lead.cls == AIC_REPLAY && lead.cap)
	{
	  capture_rec *cap = lead.cap;
	  if (!cap->exec || !cap->payload_ok)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr refusal: no owned "
			 "terminator access before increment (bb %d)\n",
			 scan.bb->index);
	      continue;
	    }
	  cand.kind = ROW_CAPTURE_EXEC;
	  cand.payload = cap;
	  cand.terminator = cap->terminator;
	  cand.terminator_acc = cap->terminator_acc;
	}
      else
	{
	  if (dump_file)
	    fprintf (dump_file, "Dst-autoincr refusal: no owned terminator "
		     "access before increment (bb %d)\n", scan.bb->index);
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

static bool
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

struct group
{
  bb_scan *scan;
  std::vector<unsigned> cand_ix; /* indexes into scan->candidates */
  HOST_WIDE_INT stride;
  bool use_preheader = false;
  basic_block preheader = nullptr;
  HOST_WIDE_INT dynamic_rows = 0; /* estimated removed increments */
};

/* Mirror of the replay pass's dedicated preheader discovery.  */

static basic_block
dedicated_loop_preheader (class loop *loop)
{
  basic_block preheader = nullptr;
  edge entry = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, loop->header->preds)
    if (!flow_bb_inside_loop_p (loop, e->src))
      {
	if (preheader)
	  return nullptr;
	preheader = e->src;
	entry = e;
      }

  return preheader && !(entry->flags & EDGE_ABNORMAL)
	 && single_succ_p (preheader)
    ? preheader : nullptr;
}

/* Try to prove the loop-shaped placement for GRP: single-block loop body,
   dedicated preheader, and a wholly-owned body (every iteration is a path
   from the preheader configuration to a row terminator).  */

static void
try_loop_placement (group &grp, const autoincr_caps &caps)
{
  basic_block bb = grp.scan->bb;
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0 || loop->header != bb || loop->latch != bb
      || loop->num_nodes != 1)
    return;
  basic_block preheader = dedicated_loop_preheader (loop);
  if (!preheader)
    return;

  /* Every item of the body must be owned: group rows, their increments,
     and gap-legal instructions.  */
  std::vector<bool> is_group_item (grp.scan->items.size (), false);
  for (unsigned cx : grp.cand_ix)
    {
      const candidate &cand = grp.scan->candidates[cx];
      is_group_item[cand.lead_item] = true;
      is_group_item[cand.incr_item] = true;
    }
  for (unsigned ix = 0; ix != grp.scan->items.size (); ++ix)
    if (!is_group_item[ix] && !gap_item_ok (grp.scan->items[ix], caps))
      return;

  /* Payload captures must not sit in an unowned location: the recording may
     be anywhere outside the loop (it does not execute the payload unless it
     is itself a row), but a capture inside the body is only owned if it is
     a group row.  */
  for (unsigned ix = 0; ix != grp.scan->items.size (); ++ix)
    if (grp.scan->items[ix].cap && !is_group_item[ix])
      return;

  gcov_type iterations = expected_loop_iterations_unbounded (loop) + 1;
  if (iterations < 2)
    {
      if (dump_file)
	fprintf (dump_file, "Dst-autoincr refusal: unknown trip count for "
		 "loop group (bb %d)\n", bb->index);
      return;
    }

  grp.use_preheader = true;
  grp.preheader = preheader;
  grp.dynamic_rows = (HOST_WIDE_INT) iterations * grp.cand_ix.size ();
}

/* Emit the owned scratch-slot programming: every consumed field of every
   physical slot behind the scratch modifier, so reset state and arbitrary
   incoming state are equivalent.  */

static void
emit_owned_config (const autoincr_caps &caps, HOST_WIDE_INT stride,
		   rtx_insn *before, rtx_insn *after)
{
  start_sequence ();
  for (unsigned sx = 0; sx != caps.nslots; ++sx)
    {
      const autoincr_slot &slot = caps.slots[sx];
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.src_reg),
					const0_rtx));
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.dst_reg),
					GEN_INT (stride)));
      emit_insn (gen_rvtt_ttsetc16_int (GEN_INT (slot.bias_reg),
					const0_rtx));
    }
  rtx_insn *seq = get_insns ();
  end_sequence ();
  if (before)
    emit_insn_before (seq, before);
  else
    emit_insn_after (seq, after);
}

static void
transform_group (const group &grp, const autoincr_caps &caps)
{
  bb_scan &scan = *grp.scan;

  /* Configuration placement.  */
  if (grp.use_preheader)
    {
      rtx_insn *end = BB_END (grp.preheader);
      if (end && JUMP_P (end))
	emit_owned_config (caps, grp.stride, end, nullptr);
      else
	emit_owned_config (caps, grp.stride, nullptr, end);
    }
  else
    {
      const candidate &first = scan.candidates[grp.cand_ix.front ()];
      emit_owned_config (caps, grp.stride, scan.items[first.lead_item].insn,
			 nullptr);
    }

  /* Retarget each terminator once and delete the explicit increments.  */
  for (unsigned cx : grp.cand_ix)
    {
      candidate &cand = scan.candidates[cx];
      extract_insn (cand.terminator);
      rtx *loc = recog_data.operand_loc[cand.terminator_acc.mode_opno];
      if (UINTVAL (*loc) != caps.scratch_mode)
	{
	  bool ok = validate_change (cand.terminator, loc,
				     GEN_INT (caps.scratch_mode), false);
	  gcc_assert (ok);
	}
      delete_insn (cand.increment);
    }

  if (dump_file)
    fprintf (dump_file,
	     "Dst-autoincr group: bb %d rows %u stride "
	     HOST_WIDE_INT_PRINT_DEC " config %u words%s\n",
	     scan.bb->index, unsigned (grp.cand_ix.size ()),
	     grp.stride, caps.nslots * 3,
	     grp.use_preheader ? " (preheader)" : "");
}

static void
transform (function *cfn)
{
  autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available)
    return;

  function_scan fn;
  basic_block bb;
  FOR_EACH_BB_FN (bb, cfn)
    {
      scan_block (fn, bb, caps);
      if (fn.bail)
	break;
    }

  bool preexisting_scratch = false;
  if (!fn.bail)
    {
      /* A pre-existing access through the scratch modifier means foreign
	 code observes the slot: refuse the whole function.  */
      auto check_scratch = [&] (const access_info &acc, autoincr_class cls)
      {
	if (cls == AIC_ACCESS && acc.mode == caps.scratch_mode)
	  preexisting_scratch = true;
      };
      for (bb_scan &scan : fn.blocks)
	for (bb_item &item : scan.items)
	  check_scratch (item.acc, item.cls);
      for (capture_rec *cap : fn.captures)
	for (unsigned ix = 0; ix != cap->members.size (); ++ix)
	  check_scratch (cap->member_acc[ix], cap->member_cls[ix]);
      if (preexisting_scratch && dump_file)
	fprintf (dump_file, "Dst-autoincr refusal: pre-existing access "
		 "through the scratch modifier\n");
    }

  if (!fn.bail && !preexisting_scratch)
    {
      resolve_replay (fn);
      for (bb_scan &scan : fn.blocks)
	find_candidates (scan, caps);

      /* Iterate group formation, profitability, and payload-coverage
	 filtering to a fixed point: dropping a payload's candidates can
	 split neighboring groups.  */
      std::vector<group> groups;
      bool changed = true;
      while (changed)
	{
	  changed = false;
	  groups.clear ();

	  for (bb_scan &scan : fn.blocks)
	    {
	      group current;
	      current.scan = &scan;
	      auto close_group = [&] ()
	      {
		if (current.cand_ix.empty ())
		  return;
		groups.push_back (current);
		current.cand_ix.clear ();
	      };

	      unsigned prev_incr_item = 0;
	      for (unsigned cx = 0; cx != scan.candidates.size (); ++cx)
		{
		  candidate &cand = scan.candidates[cx];
		  if (cand.dropped)
		    {
		      close_group ();
		      continue;
		    }
		  if (!current.cand_ix.empty ())
		    {
		      bool clean = true;
		      for (unsigned ix = prev_incr_item + 1;
			   ix < cand.lead_item; ++ix)
			if (!gap_item_ok (scan.items[ix], caps))
			  {
			    clean = false;
			    if (dump_file
				&& scan.items[ix].cls == AIC_FOREIGN)
			      fprintf (dump_file, "Dst-autoincr refusal: "
				       "foreign effect breaks ownership "
				       "(bb %d)\n", scan.bb->index);
			    break;
			  }
		      if (clean && cand.stride != current.stride)
			{
			  clean = false;
			  if (dump_file)
			    fprintf (dump_file, "Dst-autoincr refusal: "
				     "stride mismatch between rows "
				     "(bb %d)\n", scan.bb->index);
			}
		      if (!clean)
			close_group ();
		    }
		  if (current.cand_ix.empty ())
		    current.stride = cand.stride;
		  current.cand_ix.push_back (cx);
		  prev_incr_item = cand.incr_item;
		}
	      close_group ();
	    }

	  /* Profitability: configuration cost against dynamically removed
	     increments.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      grp.use_preheader = false;
	      grp.dynamic_rows = grp.cand_ix.size ();
	      try_loop_placement (grp, caps);
	      HOST_WIDE_INT cost = (HOST_WIDE_INT) caps.nslots * 3;
	      if (grp.dynamic_rows <= cost)
		{
		  if (dump_file)
		    fprintf (dump_file,
			     "Dst-autoincr refusal: unprofitable group "
			     "(config " HOST_WIDE_INT_PRINT_DEC
			     " >= removed " HOST_WIDE_INT_PRINT_DEC
			     ", bb %d)\n",
			     cost, grp.dynamic_rows,
			     grp.scan->bb->index);
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		++it;
	    }

	  /* Payload coverage: every execution site of a rewritten payload
	     must be a surviving row, or the uncovered site's RWC state
	     changes unrestorably.  */
	  for (capture_rec *cap : fn.captures)
	    cap->covered_sites = 0;
	  for (const group &grp : groups)
	    for (unsigned cx : grp.cand_ix)
	      if (capture_rec *cap = grp.scan->candidates[cx].payload)
		++cap->covered_sites;
	  for (capture_rec *cap : fn.captures)
	    if (cap->covered_sites != 0
		&& cap->covered_sites != cap->exec_sites)
	      {
		if (dump_file)
		  fprintf (dump_file, "Dst-autoincr refusal: payload "
			   "execution site without matching increment "
			   "(live-out RWC state)\n");
		for (bb_scan &scan : fn.blocks)
		  for (candidate &cand : scan.candidates)
		    if (cand.payload == cap && !cand.dropped)
		      {
			cand.dropped = true;
			changed = true;
		      }
	      }

	  if (changed)
	    continue;

	  for (const group &grp : groups)
	    transform_group (grp, caps);
	}
    }
  else if (fn.bail && dump_file)
    fprintf (dump_file, "Dst-autoincr refusal: %s\n", fn.bail_reason);

  for (capture_rec *cap : fn.captures)
    delete cap;
  for (launch_rec *launch : fn.launches)
    delete launch;
}

const pass_data pass_data_rvtt_dst_autoincr =
{
  RTL_PASS, /* type */
  "rvtt_dst_autoincr", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_dst_autoincr : public rtl_opt_pass
{
public:
  pass_rvtt_dst_autoincr (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_dst_autoincr, ctxt)
  {
  }

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_dst_autoincr > 0;
  }

  virtual unsigned execute (function *fn) override
  {
    loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
    transform (fn);
    loop_optimizer_finalize ();
    free_dominance_info (CDI_DOMINATORS);
    return 0;
  }
}; // class pass_rvtt_dst_autoincr

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_dst_autoincr (gcc::context *ctxt)
{
  return new pass_rvtt_dst_autoincr (ctxt);
}
