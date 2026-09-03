/* Tensix scheduling: capture rotation
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

/* The capture-rotation unit of the Tensix scheduler
   (-mtt-tensix-optimize-capture-rotation): seam fill, prologue
   rotation and interior gap fill for capturable self-loop rows
   whose replay makes the row tail issue-adjacent to the next
   playback's head.  Split from rtl-rvtt-schedule.cc; the algorithm
   essay lives there.  */

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

/* ---- Capture rotation: cross-row interlock fill ----

   fill_interlock_shadows above fills a modeled transparent stall with an
   independent instruction found later in the SAME row.  Inside a counted
   row loop that replay formation captures and the launch unroll plays
   back-to-back, the row is a CYCLE: the capture's last instruction is
   issued immediately before the next playback's first, so independent
   members also exist across the row boundary.  This phase performs the
   two provable cyclic reorders of that cycle:

   - SEAM FILL (no boundary work): when the row's last issued word has an
     audited one-slot result latency and the row's first word consumes
     one of its destinations (a loop-carried dependence that becomes a
     back-to-back stall in the launch run), a provably independent row
     member moves to the row's tail (or head), separating the pair.  The
     move stays inside one iteration -- a plain reorder, per-row
     semantics untouched, no prologue or epilogue.  The seam is this
     phase's own territory even where the DYNAMIC delay probe fires (WH):
     no in-row mechanism reaches across the backedge, and a committed
     move must prove the probe is quiet afterwards.

   - PROLOGUE ROTATION (iteration-shifted): a filler whose inputs are all
     loop-invariant (an immediate load, a copy from an invariant
     register) may move FORWARD past its own consumers into a stalled
     gap: after the move, consumers between the old and new position read
     the previous iteration's instance -- the same value, because the
     filler's inputs never change inside the row and nothing else writes
     its destination.  The run's FIRST row has no previous instance, so
     an explicit prologue copy of the filler is emitted in the loop's
     dedicated preheader; the FINAL row needs no epilogue because the
     relocated instance still executes within its own iteration and
     leaves the same final value.  Proof obligations, refusing by name:
       . the filler's effects are audited-clean: no CC write, no
	 configuration access, no RWC step, no Dst traffic (the bare
	 unpredicated copy is exempt as established), and an audited
	 result latency of zero;
       . a lane-predicated (CC-reading) filler is admitted only when
	 every row member provably writes no CC: the CC state is then
	 constant across the whole launch run, so the filler writes the
	 same lanes with the same values on every trip and the prologue
	 copy (executing under the loop-entry CC state) covers the first
	 row exactly; the all-lanes bare copy needs no such proof;
       . every input register is invariant in the row (no writer),
	 which also excludes read-modify-write forms;
       . the filler is its destination's only writer in the row;
       . no row member before the filler reads the destination: such a
	 read consumes a value carried across the row boundary that the
	 prologue would change (the entry-boundary dependency);
       . the destination is not live into the row header;
       . every crossed instruction satisfies the established crossing
	 discipline (shadow_crossing_safe_p);
       . the loop has a dedicated preheader to hold the prologue.

   Rotation never adds a row word (the prologue copy is one delivered
   word per RUN, outside the capture), so under the corrected delivery
   model any strictly decreasing modeled cyclic stall count wins.  The
   accounting covers every adjacency a move changes, including the
   vacated position and the row-boundary adjacency; any term depending on
   an unaudited latency refuses byte-identically.  Required-nop sites
   INSIDE the row stay owned by the nop inserter and fill_nop_shadows.

   Admission is the capturable-row shape mirroring the counted-loop
   replay hoist: a single-BB counted loop whose payload is replay-safe
   Tensix words, optionally one trailing typed TTINCRWC, one scalar
   counter step after the payload, and the final conditional jump.
   Everything else refuses by name.  Purely structural: no operation
   identity, opcode calendar, coefficient value, or instruction-word
   fingerprint participates.  */

struct rotation_row
{
  basic_block bb;
  std::vector<rtx_insn *> issued; /* issued Tensix words, in order */
};

/* The non-self predecessor of self-loop BB when it is a dedicated
   preheader (single successor, no abnormal edge), else null.  */

basic_block
rotation_dedicated_preheader (basic_block bb)
{
  if (EDGE_COUNT (bb->preds) != 2)
    return nullptr;
  basic_block pre = nullptr;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src != bb)
      pre = e->src;
  if (!pre || pre == ENTRY_BLOCK_PTR_FOR_FN (cfun)
      || !single_succ_p (pre) || single_succ (pre) != bb
      || (single_succ_edge (pre)->flags & EDGE_ABNORMAL))
    return nullptr;
  return pre;
}

/* Admission: BB is a self-loop with the capturable-row shape.  Returns
   false with *REASON naming the refusal when BB is a self-loop that
   fails the shape; *REASON stays null when BB is not a self-loop.  */

static bool
rotation_row_p (basic_block bb, rotation_row *row, const char **reason)
{
  *reason = nullptr;

  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;
  if (EDGE_COUNT (bb->succs) != 2 || EDGE_COUNT (bb->preds) != 2)
    {
      *reason = "self-loop without the two-predecessor/two-successor "
		"row shape";
      return false;
    }

  row->bb = bb;
  row->issued.clear ();
  bool saw_scalar = false;
  bool saw_trailing_increment = false;

  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    {
	      *reason = "control flow inside the row";
	      return false;
	    }
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	{
	  *reason = "opaque payload";
	  return false;
	}
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	continue;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    continue; /* bookkeeping ghost */
	  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    {
	      *reason = "explicit replay owner";
	      return false;
	    }
	  if (saw_scalar)
	    {
	      *reason = "scalar payload inside the row";
	      return false;
	    }
	  if (recog_memoized (insn) == CODE_FOR_rvtt_ttincrwc)
	    {
	      if (saw_trailing_increment || row->issued.empty ())
		{
		  *reason = "untyped row-step shape";
		  return false;
		}
	      saw_trailing_increment = true;
	    }
	  else if (get_attr_xtt_replay (insn) != XTT_REPLAY_SAFE
		   || saw_trailing_increment)
	    {
	      *reason = "non-capturable word";
	      return false;
	    }
	  row->issued.push_back (insn);
	  continue;
	}
      /* Scalar RISC insn: admit exactly the loop's counter step.  */
      rtx set = single_set (insn);
      if (saw_scalar || !set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || contains_mem_rtx_p (PATTERN (insn)))
	{
	  *reason = "scalar payload beyond the counter";
	  return false;
	}
      saw_scalar = true;
    }

  if (row->issued.size () < 4)
    {
      *reason = "row too short to capture";
      return false;
    }
  return true;
}

/* Target tag used in capture-rotation dump lines ("wh" or "bh"; the
   rotation gate admits no other target).  */

static const char *
rotation_target_name ()
{
  return TARGET_XTT_TENSIX_WH ? "wh" : "bh";
}

/* Crossing walk shared by both movers: every issued Tensix insn in
   [FROM, TO] must satisfy the crossing discipline for a filler of class
   HIDDEN_FREE, and ghost/marker register references join *CROSSED.
   Returns the offending insn, or null when the segment is crossable.  */

static rtx_insn *
rotation_crossed_segment (rtx_insn *from, rtx_insn *to, bool hidden_free,
			  insn_regs *crossed)
{
  CLEAR_HARD_REG_SET (crossed->uses);
  CLEAR_HARD_REG_SET (crossed->defs);
  for (rtx_insn *x = from; x != NEXT_INSN (to); x = NEXT_INSN (x))
    {
      if (!NONDEBUG_INSN_P (x))
	continue;
      insn_regs x_regs;
      sfpu_reg_refs (x, &x_regs);
      crossed->uses |= x_regs.uses;
      crossed->defs |= x_regs.defs;
      if (GET_CODE (PATTERN (x)) == USE
	  || GET_CODE (PATTERN (x)) == CLOBBER
	  || (recog_memoized (x) >= 0
	      && get_attr_type (x) == TYPE_TENSIX
	      && !get_attr_length (x)))
	continue;
      if (!issued_tensix_p (x) || !shadow_crossing_safe_p (x, hidden_free))
	return x;
    }
  return nullptr;
}

/* ---- Plain-reorder filler pool widening ----

   The plain-reorder movers (seam fill and the interior gap fill below)
   change only the within-iteration issue order; no prologue copy ever
   executes outside the row.  For those movers the filler pool extends
   beyond the pure-LREG classes to two audited hidden-state classes,
   each admissible only when every crossed word is proven inert to the
   state the filler carries:

   - an audited Dst-touching word (the load/store subunits with an
     audited result latency and no RWC step of its own): legal to move
     iff no crossed word touches Dst or steps an RWC counter -- then
     the filler reads/writes the identical Dst rows at its new
     position, and every other word's Dst view is unchanged;
   - the typed row-step word (TTINCRWC): legal to move iff no crossed
     word touches Dst or the RWC state -- the counter step commutes
     with pure-LREG words.  While replay-hoist is enabled this class
     DEFERS by name: counted_loop_payload (rtl-rvtt-replay.cc) refuses
     any loop whose TTINCRWC is not the trailing word, so moving it
     inward would trade a whole capture for one issue slot.

   The prologue mover keeps the pure-LREG pool unchanged: its prologue
   copy executes once outside the row, which is only sound for the
   row-invariant values that pool guarantees -- a Dst access or an RWC
   step executed twice is not.

   Everything else refuses by name.  Purely structural: no operation
   identity, opcode calendar, coefficient value, or instruction-word
   fingerprint participates.  */

enum rotation_filler_kind
{
  ROT_FILLER_REFUSED,
  ROT_FILLER_LREG,	/* the established shadow_filler_p classes */
  ROT_FILLER_DST,	/* audited Dst-touching word, RWC-neutral */
  ROT_FILLER_RWC_STEP	/* the typed row-step word */
};

/* Classify INSN for the plain-reorder movers.  *WHY names the refusal
   for the widened classes; the established pure-LREG refusals stay
   silent exactly as before (byte-identical dump behavior on rows the
   widening does not reach).  */

static rotation_filler_kind
rotation_filler_kind_p (rtx_insn *insn, insn_regs *regs, bool *hidden_free,
			const char **why)
{
  *why = nullptr;
  if (shadow_filler_p (insn, regs, hidden_free))
    return ROT_FILLER_LREG;
  *hidden_free = false;
  if (JUMP_P (insn) || !issued_tensix_p (insn)
      || contains_mem_rtx_p (PATTERN (insn)))
    return ROT_FILLER_REFUSED;
  /* Every register reference must be an SFPU register (a scalar
     reference carries dependences this pool does not track), but unlike
     collect_sfpu_regs the widened classes need no LREG destination:
     stores and the row-step word define nothing.  */
  CLEAR_HARD_REG_SET (regs->uses);
  CLEAR_HARD_REG_SET (regs->defs);
  for (df_ref ref = DF_INSN_USES (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return ROT_FILLER_REFUSED;
      SET_HARD_REG_BIT (regs->uses, regno);
    }
  for (df_ref ref = DF_INSN_DEFS (insn); ref; ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return ROT_FILLER_REFUSED;
      SET_HARD_REG_BIT (regs->defs, regno);
    }
  /* Read-modify-write conservatism for CC-predicated lane writes.  */
  regs->uses |= regs->defs;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque || e.cc_write
      || e.config_dests_written || e.config_dests_read)
    return ROT_FILLER_REFUSED;
  if (e.dst_mem_read || e.dst_mem_write)
    {
      if (e.rwc.kind != xtt_rwc_effect_t::NONE)
	{
	  *why = "carries a non-neutral or unaudited RWC mode on a "
		 "Dst access";
	  return ROT_FILLER_REFUSED;
	}
      return ROT_FILLER_DST;
    }
  if (e.rwc.kind == xtt_rwc_effect_t::INC)
    return ROT_FILLER_RWC_STEP;
  return ROT_FILLER_REFUSED;
}

/* A crossed word a Dst-touching or RWC-stepping filler may pass:
   effects on record, no CC write (the filler's lane predicate and the
   row's CC constancy), and no Dst, RWC, or memory interaction of its
   own.  Fail-closed: a Dst-reading filler does not even cross another
   Dst reader.  */

static bool
rotation_dst_rwc_crossing_safe_p (rtx_insn *x)
{
  if (get_attr_xtt_replay (x) == XTT_REPLAY_OWNER
      || contains_mem_rtx_p (PATTERN (x)))
    return false;
  xtt_effect_set e = rvtt_insn_effects (x);
  return !e.opaque && !e.cc_write
    && !e.config_dests_written && !e.config_dests_read
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* Kind-aware crossing walk: the established discipline for the
   pure-LREG classes, the Dst/RWC-inert proof for the widened ones.  */

static rtx_insn *
rotation_crossed_segment_kind (rtx_insn *from, rtx_insn *to,
			       rotation_filler_kind kind, bool hidden_free,
			       insn_regs *crossed)
{
  if (kind == ROT_FILLER_LREG)
    return rotation_crossed_segment (from, to, hidden_free, crossed);
  CLEAR_HARD_REG_SET (crossed->uses);
  CLEAR_HARD_REG_SET (crossed->defs);
  for (rtx_insn *x = from; x != NEXT_INSN (to); x = NEXT_INSN (x))
    {
      if (!NONDEBUG_INSN_P (x))
	continue;
      insn_regs x_regs;
      sfpu_reg_refs (x, &x_regs);
      crossed->uses |= x_regs.uses;
      crossed->defs |= x_regs.defs;
      if (GET_CODE (PATTERN (x)) == USE
	  || GET_CODE (PATTERN (x)) == CLOBBER
	  || (recog_memoized (x) >= 0
	      && get_attr_type (x) == TYPE_TENSIX
	      && !get_attr_length (x)))
	continue;
      if (!issued_tensix_p (x) || !rotation_dst_rwc_crossing_safe_p (x))
	return x;
    }
  return nullptr;
}

/* Post-move required-nop guards, exactly fill_nop_shadows' discipline:
   the committed order must not manufacture a new DYNAMIC-delay pad site
   at the producer, the filler, or the filler's old predecessor.  */

static bool
rotation_delay_clean_p (std::vector<basic_block> &visited, basic_block bb,
			rtx_insn *producer, rtx_insn *cand, rtx_insn *prev,
			bool prev_needed_before)
{
  if (get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
      && delay_nop_needed_p (visited, bb, producer, XTT_DELAY_DYNAMIC))
    return false;
  if (get_attr_xtt_delay (cand) == XTT_DELAY_DYNAMIC
      && delay_nop_needed_p (visited, bb, cand, XTT_DELAY_DYNAMIC))
    return false;
  if (prev && get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
      && !prev_needed_before
      && delay_nop_needed_p (visited, bb, prev, XTT_DELAY_DYNAMIC))
    return false;
  return true;
}

/* Close the row's seam stall (last issued word -> first word of the next
   playback) by moving one provably independent member to the tail or the
   head.  A plain within-iteration reorder: no prologue or epilogue.  */

static bool
rotate_seam_fill (rotation_row const &row, std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();
  rtx_insn *last = issued[m - 1];
  rtx_insn *first = issued[0];

  int s_seam = adjacency_stall (last, first);
  if (s_seam < 0)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation refused: unaudited result "
		 "latency at the seam of bb %d\n", row.bb->index);
      return false;
    }
  if (s_seam == 0)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation: no modeled seam stall in "
		 "bb %d\n", row.bb->index);
      return false;
    }
  /* A STATIC delay pads before any non-nop word: no filler can close it,
     and a pad materializing at the tail would eat the closure.  */
  if (get_attr_xtt_delay (last) == XTT_DELAY_STATIC)
    return false;
  if (dump_file)
    fprintf (dump_file, "Capture rotation: modeled seam stall after uid=%d "
	     "in bb %d\n", INSN_UID (last), row.bb->index);

  for (unsigned dir = 0; dir != 2; ++dir)
    for (unsigned o = dir ? 1 : m - 2; dir ? o <= m - 2 : o != 0;
	 dir ? ++o : --o)
      {
	rtx_insn *cand = issued[o];
	insn_regs cand_regs;
	bool hidden_free;
	const char *kind_why;
	rotation_filler_kind kind
	  = rotation_filler_kind_p (cand, &cand_regs, &hidden_free,
				    &kind_why);
	if (kind == ROT_FILLER_REFUSED)
	  {
	    if (kind_why && dump_file)
	      fprintf (dump_file, "Capture rotation refused: filler uid=%d "
		       "%s\n", INSN_UID (cand), kind_why);
	    continue;
	  }
	if (kind == ROT_FILLER_RWC_STEP && riscv_tt_opt_replay_hoist)
	  {
	    rvtt_refuse (RVTT_REF_ROW_STEP, dump_file,
			 "Capture rotation refused: row-step "
			 "filler uid=%d deferred to replay capture "
			 "formation\n", INSN_UID (cand));
	    continue;
	  }
	if (audited_latency (cand) != 0
	    /* A STATIC-delay filler drags its pad into the seam slot.  */
	    || get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	  continue;

	insn_regs crossed;
	rtx_insn *blocker
	  = dir ? rotation_crossed_segment_kind (first, PREV_INSN (cand),
						 kind, hidden_free, &crossed)
		: rotation_crossed_segment_kind (NEXT_INSN (cand), last,
						 kind, hidden_free, &crossed);
	if (blocker)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: filler uid=%d "
		       "cannot cross uid=%d\n",
		       INSN_UID (cand), INSN_UID (blocker));
	    continue;
	  }
	if (hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
	    || hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
	    || hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
	  continue;

	rtx_insn *prev = issued[o - 1];
	rtx_insn *next = issued[o + 1];
	int s_prev_cand = adjacency_stall (prev, cand);
	int s_cand_next = adjacency_stall (cand, next);
	int s_prev_next = adjacency_stall (prev, next);
	int s_last_cand = adjacency_stall (last, cand);
	int s_cand_first = adjacency_stall (cand, first);
	if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	    || s_last_cand < 0 || s_cand_first < 0)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: unaudited "
		       "latency at the vacated seam of uid=%d\n",
		       INSN_UID (cand));
	    continue;
	  }
	if (s_prev_next + s_last_cand + s_cand_first
	    >= s_prev_cand + s_cand_next + s_seam)
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation refused: no modeled "
		       "stall decrease rotating uid=%d\n", INSN_UID (cand));
	    continue;
	  }

	bool prev_needed_before
	  = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	     && delay_nop_needed_p (visited, row.bb, prev,
				    XTT_DELAY_DYNAMIC));
	rtx_insn *restore_after = PREV_INSN (cand);
	int cand_uid = INSN_UID (cand);

	reorder_insns (cand, cand, dir ? PREV_INSN (first) : last);
	if (rotation_delay_clean_p (visited, row.bb, last, cand, prev,
				    prev_needed_before))
	  {
	    if (dump_file)
	      fprintf (dump_file, "Capture rotation moved uid=%d to the "
		       "seam after uid=%d target=%s\n",
		       cand_uid, INSN_UID (last), rotation_target_name ());
	    return true;
	  }
	reorder_insns (cand, cand, restore_after);
      }

  if (dump_file)
    fprintf (dump_file, "Capture rotation refused: no independent filler "
	     "reaches the seam of bb %d\n", row.bb->index);
  return false;
}

/* Close a modeled in-row stall by moving one provably independent row
   member into the gap -- a plain within-iteration reorder exactly like
   the seam fill, extended to interior gaps and the widened filler
   classes.  Runs after the seam and prologue movers, so the
   established fire shapes keep their movers byte-identically.
   Adjacency accounting is cyclic: the row replays, so a candidate at
   either row end trades against the seam adjacency.  */

static bool
rotate_interior_fill (rotation_row const &row,
		      std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();

  for (unsigned i = 0; i + 1 < m; ++i)
    {
      rtx_insn *producer = issued[i];
      rtx_insn *consumer = issued[i + 1];
      int s = adjacency_stall (producer, consumer);
      if (s < 0)
	{
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation refused: unaudited result "
		     "latency after uid=%d\n", INSN_UID (producer));
	  continue;
	}
      if (s == 0)
	continue;
      /* Required-nop sites inside the row stay owned by the nop
	 inserter and fill_nop_shadows.  */
      if (get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
	  && delay_nop_needed_p (visited, row.bb, producer,
				 XTT_DELAY_DYNAMIC))
	continue;
      if (dump_file)
	fprintf (dump_file, "Capture rotation: modeled in-row stall after "
		 "uid=%d in bb %d\n", INSN_UID (producer), row.bb->index);

      /* Forward candidates nearest-first, then backward nearest-first.  */
      for (unsigned step = 0; step != m; ++step)
	{
	  unsigned o;
	  if (step <= i && i - step <= i)
	    o = i - step;		/* i, i-1, ..., 0 */
	  else
	    o = i + 1 + (step - i);	/* i+2, i+3, ..., m-1 */
	  if (o == i || o == i + 1 || o >= m)
	    continue;

	  rtx_insn *cand = issued[o];
	  insn_regs cand_regs;
	  bool hidden_free;
	  const char *kind_why;
	  rotation_filler_kind kind
	    = rotation_filler_kind_p (cand, &cand_regs, &hidden_free,
				      &kind_why);
	  if (kind == ROT_FILLER_REFUSED)
	    {
	      if (kind_why && dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d %s\n", INSN_UID (cand), kind_why);
	      continue;
	    }
	  if (kind == ROT_FILLER_RWC_STEP && riscv_tt_opt_replay_hoist)
	    {
	      rvtt_refuse (RVTT_REF_ROW_STEP, dump_file,
			   "Capture rotation refused: row-step "
			   "filler uid=%d deferred to replay capture "
			   "formation\n", INSN_UID (cand));
	      continue;
	    }
	  if (audited_latency (cand) != 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d carries an unaudited or nonzero result "
			 "latency\n", INSN_UID (cand));
	      continue;
	    }
	  /* A STATIC-delay filler drags its pad into the vacated slot.  */
	  if (get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	    continue;

	  insn_regs crossed;
	  rtx_insn *blocker
	    = o < i ? rotation_crossed_segment_kind (NEXT_INSN (cand),
						     producer, kind,
						     hidden_free, &crossed)
		    : rotation_crossed_segment_kind (consumer,
						     PREV_INSN (cand), kind,
						     hidden_free, &crossed);
	  if (blocker)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler "
			 "uid=%d cannot cross uid=%d\n",
			 INSN_UID (cand), INSN_UID (blocker));
	      continue;
	    }
	  if (hard_reg_set_intersect_p (cand_regs.uses, crossed.defs)
	      || hard_reg_set_intersect_p (cand_regs.defs, crossed.uses)
	      || hard_reg_set_intersect_p (cand_regs.defs, crossed.defs))
	    continue;

	  /* Cyclic vacated-position exchange.  */
	  rtx_insn *prev = issued[(o + m - 1) % m];
	  rtx_insn *next = issued[(o + 1) % m];
	  int s_prev_cand = adjacency_stall (prev, cand);
	  int s_cand_next = adjacency_stall (cand, next);
	  int s_prev_next = adjacency_stall (prev, next);
	  int s_p_cand = adjacency_stall (producer, cand);
	  int s_cand_c = adjacency_stall (cand, consumer);
	  if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	      || s_p_cand < 0 || s_cand_c < 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: unaudited "
			 "latency at the vacated seam of uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (s_prev_next + s_p_cand + s_cand_c
	      >= s_prev_cand + s_cand_next + s)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no modeled "
			 "stall decrease rotating uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }

	  bool prev_needed_before
	    = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	       && delay_nop_needed_p (visited, row.bb, prev,
				      XTT_DELAY_DYNAMIC));
	  rtx_insn *restore_after = PREV_INSN (cand);
	  int cand_uid = INSN_UID (cand);

	  reorder_insns (cand, cand, producer);
	  if (rotation_delay_clean_p (visited, row.bb, producer, cand,
				      prev, prev_needed_before))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation moved uid=%d into "
			 "the in-row stall after uid=%d target=%s\n",
			 cand_uid, INSN_UID (producer),
			 rotation_target_name ());
	      return true;
	    }
	  reorder_insns (cand, cand, restore_after);
	}
    }
  return false;
}

/* Close a modeled stall (in-row or seam) by rotating an invariant-input
   member forward past its own consumers, with a prologue copy in the
   dedicated preheader covering the run's first row.  */

static bool
rotate_prologue_fill (rotation_row const &row, basic_block preheader,
		      std::vector<basic_block> &visited)
{
  auto const &issued = row.issued;
  unsigned m = issued.size ();

  /* Row-wide register write set: invariance and single-writer proofs.  */
  insn_regs rowwide;
  CLEAR_HARD_REG_SET (rowwide.uses);
  CLEAR_HARD_REG_SET (rowwide.defs);
  rtx_insn *walk;
  FOR_BB_INSNS (row.bb, walk)
    if (NONDEBUG_INSN_P (walk))
      {
	insn_regs w;
	sfpu_reg_refs (walk, &w);
	rowwide.uses |= w.uses;
	rowwide.defs |= w.defs;
      }

  /* CC constancy across the run: every issued row member provably writes
     no CC.  Lane-predicated fillers are admissible exactly then.  */
  bool row_cc_clean = true;
  for (rtx_insn *member : issued)
    if (!bare_lreg_copy_p (member))
      {
	xtt_effect_set me = rvtt_insn_effects (member);
	if (me.opaque || me.cc_write)
	  row_cc_clean = false;
      }

  /* Gap index i: in-row adjacencies (issued[i], issued[i+1]) for
     i < m - 1, then the seam (issued[m-1], issued[0]) at i == m - 1.  */
  for (unsigned i = 0; i != m; ++i)
    {
      rtx_insn *producer = issued[i];
      rtx_insn *consumer = issued[(i + 1) % m];
      bool seam = i == m - 1;

      int s = adjacency_stall (producer, consumer);
      if (s < 0)
	{
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation refused: unaudited result "
		     "latency after uid=%d\n", INSN_UID (producer));
	  continue;
	}
      if (s == 0)
	continue;
      /* Required-nop sites inside the row stay owned by the nop inserter
	 and fill_nop_shadows; the seam has no in-row owner.  */
      if (!seam
	  && get_attr_xtt_delay (producer) == XTT_DELAY_DYNAMIC
	  && delay_nop_needed_p (visited, row.bb, producer,
				 XTT_DELAY_DYNAMIC))
	continue;
      if (dump_file)
	fprintf (dump_file, "Capture rotation: modeled stall after uid=%d "
		 "in bb %d\n", INSN_UID (producer), row.bb->index);

      /* Forward moves only: candidates strictly before the gap; for the
	 seam, any interior member (the first word is the consumer).  */
      unsigned limit = seam ? m - 1 : i;
      for (unsigned o = limit; o-- != (seam ? 1 : 0);)
	{
	  rtx_insn *cand = issued[o];
	  bool hidden_free = bare_lreg_copy_p (cand);
	  xtt_effect_set eff = rvtt_insn_effects (cand);
	  if (!hidden_free
	      && (eff.opaque || eff.cc_write
		  || eff.config_dests_written || eff.config_dests_read
		  || eff.rwc.kind != xtt_rwc_effect_t::NONE
		  || eff.dst_mem_read || eff.dst_mem_write
		  || contains_mem_rtx_p (PATTERN (cand))))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "touches Dst, RWC, CC, or configuration state\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (!hidden_free && eff.cc_read && !row_cc_clean)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "is lane-predicated in a row with unproven CC "
			 "state\n", INSN_UID (cand));
	      continue;
	    }
	  if (audited_latency (cand) != 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries an unaudited or nonzero result latency\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (get_attr_xtt_delay (cand) == XTT_DELAY_STATIC)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries a static delay contract\n", INSN_UID (cand));
	      continue;
	    }
	  /* The prologue copy must be a plain single-SET word: a copied
	     hard-register clobber could land on live preheader state.  */
	  if (GET_CODE (PATTERN (cand)) != SET || !single_set (cand))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "is not a plain single-set word\n", INSN_UID (cand));
	      continue;
	    }
	  insn_regs cand_regs;
	  if (!collect_sfpu_regs (cand, &cand_regs))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "touches scalar state\n", INSN_UID (cand));
	      continue;
	    }
	  if (hard_reg_set_intersect_p (cand_regs.uses, rowwide.defs))
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "reads registers written inside the row\n",
			 INSN_UID (cand));
	      continue;
	    }

	  /* Single writer, and no reader before the filler (an entry
	     boundary dependency the prologue cannot honor).  */
	  bool sole_writer = true;
	  bool carried_read = false;
	  FOR_BB_INSNS (row.bb, walk)
	    {
	      if (walk == cand || !NONDEBUG_INSN_P (walk))
		continue;
	      insn_regs w;
	      sfpu_reg_refs (walk, &w);
	      if (hard_reg_set_intersect_p (w.defs, cand_regs.defs))
		{
		  sole_writer = false;
		  break;
		}
	    }
	  for (walk = BB_HEAD (row.bb); walk != cand;
	       walk = NEXT_INSN (walk))
	    {
	      if (!NONDEBUG_INSN_P (walk))
		continue;
	      insn_regs w;
	      sfpu_reg_refs (walk, &w);
	      if (hard_reg_set_intersect_p (w.uses, cand_regs.defs))
		{
		  carried_read = true;
		  break;
		}
	    }
	  if (!sole_writer)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "writes a register another row member also "
			 "writes\n", INSN_UID (cand));
	      continue;
	    }
	  if (carried_read)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "carries a live value across the row boundary\n",
			 INSN_UID (cand));
	      continue;
	    }
	  bool live_in = false;
	  for (unsigned r = 0; r != FIRST_PSEUDO_REGISTER; ++r)
	    if (TEST_HARD_REG_BIT (cand_regs.defs, r)
		&& bitmap_bit_p (DF_LR_IN (row.bb), r))
	      live_in = true;
	  if (live_in)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "destination is live into the row\n",
			 INSN_UID (cand));
	      continue;
	    }

	  insn_regs crossed;
	  rtx_insn *blocker
	    = rotation_crossed_segment (NEXT_INSN (cand), producer,
					hidden_free, &crossed);
	  if (blocker)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: filler uid=%d "
			 "cannot cross uid=%d\n",
			 INSN_UID (cand), INSN_UID (blocker));
	      continue;
	    }

	  rtx_insn *prev = issued[o ? o - 1 : m - 1];
	  rtx_insn *next = issued[o + 1];
	  int s_prev_cand = adjacency_stall (prev, cand);
	  int s_cand_next = adjacency_stall (cand, next);
	  int s_prev_next = adjacency_stall (prev, next);
	  int s_p_cand = adjacency_stall (producer, cand);
	  int s_cand_c = adjacency_stall (cand, consumer);
	  if (s_prev_cand < 0 || s_cand_next < 0 || s_prev_next < 0
	      || s_p_cand < 0 || s_cand_c < 0)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: unaudited "
			 "latency at the vacated seam of uid=%d\n",
			 INSN_UID (cand));
	      continue;
	    }
	  if (s_prev_next + s_p_cand + s_cand_c
	      >= s_prev_cand + s_cand_next + s)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no modeled "
			 "stall decrease rotating uid=%d\n", INSN_UID (cand));
	      continue;
	    }
	  if (!preheader)
	    {
	      if (dump_file)
		fprintf (dump_file, "Capture rotation refused: no dedicated "
			 "preheader for the prologue of bb %d\n",
			 row.bb->index);
	      continue;
	    }

	  bool prev_needed_before
	    = (get_attr_xtt_delay (prev) == XTT_DELAY_DYNAMIC
	       && delay_nop_needed_p (visited, row.bb, prev,
				      XTT_DELAY_DYNAMIC));
	  rtx_insn *restore_after = PREV_INSN (cand);
	  int cand_uid = INSN_UID (cand);

	  reorder_insns (cand, cand, producer);
	  if (!rotation_delay_clean_p (visited, row.bb, producer, cand,
				       prev, prev_needed_before))
	    {
	      reorder_insns (cand, cand, restore_after);
	      continue;
	    }

	  rtx_insn *end = BB_END (preheader);
	  rtx_insn *pro = JUMP_P (end)
	    ? emit_insn_before (copy_rtx (PATTERN (cand)), end)
	    : emit_insn_after (copy_rtx (PATTERN (cand)), end);
	  INSN_LOCATION (pro) = INSN_LOCATION (cand);
	  df_insn_rescan (pro);
	  if (dump_file)
	    fprintf (dump_file, "Capture rotation moved uid=%d into the "
		     "stall after uid=%d with prologue uid=%d target=%s\n",
		     cand_uid, INSN_UID (producer), INSN_UID (pro),
		     rotation_target_name ());
	  return true;
	}
    }
  return false;
}

/* Capture-rotation driver.  Refuses targets without audited latency
   facts; otherwise, for every block of FN that admits as a capturable
   self-loop row, repeatedly applies the three movers (seam fill,
   prologue fill, interior fill -- each committed move restarts the
   trio) until none fires or the row no longer re-admits.  */

void
rotate_capture_rows (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file, "Capture rotation refused: no audited latency "
		 "facts for this target\n");
      return;
    }

  df_analyze ();

  std::vector<basic_block> visited;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rotation_row row;
      const char *reason;
      if (!rotation_row_p (bb, &row, &reason))
	{
	  if (reason && dump_file)
	    rvtt_refuse_by_name (reason, dump_file,
				 "Capture rotation refused: %s in bb %d\n",
				 reason, bb->index);
	  continue;
	}
      basic_block preheader = rotation_dedicated_preheader (bb);

      visited.reserve (n_basic_blocks_for_fn (fn));
      while (rotate_seam_fill (row, visited)
	     || rotate_prologue_fill (row, preheader, visited)
	     || rotate_interior_fill (row, visited))
	if (!rotation_row_p (bb, &row, &reason))
	  break;
    }
}
