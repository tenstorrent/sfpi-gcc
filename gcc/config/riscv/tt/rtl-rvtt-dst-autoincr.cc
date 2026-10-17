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

/* Absorb per-row TTINCRWC into Dst auto-increment.

   SFPI code accesses Dst through the no-increment address modifier and
   advances the Dst RWC with an explicit TTINCRWC after each row.  When a
   straight-line row sequence or a counted loop advances Dst by a constant
   stride this way, an address modifier slot programmed with that Dst
   increment can make the row's final Dst access post-increment the RWC
   instead, and the explicit TTINCRWC is deleted, saving one word per row.

   - Rows are found from typed-insn effects only: a TTINCRWC with a pure
     constant Dst stride whose preceding Dst access is a statically
     encoded access through the no-increment modifier, either explicit or
     the last access of a replay payload.
   - The slot written is the target's compiler-owned scratch slot (see the
     capability table in rtl-rvtt-dst-autoincr-int.h).  All three of its
     configuration registers (Src, Dst+fidelity, bias) are programmed with
     SETC16, so its incoming state does not matter; the no-increment slot
     is never written.
   - Ownership: no call, opaque asm or unclassified Tensix instruction may
     lie on any path between the slot program and the last transformed
     row.  For a loop, the program goes in the preheader and the whole
     loop body must be clean.
   - The RWC value is unchanged outside each window from the terminator
     access to the removed TTINCRWC, and nothing in between consumes Dst.
     A replay payload is only rewritten if every execution site of it is
     a transformed row.
   - Profitability compares the SETC16 words, plus the drain residual
     min_config_distance, against the removed increments; a program
     outside a preheader re-executes with each region entry and pays the
     two-cycle configuration issue class (rvtt_issue_cfg) per word.
   - The post-increment retires inside the vector unit some time after
     issue.  When the next consumer is reached across a loop backedge, the
     frontend has drained and the consumer can issue inside that window,
     so each crossing is charged the part of drained_frontend_window not
     covered by the iteration's own words (see rvtt-cost.md).
   - A replay capture recorded without execution must not start while a
     mod-write may still be retiring (noexec_record_composition_p).

   gcc/auto-inc-dec.cc does a similar merge, but needs a MEM whose address
   register is incremented; a Dst access is an UNSPEC_VOLATILE with no
   address register, and the modifier is a separately programmed
   configuration slot.  The capability table, insn classification and
   replay bookkeeping are in rtl-rvtt-dst-autoincr-scan.cc.  Every refusal
   leaves the function unchanged.  */

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
#include "rtl-rvtt-dst-autoincr-int.h"

using namespace rvtt_autoincr;
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-macro-ownership.h"
#include "rvtt-raw-boundary.h"

namespace {

struct group
{
  bb_scan *scan;
  std::vector<unsigned> cand_ix; /* indexes into scan->candidates */
  HOST_WIDE_INT stride;
  bool use_preheader = false;
  basic_block preheader = nullptr;
  HOST_WIDE_INT dynamic_rows = 0; /* estimated removed increments */
  /* Straight-line placement: item index the slot program is emitted
     before.  Defaults to the first row's lead and may be anchored earlier
     to satisfy the distance guard.  */
  unsigned anchor_item = 0;
  /* False when a dominating same-program group's configuration reaches
     this group on every path, so this group emits nothing.  */
  bool emit_config = true;
  /* Straight-line shared-placement set this group belongs to, or -1.
     Profitability is evaluated per set: one program serves all rows.  */
  int shared_set = -1;
  /* Set when no placement satisfying the distance guard exists.  */
  bool guard_refused = false;
  /* Backedge-crossing charge, in issue slots per execution of the
     block.  */
  unsigned crossing_charge = 0;
  /* Set when the group's final terminator carries a mod-write across a
     loop backedge, covered or not; the configuration cost then includes
     the drain residual once per loop entry.  */
  bool live_crossing = false;
  /* Set when the slot program is hoisted to the caller's loop entry by
     the cross-call ADDR_MOD contract, so the group emits nothing.  */
  bool contract = false;
};

/* Return the scan of BB in FN.  */

static bb_scan *
find_scan (function_scan &fn, basic_block bb)
{
  for (bb_scan &scan : fn.blocks)
    if (scan.bb == bb)
      return &scan;
  return nullptr;
}

/* Return the frontend issue-slot words of GRP's block after the
   transform, which bounds from below the distance between two backedge
   crossings: any path from the block back to itself issues at least the
   block's own words.  */

static unsigned
iteration_cover_words (const group &grp)
{
  unsigned words = 0;
  for (const bb_item &item : grp.scan->items)
    words += item_frontend_words (item);
  for (const candidate &cand : grp.scan->candidates)
    if (!cand.dropped && words)
      --words;
  return words;
}

/* Return the backedge-crossing charge for GRP in issue slots per
   execution of its block, using CAPS.  It is zero if the block is not in
   a loop, if GRP does not hold the block's last surviving candidate, if
   an issue-time RWC writer (a surviving TTINCRWC or a face advance)
   follows it, or if the block's own words cover drained_frontend_window.
   Set *LIVE_CROSSING whenever a crossing exists, covered or not.  */

static unsigned
crossing_penalty (const group &grp, const autoincr_caps &caps,
		  bool *live_crossing)
{
  *live_crossing = false;
  basic_block bb = grp.scan->bb;
  class loop *loop = bb->loop_father;
  if (!loop || loop->num == 0)
    return 0;

  /* The block's final surviving candidate carries the crossing.  */
  int last = -1;
  for (unsigned cx = 0; cx != grp.scan->candidates.size (); ++cx)
    if (!grp.scan->candidates[cx].dropped)
      last = cx;
  if (last < 0
      || std::find (grp.cand_ix.begin (), grp.cand_ix.end (),
		    (unsigned) last) == grp.cand_ix.end ())
    return 0;

  if (crossing_reanchored_p (*grp.scan, grp.scan->candidates[last]))
    return 0;

  *live_crossing = true;
  unsigned cover = iteration_cover_words (grp);
  if (cover >= caps.drained_frontend_window)
    {
      if (dump_file)
	fprintf (dump_file, "Dst-autoincr: mod-write backedge crossing "
		 "covered (rows %u, iteration slot words %u >= drain "
		 "window %u, bb %d)\n", unsigned (grp.cand_ix.size ()),
		 cover, caps.drained_frontend_window, bb->index);
      return 0;
    }
  return caps.drained_frontend_window - cover;
}

/* A replay capture recorded without execution (TTREPLAY load=1 exec=0)
   must not begin while a mod-write is still retiring; on hardware this
   hangs the Tensix core (see "No-exec record composition" in
   rvtt-cost.md).  For explicit rows, the mod-write store is itself a
   frontend word, so the frontend-word distance from the store to the
   capture bounds its retirement, and a capture at least
   drained_frontend_window words away is safe.  For a row delivered by
   replay, the launch issues one word while the payload's store is
   delivered later, so no distance is safe; and since the replay buffer
   persists across calls, a capture in a sibling arm may have been armed
   by a previous invocation.

   Return true if any surviving row of GRP is delivered through the
   replay expander (a launch or an executing capture).  */

static bool
group_replay_delivered_p (const group &grp)
{
  for (unsigned cx : grp.cand_ix)
    if (grp.scan->candidates[cx].payload)
      return true;
  return false;
}

/* Return true if TO is reachable from the successors of FROM (FROM
   itself only through a cycle).  */

static bool
block_reachable_p (basic_block from, basic_block to)
{
  hash_set<basic_block> seen;
  std::vector<basic_block> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, from->succs)
    work.push_back (e->dest);
  while (!work.empty ())
    {
      basic_block bb = work.back ();
      work.pop_back ();
      if (bb == to)
	return true;
      if (seen.add (bb))
	continue;
      FOR_EACH_EDGE (e, ei, bb->succs)
	work.push_back (e->dest);
    }
  return false;
}

/* Return the frontend issue-slot words of BB in FN's scan, or zero for
   an unscanned block (the conservative direction).  */

static unsigned
block_frontend_words (const function_scan &fn, basic_block bb)
{
  for (const bb_scan &scan : fn.blocks)
    if (scan.bb == bb)
      {
	unsigned words = 0;
	for (const bb_item &item : scan.items)
	  words += item_frontend_words (item);
	return words;
      }
  return 0;
}

/* Return the frontend issue-slot words in CAP's block before the capture
   itself, or zero for an unscanned block.  */

static unsigned
words_before_capture (const function_scan &fn, const capture_rec *cap)
{
  for (const bb_scan &scan : fn.blocks)
    if (scan.bb == cap->bb)
      {
	unsigned words = 0;
	for (const bb_item &item : scan.items)
	  {
	    if (item.insn == cap->insn)
	      return words;
	    words += item_frontend_words (item);
	  }
	return words;
      }
  return 0;
}

/* Return true, setting *HAZARD to the capture and *DETAIL to the reason,
   if a no-exec replay capture in FN could begin while GRP's final
   mod-write is still retiring.  Same-block captures always count; for a
   replay-delivered group so does any capture that is reachable from the
   group or does not dominate it; otherwise the minimum frontend-word
   distance from the group's block, pruned at CAPS's
   drained_frontend_window, decides.  */

static bool
noexec_record_composition_p (const function_scan &fn, const group &grp,
			     const autoincr_caps &caps,
			     const capture_rec **hazard,
			     const char **detail)
{
  *hazard = nullptr;
  *detail = "no-exec replay capture within the drained-frontend window "
	    "of the group's stores";
  if (fn.noexec_captures.empty ())
    return false;
  unsigned window = caps.drained_frontend_window;
  bool replay_delivered = group_replay_delivered_p (grp);

  for (capture_rec *cap : fn.noexec_captures)
    {
      /* Ordering within a block is not modelled.  */
      if (cap->bb == grp.scan->bb)
	{
	  *hazard = cap;
	  return true;
	}

      bool cap_dominates_group
	= dom_info_available_p (CDI_DOMINATORS)
	  && dominated_by_p (CDI_DOMINATORS, grp.scan->bb, cap->bb);

      /* For a replay-delivered group, the only acceptable capture is one
	 that dominates the group and is not reachable from it: it records
	 once before every launch, as a record hoisted to the preheader
	 does.  A reachable capture may re-record while the payload's store
	 is retiring, and a non-dominating one may have been armed by a
	 previous invocation.  */
      if (replay_delivered)
	{
	  bool reachable = block_reachable_p (grp.scan->bb, cap->bb);
	  if (reachable || !cap_dominates_group)
	    {
	      *hazard = cap;
	      *detail
		= reachable
		    ? "replay-delivered mod-write, no-exec replay capture "
		      "reachable from the group"
		    : "replay-delivered mod-write, no-exec replay capture in "
		      "the same function (persistent replay slot, cross-"
		      "invocation reassembly)";
	      return true;
	    }
	}

      /* Minimum issue-word distance from the exit of GRP's block to
	 CAP, pruned at WINDOW.  The rest of GRP's block is not counted,
	 which is conservative.  */
      hash_map<basic_block, unsigned> best;
      std::vector<std::pair<unsigned, basic_block>> work;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, grp.scan->bb->succs)
	work.emplace_back (0u, e->dest);
      bool refuse = false;
      while (!work.empty () && !refuse)
	{
	  auto it = std::min_element (work.begin (), work.end ());
	  unsigned cost = it->first;
	  basic_block bb = it->second;
	  work.erase (it);
	  if (cost >= window)
	    continue;
	  unsigned *seen = best.get (bb);
	  if (seen && *seen <= cost)
	    continue;
	  best.put (bb, cost);
	  if (bb == cap->bb)
	    {
	      if (cost + words_before_capture (fn, cap) < window)
		refuse = true;
	      continue;
	    }
	  unsigned out = cost + block_frontend_words (fn, bb);
	  if (out >= window)
	    continue;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    work.emplace_back (out, e->dest);
	}
      if (refuse)
	{
	  *hazard = cap;
	  return true;
	}
    }
  return false;
}

/* Return true if a slot program in the preheader of LOOP is owned by
   MEMBERS: every instruction of the loop must be a member group's row or
   increment, or allowed inside the configuration window by CAPS.  FN is
   the function scan.  */

static bool
loop_config_owned_p (class loop *loop, function_scan &fn,
		     const std::vector<group *> &members,
		     const autoincr_caps &caps)
{
  basic_block *bbs = get_loop_body (loop);
  bool ok = true;
  for (unsigned ix = 0; ok && ix != loop->num_nodes; ++ix)
    {
      bb_scan *scan = find_scan (fn, bbs[ix]);
      if (!scan)
	{
	  ok = false;
	  break;
	}
      std::vector<bool> owned (scan->items.size (), false);
      for (group *grp : members)
	if (grp->scan == scan)
	  for (unsigned cx : grp->cand_ix)
	    {
	      owned[scan->candidates[cx].lead_item] = true;
	      owned[scan->candidates[cx].incr_item] = true;
	    }
      for (unsigned jx = 0; ok && jx != scan->items.size (); ++jx)
	if (!owned[jx] && !config_window_item_ok (scan->items[jx], caps))
	  ok = false;
    }
  free (bbs);
  return ok;
}

/* Return the slot-occupying words from the head of GRP's block to the
   group's first consumer.  Preceding blocks only add words, so this is a
   lower bound for a program placed in the preheader.  */

static unsigned
block_prefix_distance (const group &grp)
{
  const candidate &first = grp.scan->candidates[grp.cand_ix.front ()];
  unsigned words = first.consume_prefix;
  for (unsigned ix = 0; ix != first.lead_item; ++ix)
    words += item_issue_words (grp.scan->items[ix]);
  return words;
}

/* Enforce CAPS's SETC16-to-consume distance on GRP's in-block placement:
   move the anchor earlier from the first row's lead over items allowed in
   the configuration window, but not before FLOOR, until the distance is
   met.  Set guard_refused if it cannot be.  */

static void
adjust_anchor_for_guard (group &grp, unsigned floor,
			 const autoincr_caps &caps)
{
  bb_scan &scan = *grp.scan;
  const candidate &first = scan.candidates[grp.cand_ix.front ()];
  unsigned anchor = first.lead_item;
  unsigned dist = first.consume_prefix;
  while (dist < caps.min_config_distance && anchor > floor
	 && config_window_item_ok (scan.items[anchor - 1], caps))
    {
      --anchor;
      dist += item_issue_words (scan.items[anchor]);
    }
  grp.anchor_item = anchor;
  if (dist < caps.min_config_distance)
    {
      grp.guard_refused = true;
      rvtt_refuse (RVTT_REF_CONFIGURATION_TO_CONSUME, dump_file,
		   "Dst-autoincr refusal: configuration-to-consume "
		   "distance %u below guard %u (bb %d)\n", dist,
		   caps.min_config_distance, scan.bb->index);
    }
}

/* Decide where to place the slot program for GROUPS in FN, using CAPS.
   The scratch slot is global state, so shared placements are only tried
   when every group needs the same program (a single stride).  In order of
   preference:

     - in the dedicated preheader of a loop whose body is owned;
     - once before the first of several groups in one block, if the items
       between them are allowed in the configuration window;
     - before each group's first row.

   The distance guard applies to every placement.  */

static void
place_groups (function_scan &fn, std::vector<group> &groups,
	      const autoincr_caps &caps)
{
  if (groups.empty ())
    return;

  bool single_program = true;
  for (const group &grp : groups)
    if (grp.stride != groups.front ().stride)
      single_program = false;

  for (group &grp : groups)
    {
      grp.use_preheader = false;
      grp.preheader = nullptr;
      grp.emit_config = true;
      grp.shared_set = -1;
      grp.guard_refused = false;
      grp.crossing_charge = 0;
      grp.live_crossing = false;
      grp.contract = false;
      grp.dynamic_rows = grp.cand_ix.size ();
      grp.anchor_item
	= grp.scan->candidates[grp.cand_ix.front ()].lead_item;
    }

  if (single_program)
    {
      /* Preheader placement, per innermost loop containing groups.  All
	 groups need the same program, so ownership only has to exclude
	 other writers.  */
      int next_set = 0;
      std::vector<class loop *> loops;
      for (const group &grp : groups)
	{
	  class loop *loop = grp.scan->bb->loop_father;
	  if (loop && loop->num != 0
	      && std::find (loops.begin (), loops.end (), loop)
		 == loops.end ())
	    loops.push_back (loop);
	}
      for (class loop *loop : loops)
	{
	  std::vector<group *> members;
	  for (group &grp : groups)
	    if (grp.scan->bb->loop_father == loop && !grp.use_preheader)
	      members.push_back (&grp);
	  if (members.empty ())
	    continue;
	  basic_block preheader = rvtt_dedicated_loop_preheader (loop);
	  if (preheader && !loop_config_owned_p (loop, fn, members, caps))
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: dominating placement "
			 "refused: foreign effect on a path (loop %d)\n",
			 loop->num);
	      preheader = nullptr;
	    }
	  if (!preheader)
	    continue;
	  gcov_type iterations
	    = expected_loop_iterations_unbounded (loop) + 1;
	  if (iterations < 2)
	    {
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr refusal: unknown trip "
			 "count for loop group (bb %d)\n",
			 members.front ()->scan->bb->index);
	      continue;
	    }
	  unsigned dist = ~0u;
	  for (const group *grp : members)
	    dist = std::min (dist, block_prefix_distance (*grp));
	  if (dist < caps.min_config_distance)
	    {
	      rvtt_refuse (RVTT_REF_CONFIGURATION_TO_CONSUME, dump_file,
			   "Dst-autoincr: dominating placement "
			   "refused: configuration-to-consume distance %u "
			   "below guard %u (loop %d)\n", dist,
			   caps.min_config_distance, loop->num);
	      continue;
	    }
	  bool first = true;
	  for (group *grp : members)
	    {
	      grp->use_preheader = true;
	      grp->preheader = preheader;
	      grp->emit_config = first;
	      grp->shared_set = next_set;
	      grp->dynamic_rows
		= (HOST_WIDE_INT) iterations * grp->cand_ix.size ();
	      first = false;
	    }
	  ++next_set;
	}

      /* Shared placement for the remaining groups if they are all in one
	 block.  */
      std::vector<group *> rest;
      for (group &grp : groups)
	if (!grp.use_preheader)
	  rest.push_back (&grp);
      bool same_block = rest.size () > 1;
      for (const group *grp : rest)
	if (grp->scan != rest.front ()->scan)
	  same_block = false;
      if (same_block)
	{
	  bb_scan &scan = *rest.front ()->scan;
	  std::vector<bool> owned (scan.items.size (), false);
	  unsigned first_lead = ~0u, last_incr = 0;
	  for (const group *grp : rest)
	    for (unsigned cx : grp->cand_ix)
	      {
		const candidate &cand = scan.candidates[cx];
		owned[cand.lead_item] = true;
		owned[cand.incr_item] = true;
		first_lead = std::min (first_lead, cand.lead_item);
		last_incr = std::max (last_incr, cand.incr_item);
	      }
	  bool ok = true;
	  for (unsigned ix = first_lead; ok && ix <= last_incr; ++ix)
	    if (!owned[ix] && !config_window_item_ok (scan.items[ix], caps))
	      ok = false;
	  if (ok)
	    {
	      /* The earliest group carries the program for all.  */
	      group *lead = rest.front ();
	      for (group *grp : rest)
		if (grp->cand_ix.front () < lead->cand_ix.front ())
		  lead = grp;
	      adjust_anchor_for_guard (*lead, 0, caps);
	      if (!lead->guard_refused)
		{
		  for (group *grp : rest)
		    {
		      grp->shared_set = next_set;
		      grp->emit_config = grp == lead;
		    }
		  return;
		}
	      lead->guard_refused = false;
	      if (dump_file)
		fprintf (dump_file, "Dst-autoincr: shared placement refused "
			 "by the distance guard (bb %d)\n", scan.bb->index);
	    }
	  else if (dump_file)
	    fprintf (dump_file, "Dst-autoincr: shared placement refused: "
		     "foreign effect between groups (bb %d)\n",
		     scan.bb->index);
	}
    }

  /* Per-group placement for the rest.  The anchor never moves across a
     preceding group's rows, whose window is still consuming.  */
  for (group &grp : groups)
    {
      if (grp.use_preheader || grp.shared_set >= 0)
	continue;
      unsigned floor = 0;
      for (const group &other : groups)
	if (other.scan == grp.scan && &other != &grp
	    && grp.scan->candidates[other.cand_ix.back ()].incr_item
	       < grp.scan->candidates[grp.cand_ix.front ()].lead_item)
	  floor = std::max (floor,
			    grp.scan->candidates[other.cand_ix.back ()]
			      .incr_item + 1);
      adjust_anchor_for_guard (grp, floor, caps);
    }
}

/* Emit the scratch-slot program for STRIDE before BEFORE, or after AFTER
   if BEFORE is null, programming every field of every physical slot behind
   the scratch modifier in CAPS.  */

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

/* Transform GRP: emit the slot program at its placement unless another
   group or the cross-call contract provides it, retarget each row's
   terminator access to CAPS's scratch modifier, and delete the rows'
   TTINCRWC instructions.  */

static void
transform_group (const group &grp, const autoincr_caps &caps)
{
  bb_scan &scan = *grp.scan;

  if (grp.emit_config)
    {
      if (grp.use_preheader)
	{
	  rtx_insn *end = BB_END (grp.preheader);
	  if (end && JUMP_P (end))
	    emit_owned_config (caps, grp.stride, end, nullptr);
	  else
	    emit_owned_config (caps, grp.stride, nullptr, end);
	}
      else
	emit_owned_config (caps, grp.stride,
			   scan.items[grp.anchor_item].insn, nullptr);
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
    {
      if (grp.emit_config)
	fprintf (dump_file,
		 "Dst-autoincr group: bb %d rows %u stride "
		 HOST_WIDE_INT_PRINT_DEC " config %u words%s\n",
		 scan.bb->index, unsigned (grp.cand_ix.size ()),
		 grp.stride, caps.nslots * 3,
		 grp.use_preheader ? " (preheader)" : "");
      else
	fprintf (dump_file,
		 "Dst-autoincr group: bb %d rows %u stride "
		 HOST_WIDE_INT_PRINT_DEC " %s%s\n",
		 scan.bb->index, unsigned (grp.cand_ix.size ()),
		 grp.stride,
		 grp.contract ? "crosscall contract config" : "shared config",
		 grp.use_preheader ? " (preheader)" : "");
    }
}

/* Cross-call ADDR_MOD contract (-mtt-tensix-optimize-crosscall-addrmod).
   When all GROUPS of FN were refused as unprofitable, try to hoist the slot
   program once into the caller's loop entry
   (rvtt_crosscall_addrmod_hoist), so the groups need no program of their
   own.  On success clear the groups' entries in *REFUSE.  The callee-side
   conditions are:

     - every group is unshared, outside a preheader, of one stride and
       made of explicit rows, so no per-call program remains in the
       callee;
     - every other instruction of the callee, including its tail, is
       allowed in the configuration window, so nothing clobbers the slot
       between calls;
     - each group's first consumer is at least min_config_distance words
       from the function entry;
     - the rows outnumber the backedge-style charge for the mod-write
       whose consumer is the next invocation's first Dst access, counting
       only the callee's own words after the last increment.

   The caller-side checks are done by rvtt_crosscall_addrmod_hoist.  */

static void
attempt_addrmod_contract (function_scan &fn, std::vector<group> &groups,
			  const autoincr_caps &caps,
			  std::vector<bool> *refuse)
{
  const char *why = nullptr;
  HOST_WIDE_INT total_rows = 0;
  for (unsigned gx = 0; !why && gx != groups.size (); ++gx)
    {
      group &grp = groups[gx];
      if (grp.use_preheader || grp.shared_set >= 0)
	why = "preheader-placement";
      else if (grp.stride != groups.front ().stride)
	why = "stride-plural";
      else
	for (unsigned cx : grp.cand_ix)
	  if (grp.scan->candidates[cx].payload)
	    {
	      why = "replay-delivered-row";
	      break;
	    }
      total_rows += grp.cand_ix.size ();
    }

  /* Whole-callee slot-clobber scan.  */
  if (!why)
    for (bb_scan &scan : fn.blocks)
      {
	std::vector<bool> owned (scan.items.size (), false);
	for (group &grp : groups)
	  if (grp.scan == &scan)
	    for (unsigned cx : grp.cand_ix)
	      {
		owned[scan.candidates[cx].lead_item] = true;
		owned[scan.candidates[cx].incr_item] = true;
	      }
	for (unsigned jx = 0; !why && jx != scan.items.size (); ++jx)
	  if (!owned[jx] && !config_window_item_ok (scan.items[jx], caps))
	    why = "callee-slot-clobber";
	if (why)
	  break;
      }

  /* Entry distance guard.  */
  if (!why)
    for (group &grp : groups)
      if (block_prefix_distance (grp) < caps.min_config_distance)
	{
	  why = "entry-distance";
	  break;
	}

  /* Call-boundary crossing charge.  */
  unsigned boundary_charge = 0;
  if (!why)
    for (bb_scan &scan : fn.blocks)
      {
	int last = -1;
	for (unsigned cx = 0; cx != scan.candidates.size (); ++cx)
	  if (!scan.candidates[cx].dropped)
	    last = cx;
	if (last < 0)
	  continue;
	const candidate &cand = scan.candidates[last];
	if (crossing_reanchored_p (scan, cand))
	  continue;
	unsigned cover = 0;
	for (unsigned ix = cand.incr_item + 1; ix != scan.items.size ();
	     ++ix)
	  cover += item_frontend_words (scan.items[ix]);
	if (cover < caps.drained_frontend_window)
	  boundary_charge += caps.drained_frontend_window - cover;
      }
  if (!why && total_rows <= (HOST_WIDE_INT) boundary_charge)
    {
      if (dump_file)
	fprintf (dump_file, "Dst-autoincr refusal: "
		 "mod-write-dominates-crosscall-body (rows "
		 HOST_WIDE_INT_PRINT_DEC ", uncovered boundary slots %u)\n",
		 total_rows, boundary_charge);
      return;
    }
  if (why)
    {
      rvtt_refuse (RVTT_REF_CROSSCALL_ADDRMOD_UNPROVEN, dump_file,
		   "Dst-autoincr refusal: "
		   "crosscall-addrmod-unproven (%s)\n", why);
      return;
    }

  rvtt_addrmod_hoist_program prog;
  memset (&prog, 0, sizeof (prog));
  for (unsigned sx = 0; sx != caps.nslots; ++sx)
    {
      const autoincr_slot &slot = caps.slots[sx];
      prog.setc16[prog.n_setc16].reg = slot.src_reg;
      prog.setc16[prog.n_setc16++].value = 0;
      prog.setc16[prog.n_setc16].reg = slot.dst_reg;
      prog.setc16[prog.n_setc16++].value = (unsigned) groups.front ().stride;
      prog.setc16[prog.n_setc16].reg = slot.bias_reg;
      prog.setc16[prog.n_setc16++].value = 0;
    }
  prog.n_watch = caps.n_watch;
  if (caps.n_watch)
    prog.watch[0] = caps.watch_reg;

  const char *res = rvtt_crosscall_addrmod_hoist (cfun, &prog);
  if (res)
    {
      rvtt_refuse (RVTT_REF_CROSSCALL_ADDRMOD_UNPROVEN, dump_file,
		   "Dst-autoincr refusal: "
		   "crosscall-addrmod-unproven (%s)\n", res);
      return;
    }

  for (unsigned gx = 0; gx != groups.size (); ++gx)
    {
      (*refuse)[gx] = false;
      groups[gx].emit_config = false;
      groups[gx].contract = true;
    }
  if (dump_file)
    fprintf (dump_file, "Dst-autoincr crosscall-addrmod contract: rows "
	     HOST_WIDE_INT_PRINT_DEC " stride " HOST_WIDE_INT_PRINT_DEC
	     " boundary charge %u, program hoisted to caller entry "
	     "(lifted %u levels)\n", total_rows, groups.front ().stride,
	     boundary_charge, prog.lift_levels);
}

/* Run the pass on CFN.  Scan every block, give up on a function that
   already uses the scratch modifier, resolve replay captures and
   launches, then iterate to a fixed point: form maximal same-stride
   groups of candidate rows, apply the no-exec record guard, place the
   slot programs, price the backedge crossings and check profitability;
   dropping a group restarts the iteration.  Finally transform the
   surviving groups.  */

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

      /* Iterate to a fixed point, since dropping a payload's candidates
	 can split neighbouring groups.  */
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

	  /* No-exec record composition guard.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      const capture_rec *hazard;
	      const char *detail;
	      if (noexec_record_composition_p (fn, grp, caps, &hazard,
					       &detail))
		{
		  rvtt_refuse
		    (RVTT_REF_MOD_WRITE_NOEXEC_RECORD_COMPOSITION_UNAUDITED,
		     dump_file,
		     "Dst-autoincr refusal: "
		     "mod-write-noexec-record-composition-"
		     "unaudited (%s, bb %d, capture bb %d)\n",
		     detail, grp.scan->bb->index,
		     hazard->bb->index);
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		++it;
	    }
	  if (changed)
	    continue;

	  place_groups (fn, groups, caps);

	  /* Distance-guard refusals leave the group untransformed.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      if (grp.guard_refused)
		{
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		++it;
	    }
	  if (changed)
	    continue;

	  /* Backedge-crossing charge.  Groups whose rows cannot pay it
	     are refused; the others carry it into the profitability
	     check.  */
	  for (auto it = groups.begin (); it != groups.end ();)
	    {
	      group &grp = *it;
	      grp.crossing_charge
		= crossing_penalty (grp, caps, &grp.live_crossing);
	      if ((HOST_WIDE_INT) grp.crossing_charge
		  >= (HOST_WIDE_INT) grp.cand_ix.size ())
		{
		  rvtt_refuse (RVTT_REF_MOD_WRITE_DOMINATES_ROLLED_BODY,
			       dump_file,
			       "Dst-autoincr refusal: "
			       "mod-write-dominates-rolled-body (rows %u, "
			       "uncovered crossing slots %u, bb %d)\n",
			       unsigned (grp.cand_ix.size ()),
			       grp.crossing_charge, grp.scan->bb->index);
		  for (unsigned cx : grp.cand_ix)
		    grp.scan->candidates[cx].dropped = true;
		  changed = true;
		  it = groups.erase (it);
		}
	      else
		{
		  if (grp.crossing_charge && dump_file)
		    fprintf (dump_file, "Dst-autoincr: mod-write backedge "
			     "crossing priced (rows %u, uncovered crossing "
			     "slots %u, bb %d)\n",
			     unsigned (grp.cand_ix.size ()),
			     grp.crossing_charge, grp.scan->bb->index);
		  ++it;
		}
	    }
	  if (changed)
	    continue;

	  /* Profitability: the removed increments, less the crossing
	     charge, against the configuration cost, both in frontend
	     issue slots per execution of the program.  A shared program
	     is paid for once.  */
	  auto priced_rows = [] (const group &grp)
	  {
	    HOST_WIDE_INT iter_mult
	      = grp.dynamic_rows / (HOST_WIDE_INT) grp.cand_ix.size ();
	    return grp.dynamic_rows
		   - (HOST_WIDE_INT) grp.crossing_charge * iter_mult;
	  };
	  std::map<int, HOST_WIDE_INT> shared_rows;
	  for (const group &grp : groups)
	    if (grp.shared_set >= 0)
	      shared_rows[grp.shared_set] += priced_rows (grp);

	  /* A preheader program runs once per loop entry, during the
	     start-up window that the drain residual already covers, so
	     its words cost their count, plus the residual only for a
	     live crossing.  Any other program runs on every execution
	     of its region after a frontend drain, so each SETC16 costs
	     the two-cycle configuration issue class (rvtt_issue_cfg)
	     and the residual is added.  */
	  auto group_cost = [&] (const group &grp)
	  {
	    HOST_WIDE_INT cost = (HOST_WIDE_INT) caps.nslots * 3;
	    if (!grp.use_preheader)
	      cost = cost * caps.config_issue_slots
		     + caps.min_config_distance;
	    else if (grp.live_crossing)
	      cost += caps.min_config_distance;
	    return cost;
	  };

	  /* Groups whose candidates share a rewritten capture payload
	     are transformed or kept together (see the payload coverage
	     check below), so they are priced as one family: the sum of
	     their rows against one cost per distinct program.  Pricing a
	     small member alone could refuse it and, through payload
	     coverage, its profitable siblings.  Families are found by
	     union-find over group indices.  */
	  std::vector<int> fam (groups.size (), -1);
	  auto fam_find = [&] (int g)
	  {
	    while (fam[g] >= 0 && fam[g] != g)
	      g = fam[g];
	    return g;
	  };
	  {
	    std::map<const capture_rec *, int> cap_owner;
	    for (unsigned gx = 0; gx != groups.size (); ++gx)
	      for (unsigned cx : groups[gx].cand_ix)
		if (capture_rec *cap
		      = groups[gx].scan->candidates[cx].payload)
		  {
		    if (fam[gx] < 0)
		      fam[gx] = gx;
		    int me = fam_find (gx);
		    auto it = cap_owner.find (cap);
		    if (it == cap_owner.end ())
		      cap_owner[cap] = me;
		    else
		      {
			int other = fam_find (it->second);
			if (other != me)
			  fam[other] = me;
		      }
		  }
	  }
	  std::map<int, HOST_WIDE_INT> fam_removed, fam_cost;
	  std::map<int, std::vector<int>> fam_shared_seen;
	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    if (fam[gx] >= 0)
	      {
		int root = fam_find (gx);
		group &grp = groups[gx];
		fam_removed[root] += priced_rows (grp);
		bool count_cost = true;
		if (grp.shared_set >= 0)
		  {
		    std::vector<int> &seen = fam_shared_seen[root];
		    if (std::find (seen.begin (), seen.end (),
				   grp.shared_set) != seen.end ())
		      count_cost = false;
		    else
		      seen.push_back (grp.shared_set);
		  }
		if (count_cost)
		  fam_cost[root] += group_cost (grp);
	      }

	  /* Compute all verdicts before erasing, so indices stay
	     stable.  */
	  std::vector<bool> refuse (groups.size (), false);
	  std::vector<HOST_WIDE_INT> verdict_cost (groups.size (), 0);
	  std::vector<HOST_WIDE_INT> verdict_removed (groups.size (), 0);
	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    {
	      group &grp = groups[gx];
	      bool in_family = fam[gx] >= 0;
	      HOST_WIDE_INT cost, removed;
	      if (in_family)
		{
		  int root = fam_find (gx);
		  cost = fam_cost[root];
		  removed = fam_removed[root];
		}
	      else
		{
		  cost = group_cost (grp);
		  removed = grp.shared_set >= 0
		    ? shared_rows[grp.shared_set] : priced_rows (grp);
		}
	      verdict_cost[gx] = cost;
	      verdict_removed[gx] = removed;
	      if (removed <= cost)
		refuse[gx] = true;
	    }

	  /* If every group was refused, try the cross-call ADDR_MOD
	     contract.  */
	  if (riscv_tt_opt_crosscall_addrmod && !groups.empty ())
	    {
	      bool all_refused = true;
	      for (unsigned gx = 0; gx != groups.size (); ++gx)
		all_refused &= refuse[gx] ? true : false;
	      if (all_refused)
		attempt_addrmod_contract (fn, groups, caps, &refuse);
	    }

	  for (unsigned gx = 0; gx != groups.size (); ++gx)
	    if (refuse[gx] && dump_file)
	      fprintf (dump_file,
		       "Dst-autoincr refusal: unprofitable %s "
		       "(config+entry slots " HOST_WIDE_INT_PRINT_DEC
		       " >= removed " HOST_WIDE_INT_PRINT_DEC
		       ", bb %d)\n",
		       fam[gx] >= 0 ? "payload family" : "group",
		       verdict_cost[gx], verdict_removed[gx],
		       groups[gx].scan->bb->index);
	  {
	    std::vector<group> kept;
	    for (unsigned gx = 0; gx != groups.size (); ++gx)
	      if (refuse[gx])
		{
		  for (unsigned cx : groups[gx].cand_ix)
		    groups[gx].scan->candidates[cx].dropped = true;
		  changed = true;
		}
	      else
		kept.push_back (groups[gx]);
	    groups.swap (kept);
	  }

	  /* Every execution site of a rewritten payload must be a
	     surviving row, or the RWC state at the other sites would
	     change.  */
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
    rvtt_refuse_by_name (fn.bail_reason, dump_file,
			 "Dst-autoincr refusal: %s\n", fn.bail_reason);

  for (capture_rec *cap : fn.captures)
    delete cap;
  for (launch_rec *launch : fn.launches)
    delete launch;
}

const pass_data pass_data_rvtt_dst_autoincr =
{
  RTL_PASS, /* type */
  "rvtt_dst_autoincr", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
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
}; /* class pass_rvtt_dst_autoincr */

} /* anon namespace */

/* Return the drained-frontend window, so that the replay former
   (rtl-rvtt-replay.cc) uses the same value as the no-exec record guard
   here.  */

unsigned
rvtt_modwrite_drained_frontend_window (void)
{
  return target_autoincr_caps ().drained_frontend_window;
}

/* Return true if BB, up to but excluding BEFORE_INSN (or all of BB if
   null), contains an explicit row that find_candidates would accept: a
   pure Dst increment whose preceding non-neutral item is a retargetable
   Dst access through CAPS's no-increment modifier.  Capture recording
   shadows are skipped as in scan_block.  Replay-delivered rows are not
   considered, since their groups refuse any no-exec capture in the
   function anyway.  A capture of non-constant length stops the scan.  */

static bool
block_has_explicit_candidate_increment_p (basic_block bb,
					  const autoincr_caps &caps,
					  rtx_insn *before_insn)
{
  bool lead_is_access = false;
  access_info lead_acc;
  rtx_insn *insn = BB_HEAD (bb);
  rtx_insn *end = NEXT_INSN (BB_END (bb));
  for (; insn && insn != end; insn = NEXT_INSN (insn))
    {
      if (insn == before_insn)
	break;
      if (!NONDEBUG_INSN_P (insn))
	continue;
      access_info acc;
      autoincr_class cls = classify_insn (insn, &acc);
      if (cls == AIC_NEUTRAL)
	continue;
      if (cls == AIC_REPLAY)
	{
	  rtx pattern = PATTERN (insn);
	  rtx len = XVECEXP (pattern, 0, 3);
	  rtx load = XVECEXP (pattern, 0, 7);
	  if (!CONST_INT_P (len) || !CONST_INT_P (load))
	    return false;
	  if (INTVAL (load) != 0)
	    {
	      /* Capture: fold the recording shadow like scan_block.  */
	      unsigned remaining = UINTVAL (len);
	      while (remaining && insn)
		{
		  insn = NEXT_INSN (insn);
		  if (!insn || insn == end || insn == before_insn)
		    return false;
		  if (!NONDEBUG_INSN_P (insn))
		    continue;
		  if (occupies_replay_slot_p (insn))
		    --remaining;
		}
	    }
	  lead_is_access = false;
	  continue;
	}
      if (cls == AIC_INCRWC)
	{
	  HOST_WIDE_INT stride;
	  if (pure_dst_increment_p (insn, &stride) && lead_is_access
	      && lead_acc.mode == caps.noinc_mode && lead_acc.retargetable)
	    return true;
	  lead_is_access = false;
	  continue;
	}
      if (cls == AIC_ACCESS)
	{
	  lead_is_access = true;
	  lead_acc = acc;
	  continue;
	}
      lead_is_access = false; /* AIC_RWC_STEP / AIC_FOREIGN */
    }
  return false;
}

/* Return the frontend issue-slot words of BB, computed from the insns
   with the same per-insn primitive as item_frontend_words.  */

static unsigned
block_frontend_cover_words (basic_block bb)
{
  unsigned words = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    if (NONDEBUG_INSN_P (insn))
      words += insn_frontend_cover_words (insn);
  return words;
}

/* For the replay former: return true if a no-exec replay capture hoisted
   into PREHEADER (before a trailing jump, else at the end) would be within
   the drained-frontend window of a row this pass would transform.  The
   no-exec record guard would then refuse that row, so the hoist changes
   the code this pass produces.  Distances are measured as in
   noexec_record_composition_p, backwards from the insertion point; store
   the distance found in *DIST.  */

bool
rvtt_dst_autoincr_hoist_capture_composition_p (basic_block preheader,
					       unsigned *dist)
{
  *dist = 0;
  const autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available || !caps.drained_frontend_window)
    return true;
  unsigned window = caps.drained_frontend_window;

  /* A candidate in the capture's own block always counts, as in the
     group guard.  */
  rtx_insn *anchor = BB_END (preheader);
  rtx_insn *stop = (anchor && JUMP_P (anchor)) ? anchor : nullptr;
  if (block_has_explicit_candidate_increment_p (preheader, caps, stop))
    return true;
  unsigned cover = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (preheader, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (stop && insn == stop)
	break;
      cover += insn_frontend_cover_words (insn);
    }
  if (cover >= window)
    return false;

  /* Minimum-distance walk over predecessors, pruned at WINDOW.  */
  hash_map<basic_block, unsigned> best;
  std::vector<std::pair<unsigned, basic_block>> work;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, preheader->preds)
    work.emplace_back (cover, e->src);
  while (!work.empty ())
    {
      auto it = std::min_element (work.begin (), work.end ());
      unsigned cost = it->first;
      basic_block bb = it->second;
      work.erase (it);
      if (cost >= window || bb == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	continue;
      unsigned *seen = best.get (bb);
      if (seen && *seen <= cost)
	continue;
      best.put (bb, cost);
      if (block_has_explicit_candidate_increment_p (bb, caps, nullptr))
	{
	  *dist = cost;
	  return true;
	}
      unsigned out = cost + block_frontend_cover_words (bb);
      if (out >= window)
	continue;
      FOR_EACH_EDGE (e, ei, bb->preds)
	work.emplace_back (out, e->src);
    }
  return false;
}

/* Return true if INSN is a Dst access that this pass retargeted to the
   scratch modifier, so that each execution advances the Dst RWC.  Only
   this pass uses the scratch modifier, so the static modifier operand is
   enough to tell.  Used by replay formation after this pass.  */

bool
rvtt_dst_autoincr_carried_access_p (rtx_insn *insn)
{
  const autoincr_caps caps = target_autoincr_caps ();
  if (!caps.available)
    return false;
  if (GET_CODE (insn) != INSN)
    return false;
  int code = recog_memoized (insn);
  if (code < 0)
    return false;
  access_info acc;
  if (!classify_access (insn, code, &acc))
    return false;
  return acc.retargetable && acc.mode == caps.scratch_mode;
}

/* Create the Dst auto-increment pass for CTXT.  */

rtl_opt_pass *
make_pass_rvtt_dst_autoincr (gcc::context *ctxt)
{
  return new pass_rvtt_dst_autoincr (ctxt);
}
