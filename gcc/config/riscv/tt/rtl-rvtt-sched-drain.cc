/* Tensix scheduling: macro drain and window emission services
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

/* The three emission services the macro planner calls (exported in
   rvtt-schedule.h): rvtt_macro_drain_boundary_elidable and
   rvtt_macro_drain_backedge_elidable prove a formed run's derived
   drain NOPs redundant under -mtt-tensix-optimize-drain-schedule,
   and rvtt_macro_interrow_drain_tuned derives the minimal
   inter-row drain under window-pairing.  Split from
   rtl-rvtt-schedule.cc; the algorithm essay lives there.  */

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

/* ----------------------------------------------------------------------
   Drain-aware boundary placement (macro-planner emission service, under
   -mtt-tensix-optimize-drain-schedule; placement logic lives here, the
   planner's emission consumes the verdict).

   The planner derives core_drain_slots -- the greatest event writeback
   distance past a run's last issue slot -- from the descriptor's own
   SequenceBits delay fields (rvtt-macro-sched-core.h, core_drain_slots;
   the delays are the derived timing calendars, never hand constants) and
   emits that many SFPNOPs after EVERY formed run.  A region split into
   several runs by an architectural boundary instruction therefore
   executes the drain once per run where the architecture requires it
   only before the first genuinely conflicting follower access.

   Architectural basis (the reference simulator's adjudicated
   retirement semantics, pinned to the BlackholeA0 SFPLOADMACRO
   functional spec):

     Launch-latched state (safe to mutate while events are in flight):
       L1  the store event's Dst row (dst_rwc + imm10 + DEST_TARGET
	   offset are read AT LAUNCH, tensix.cpp:9848-9853, 9905);
       L2  the store format (Misc/launch Mod0, resolved at launch :9907);
       L3  the Dst layout the format decode depends on (:9913-9917).
     Live-at-execution state (mutation inside the event horizon races):
       E1  the lane predicate (live lane-enable evaluation at execution,
	   :9908-9911) -- any CC write inside the horizon changes which
	   lanes an in-flight event touches;
       E2  LReg contents read or written by staged events (events execute
	   against live architectural state via the ordinary
	   per-instruction executors, :9830-9833);
       E3  the LoadMacroConfig state itself (templates/sequence/misc);
       E4  Dst rows an in-flight store writes (a read before writeback is
	   a RAW hazard).
     Horizon arithmetic:
       H1  event writeback slot = carrier issue slot + programmed delay
	   (the exact model core_drain_slots already uses; a run's last
	   pending writeback is therefore at most drain_slots past its
	   final issue slot);
       H2  at most one instruction issues per cycle, so the position of a
	   follower word in the issue stream is a LOWER bound on its
	   issue-cycle distance from the boundary -- counting stream
	   slots is sound, and any dynamic stall only moves follower
	   accesses later (the safe direction, since every proof below is
	   "follower access strictly after pending writeback").

   The verdict is per boundary: the drain of run K may be elided exactly
   when (a) everything between run K and run K+1 is a discovery-admitted
   pure-RWC run separator -- launch-latched state only (L1-L3) -- whose
   issued words moreover provably survive to the final stream (the
   dst-autoincr AIC_RWC_STEP contract: FACE-class typed advances and the
   audited raw SETRWC-class words separate rows and are never absorbed;
   INC-class TTINCRWC is absorbable and earns no slot credit), and
   (b) every event of run K+1 that touches any architectural state first
   executes strictly after the last pending writeback, by the derived
   slot arithmetic above, and (c) the enumerated follower words cover the
   whole horizon.  Anything unprovable refuses by name and keeps the full
   derived drain byte-identically.  The final run's drain -- the region's
   exit contract (a formed function or loop body must not hand events in
   flight to an invisible follower stream) -- is never elided; that is
   the caller's obligation, not checked here.  */

static bool
drain_refuse (FILE *dump, const char *name, rtx_insn *insn)
{
  if (dump)
    {
      rvtt_refuse_by_name (name, dump,
			   "Macro-planner drain-refusal: %s", name);
      if (insn)
	fprintf (dump, " (insn %d)", INSN_UID (insn));
      fprintf (dump, "\n");
    }
  return false;
}

/* Region members are deleted (or re-emitted inside the formed calendar)
   at formation; the boundary walk skips them.  */

static bool
drain_region_member_p (const macro_region &region, rtx_insn *insn)
{
  for (const macro_row &row : region.rows)
    {
      if (insn == row.separator || insn == row.enable)
	return true;
      for (rtx_insn *member : row.insns)
	if (insn == member)
	  return true;
    }
  return false;
}

/* Whether INSN is one of REGION's discovery-recorded run separators
   (the words allowed to stand between two formed runs).  */

static bool
drain_run_separator_p (const macro_region &region, rtx_insn *insn)
{
  for (rtx_insn *sep : region.run_separators)
    if (sep == insn)
      return true;
  return false;
}

/* Stable refusal name of a follower-event conflict, by effect class
   (E4, E1, E3, E2 in that order of specificity).  */

static const char *
drain_conflict_name (const xtt_effect_set &e)
{
  if (e.dst_mem_read || e.dst_mem_write)
    return "drain-dst-raw";
  if (e.cc_read || e.cc_write)
    return "drain-cc-live";
  if (e.config_dests_written || e.config_dests_read || e.addr_mod_slot_write)
    return "drain-config-overlap";
  return "drain-lreg-overlap";
}

/* Decoded pending-event horizon of one emitted run, shared by the
   intra-region boundary proof and the loop-backedge proof below.  All
   fields derive from the descriptor's own SequenceBits delays and the
   adopted schedule's slot assignment -- derived timing calendars,
   never hand constants.  */

struct drain_horizon
{
  int max_dist;			/* == desc.drain_slots, cross-checked  */
  unsigned words_per_row;
  int carrier_pos[8];
  auto_vec<int> word_pos;	/* per schedule event: row position    */
  struct { unsigned kind; unsigned delay; } events[8][4];
  int n_events[8];
};

/* Decode the run horizon from SCHEDULE and DESC into *H.  Word
   positions mirror emit_planner_run's emission order (issue slots
   ascending).  Per-macro launched-event timing is decoded from the
   descriptor's OWN sequence words through the established SequenceBits
   format (rvtt-macro-tables.h: byte i programs sub-unit i; case = bits
   2:0, the event executes at issue + 1 + delay(bits 5:3); provenance
   the derivation notes in rvtt-macro-derive-core.h).  The frozen whole-word
   programs left per-event delays untranscribed in the schedule
   (DELAY_UNKNOWN), but the words themselves carry them -- decoding
   the emitted words is the one derivation that can never drift from
   what the hardware sequences.  Case 1 is architecturally undefined;
   SKIP/NOP bytes stage no architectural event.  The decoded pending
   horizon is cross-checked against the descriptor's own drain_slots
   -- the two derive from the same calendar, so a mismatch means the
   timing facts are not established for this shape and the boundary
   refuses.  */

/* REQUIRE_EXACT selects the establishment rule.  The intra-region
   boundary proof (the lane-AY envelope) requires the decoded pending
   horizon to EQUAL the descriptor's emitted drain_slots -- byte-stable
   with the shipped behavior.  The loop-backedge proof admits a
   conservative emitted drain (a frozen proven-calendar figure may
   exceed the decoded truth -- the compact CC program carries 3 where
   the SequenceBits pend only 1); the decoded words are the derivation
   that can never drift from what the hardware sequences, so the proof
   horizon is the DECODED pending, refused if it ever exceeded the
   emitted drain.  */

static bool
drain_decode_horizon (const macro_schedule &schedule,
		      const macro_descriptor &desc,
		      drain_horizon *h, FILE *dump, bool require_exact)
{
  h->max_dist = desc.drain_slots;

  for (int m = 0; m != 8; ++m)
    h->carrier_pos[m] = -1;
  h->word_pos.safe_grow_cleared (schedule.events.length ());
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    h->word_pos[ix] = -1;
  h->words_per_row = 0;
  for (int slot = 0; slot != schedule.ii; ++slot)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (!ev.issues_word || ev.slot != slot)
	  continue;
	h->word_pos[ix] = h->words_per_row;
	if (ev.is_carrier && ev.macro_index < 8)
	  h->carrier_pos[ev.macro_index] = h->words_per_row;
	++h->words_per_row;
      }
  if (h->words_per_row == 0)
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  for (int m = 0; m != 8; ++m)
    h->n_events[m] = 0;
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    {
      uint8_t bytes[4];
      rvtt_macro::decompose_sequence_word (desc.seq[m], bytes);
      for (int i = 0; i != 4; ++i)
	{
	  unsigned kind, delay;
	  bool vd16, route_vb;
	  if (!rvtt_macro::decode_sequence_bits (bytes[i], &kind, &delay,
						 &vd16, &route_vb))
	    return drain_refuse (dump, "drain-delay-unproven", nullptr);
	  if (kind == rvtt_macro::SEQ_CASE_SKIP
	      || kind == rvtt_macro::SEQ_CASE_NOP)
	    continue;
	  h->events[m][h->n_events[m]].kind = kind;
	  h->events[m][h->n_events[m]].delay = delay;
	  ++h->n_events[m];
	}
    }

  /* Every sequence word's events ride a known carrier; a macro without
     a located carrier word leaves events unaccounted -- refuse.  */
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    if (h->n_events[m] > 0 && h->carrier_pos[m] < 0)
      return drain_refuse (dump, "drain-delay-unproven", nullptr);

  /* Pending horizon: greatest event-execution distance past the run's
     last issue slot, over the decoded events of the trailing rows
     (event execution = carrier position + 1 + delay; rows are
     words_per_row issue slots apart).  */
  int last_issue = (int) h->words_per_row - 1;
  int max_pending = 0;
  for (int j = 0;
       j * (int) h->words_per_row <= (int) rvtt_macro::SEQ_MAX_DELAY + 1;
       ++j)
    for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
      {
	if (h->carrier_pos[m] < 0)
	  continue;
	for (int e = 0; e != h->n_events[m]; ++e)
	  {
	    int dist = h->carrier_pos[m] + 1 + (int) h->events[m][e].delay
	      - last_issue - j * (int) h->words_per_row;
	    if (dist > max_pending)
	      max_pending = dist;
	  }
      }
  if (require_exact ? max_pending != h->max_dist
		    : max_pending > h->max_dist)
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (!require_exact)
    h->max_dist = max_pending;
  return true;
}

/* Prove that every architectural access of the follower rows
   [ROW_BEGIN, ROW_END) is ordered after every pending event of the
   horizon H, with SEP_CREDIT proven follower words already issued
   before the first row (H1+H2): a follower word at run position P
   issues no earlier than boundary + sep_credit + 1 + P, its launched
   events execute at issue + 1 + delay, an explicit word's own access
   is counted at issue (the conservative earliest), and a carrier
   word's own front-end VD write is counted at issue too (admitted
   only under the established protections -- VD alternation,
   store-only sacrificial VD, the CC-template model's next-row
   obligations).  Ordering at equal
   cycles follows the established transactional model
   (rvtt-macro-tables.h derived-calendar provenance: ISA spec +
   reference-simulator executor + the hand MulInt32 kernel --
   "retire-before-issue"): a
   staged event retiring at cycle X retires BEFORE the front-end
   instruction issuing at X executes, so a FRONT-END access at the
   last retirement cycle is ordered after every pending event
   (equality admitted); two staged EVENTS at one cycle stay a race --
   the hardware-adjudicated cc-restore-store-race failure mode -- so
   launched follower events keep the strict inequality.  The
   enumerated follower words must cover the whole horizon.  */

static bool
drain_follower_rows_ok (const macro_region &region,
			const macro_schedule &schedule,
			const macro_descriptor &desc,
			const drain_horizon &h,
			unsigned row_begin, unsigned row_end,
			unsigned sep_credit, FILE *dump)
{
  int max_dist = h.max_dist;
  unsigned base = sep_credit;	/* follower words issued before this row */
  for (unsigned r = row_begin; r != row_end; ++r)
    {
      if ((int) base + 1 > max_dist)
	break;			/* every later access clears by time */
      if (region.rows[r].insns.length () != schedule.events.length ())
	return drain_refuse (dump, "drain-follower-opaque", nullptr);
      /* The launch word's OWN front-end VD write (the fixed-VD
	 corruption class at a RUN boundary).  A launch is a
	 front-end instruction: retire-before-issue admits it at the
	 last retirement cycle (equality), but an EARLIER issue writes
	 its VD while the horizon's events -- hosted consumers of the
	 SAME descriptor's previous rows -- are still in flight.  The
	 established protections are exactly the launch-spec predicate
	 (rvtt-macro-desc.h macro_launch_spec): VD alternation (the
	 conservative VD policy's own envelope -- adjacent rows target
	 different registers), a store-only sacrificial VD (written,
	 never read), and the CC-template model's proven next-row
	 obligations (macro_cc_model: store-before-next-def,
	 restore-visibility; hardware-proven multi-row on the unified
	 where kernel).  A fixed-VD VALUE carrier has none of them --
	 the previous run's pending events read the register this
	 launch overwrites -- so the boundary refuses by name.  A
	 carrier with no launch spec on record refuses fail-closed.  */
      for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
	{
	  if (h.carrier_pos[m] < 0)
	    continue;
	  int issue = (int) base + 1 + h.carrier_pos[m];
	  if (issue >= max_dist)
	    continue;		/* front-end: equality admitted */
	  if (desc.cc.active)
	    continue;		/* CC-template inter-row contract */
	  const macro_launch_spec *spec = nullptr;
	  for (const macro_launch_spec &l : desc.launches)
	    if (l.macro_index == m)
	      {
		spec = &l;
		break;
	      }
	  if (!spec)
	    return drain_refuse (dump, "drain-follower-opaque", nullptr);
	  if (!spec->vd_alternates && !spec->is_store_only)
	    return drain_refuse (dump, "drain-follower-vd-write", nullptr);
	}
      /* Launched events, per carrier word.  */
      for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
	{
	  if (h.carrier_pos[m] < 0)
	    continue;
	  for (int e = 0; e != h.n_events[m]; ++e)
	    {
	      int exec = (int) base + 1 + h.carrier_pos[m] + 1
		+ (int) h.events[m][e].delay;
	      if (exec > max_dist)
		continue;	/* strictly after the last writeback */
	      return drain_refuse
		(dump, h.events[m][e].kind == rvtt_macro::SEQ_CASE_STORE
		 ? "drain-dst-raw" : "drain-lreg-overlap", nullptr);
	    }
	}
      /* Explicit words, at issue.  */
      for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
	{
	  const macro_event &ev = schedule.events[ix];
	  if (ev.realization != macro_event::EXPLICIT_INSN
	      || h.word_pos[ix] < 0)
	    continue;
	  int access_lb = (int) base + 1 + h.word_pos[ix];
	  /* Front-end access: retire-before-issue admits equality with
	     the last pending retirement.  */
	  if (access_lb >= max_dist)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (region.rows[r].insns[ix]);
	  return drain_refuse (dump, drain_conflict_name (e),
			       region.rows[r].insns[ix]);
	}
      base += h.words_per_row;
      if (desc.keep_separator && region.rows[r].separator)
	base += 1;		/* pure-RWC word re-emitted verbatim */
    }

  /* The enumerated follower words must cover the whole horizon.  */
  if ((int) base < max_dist)
    return drain_refuse (dump, "drain-horizon-spill", nullptr);
  return true;
}

/* The intra-region boundary verdict (see the section comment above):
   prove that the drain after REGION's run ending at row END may be
   elided.  BEGIN is unused (the run's own rows need no re-proof);
   the follower run is rows [END, NEXT_END).  SCHEDULE and DESC are
   the adopted row schedule and its descriptor.  Proves (a) the
   inter-run stream holds only recorded run separators, earning slot
   credit for never-absorbed pure-RWC words, (b) the decoded pending
   horizon equals the emitted drain figure, and (c) every follower
   access clears that horizon.  Returns true when elidable; any
   unproven piece refuses by name to DUMP.  */

bool
rvtt_macro_drain_boundary_elidable (const macro_region &region,
				    const macro_schedule &schedule,
				    const macro_descriptor &desc,
				    unsigned begin, unsigned end,
				    unsigned next_end, FILE *dump)
{
  (void) begin;
  int max_dist = desc.drain_slots;
  if (max_dist < 0)
    /* Unknown delays refuse formation before emission ever runs; keep
       the refusing direction locally anyway (drain-delay-unproven is the
       existing CORE_DELAY_UNKNOWN path).  */
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (max_dist == 0)
    return true;		/* Nothing to elide; trivially proven.  */
  if (end >= next_end || next_end > region.rows.length ())
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  /* (a) The inter-run stream: separators only, all launch-latched.  */
  const macro_row &last_row = region.rows[end - 1];
  rtx_insn *from = last_row.separator
    ? last_row.separator : last_row.insns[last_row.insns.length () - 1];
  const macro_row &next_row0 = region.rows[end];
  rtx_insn *to = next_row0.enable ? next_row0.enable : next_row0.insns[0];

  unsigned sep_credit = 0;
  for (rtx_insn *insn = NEXT_INSN (from); insn != to;
       insn = insn ? NEXT_INSN (insn) : nullptr)
    {
      if (!insn)
	return drain_refuse (dump, "drain-follower-opaque", nullptr);
      if (!NONDEBUG_INSN_P (insn))
	continue;
      rtx pat = PATTERN (insn);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      if (!drain_run_separator_p (region, insn))
	return drain_refuse (dump, "drain-follower-opaque", insn);
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque || e.lreg_read || e.lreg_write || e.cc_read || e.cc_write
	  || e.config_dests_written || e.config_dests_read
	  || e.addr_mod_slot_write || e.dst_mem_read || e.dst_mem_write)
	return drain_refuse (dump, "drain-follower-opaque", insn);
      switch (e.rwc.kind)
	{
	case xtt_rwc_effect_t::SET:
	case xtt_rwc_effect_t::FACE:
	  /* Launch-latched-only mutator (L1) no later pass absorbs (the
	     dst-autoincr AIC_RWC_STEP contract), so its issued words hold
	     their stream slots: slot credit.  A raw `.ttinsn' word is
	     exactly one word by the extraction contract
	     (rvtt_raw_ttinsn_word); a typed pattern's word count is its
	     machine-description length in 4-byte Tensix words.  */
	  if (asm_noperands (pat) >= 0)
	    sep_credit += 1;
	  else
	    sep_credit += get_attr_length (insn) / 4;
	  break;
	case xtt_rwc_effect_t::INC:
	  /* Launch-latched-neutral (L1), but dst-autoincr may absorb it
	     (AIC_INCRWC): presence in the final stream is unproven, so it
	     earns no slot credit.  */
	  break;
	default:
	  return drain_refuse (dump, "drain-follower-opaque", insn);
	}
    }

  drain_horizon h;
  if (!drain_decode_horizon (schedule, desc, &h, dump, /*require_exact=*/true))
    return false;
  if (dump)
    fprintf (dump, "Macro-planner drain-boundary: drain=%d"
	     " separator-credit=%u words-per-row=%u\n",
	     h.max_dist, sep_credit, h.words_per_row);

  /* (b)+(c) The next run's accesses and horizon coverage.  */
  if (!drain_follower_rows_ok (region, schedule, desc, h, end, next_end,
			       sep_credit, dump))
    return false;

  if (dump)
    /* NB the harness fire witness 'run-boundary drain elided'
       (_REVIEWED_FIRE_WITNESSES) is SPLIT across the two source lines
       below -- a literal source grep for the full witness misses it;
       the runtime dump line matches (FH audit witness-check gotcha).  */
    fprintf (dump, "Macro-planner drain-schedule: run-boundary drain"
	     " elided (drain=%d separator-credit=%u)\n",
	     h.max_dist, sep_credit);
  return true;
}

/* ----------------------------------------------------------------------
   Loop-backedge drain elision (the drain-route remainder).

   A loop-body region has one boundary the intra-region proof above can
   never reach: its final run ends at the loop latch, so today the
   derived drain executes once per trip -- where the architecture
   requires it once per loop EXIT.  The backedge follower stream is not
   invisible: it is the in-body tail after the final run, plus anything
   ahead of the region at the loop-body head, plus the region's OWN
   first run in the next iteration -- the identical row-succession the
   adopted schedule already sequences run-internally.  The verdict
   below proves that stream with the same decoded slot arithmetic
   (drain_decode_horizon / drain_follower_rows_ok) after classifying
   every interposed instruction:

     - never-absorbed launch-latched pure-RWC words (SET/FACE class,
       the AIC_RWC_STEP contract) earn slot credit, absorbable INC
       words none -- the intra-region separator discipline verbatim;
     - proven-neutral scalar instructions (no call, no asm, no memory
       store -- a scalar can only touch Tensix state by delivering a
       word through the instruction FIFO, which is a memory store)
       earn no credit: they can only DELAY follower issue, the safe
       direction under H2;
     - anything else refuses by name.

   The architectural exit contract is PRESERVED, not weakened: the
   caller of this verdict emits the full derived drain on the loop's
   exit path, so no event ever reaches an unproven follower stream.
   The first trip trivially satisfies the proof (no pending events at
   loop entry).  The replay/MOP passes that later re-deliver the body
   can only ADD issue slots ahead of the follower words (record and
   playback words), which moves follower accesses later -- the safe
   direction, same as any dynamic stall (H2).  */

static void
drain_note_mem_store (rtx x, const_rtx, void *data)
{
  if (MEM_P (x))
    *(bool *) data = true;
}

/* Classify one interposed follower-stream instruction at the loop
   backedge.  Returns true when INSN is proven drain-neutral,
   accumulating slot credit into *CREDIT; otherwise false with the
   refusal name in *WHY.  */

static bool
drain_stream_insn_neutral (rtx_insn *insn, unsigned *credit,
			   const char **why)
{
  *why = "drain-follower-opaque";
  rtx pat = PATTERN (insn);
  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
    return true;
  if (CALL_P (insn))
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (!e.opaque)
    {
      if (e.lreg_read || e.lreg_write || e.cc_read || e.cc_write
	  || e.config_dests_written || e.config_dests_read
	  || e.addr_mod_slot_write || e.dst_mem_read || e.dst_mem_write)
	{
	  /* A real effect conflict names its class (E1-E4).  */
	  *why = drain_conflict_name (e);
	  return false;
	}
      switch (e.rwc.kind)
	{
	case xtt_rwc_effect_t::SET:
	case xtt_rwc_effect_t::FACE:
	  /* Never-absorbed launch-latched words hold their stream slots
	     (AIC_RWC_STEP contract): slot credit, sized as in the
	     intra-region walk.  */
	  if (asm_noperands (pat) >= 0)
	    *credit += 1;
	  else
	    *credit += get_attr_length (insn) / 4;
	  return true;
	case xtt_rwc_effect_t::INC:
	  /* Absorbable (AIC_INCRWC): neutral, no slot credit.  */
	  return true;
	case xtt_rwc_effect_t::NONE:
	  /* Audited effect-free Tensix instruction: neutral; its slot
	     survival is unproven, so no credit.  */
	  return true;
	default:
	  return false;
	}
    }
  /* Unaudited Tensix instruction or unproven raw word: refuse.  */
  if (asm_noperands (pat) >= 0)
    return false;
  if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
    return false;
  /* A scalar RISC instruction.  It can only touch Tensix state by
     delivering a word through a memory store; refuse stores, admit
     pure register/branch scalars with no slot credit.  */
  bool stores_mem = false;
  note_stores (insn, drain_note_mem_store, &stores_mem);
  if (stores_mem)
    return false;
  return true;
}

/* The loop-backedge verdict (see the section comment above): prove
   that REGION's final-run drain may be elided across the backedge of
   its single-block loop body.  SCHEDULE and DESC are the adopted row
   schedule and its descriptor; FIRST_RUN_END bounds the rows of the
   region's first run, the follower stream's next-iteration rows.  The
   in-body tail after the final run and anything ahead of the region at
   the body head are classified for slot credit; the decoded pending
   horizon (conservative form: decoded pending, at most the emitted
   drain) must then clear against those first-run rows.  Returns true
   when elidable; any unproven piece refuses by name to DUMP.  */

bool
rvtt_macro_drain_backedge_elidable (const macro_region &region,
				    const macro_schedule &schedule,
				    const macro_descriptor &desc,
				    unsigned first_run_end, FILE *dump)
{
  if (!region.loop_body || !region.bb)
    return drain_refuse (dump, "drain-follower-opaque", nullptr);
  int max_dist = desc.drain_slots;
  if (max_dist < 0)
    return drain_refuse (dump, "drain-delay-unproven", nullptr);
  if (max_dist == 0)
    return true;
  if (first_run_end == 0 || first_run_end > region.rows.length ())
    return drain_refuse (dump, "drain-follower-opaque", nullptr);

  const char *why = nullptr;
  unsigned credit = 0;

  /* Tail walk: from the final run's end to the end of the loop body
     (the latch branch included).  */
  const macro_row &last_row = region.rows[region.rows.length () - 1];
  rtx_insn *from = last_row.separator
    ? last_row.separator : last_row.insns[last_row.insns.length () - 1];
  for (rtx_insn *insn = NEXT_INSN (from);
       insn && BLOCK_FOR_INSN (insn) == region.bb; insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      /* A discovery-admitted pure-RWC run separator surviving in the
	 tail is exactly the creditable launch-latched class; classify
	 it like any other stream insn (it earns its slot credit).  */
      if (!drain_stream_insn_neutral (insn, &credit, &why))
	return drain_refuse (dump, why, insn);
    }

  /* Head walk: anything ahead of the region at the loop-body head
     (executed after the backedge, before the region re-enters).  */
  rtx_insn *anchor = region.rows[0].enable
    ? region.rows[0].enable : region.rows[0].insns[0];
  for (rtx_insn *insn = BB_HEAD (region.bb); insn && insn != anchor;
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (drain_region_member_p (region, insn))
	continue;
      if (!drain_stream_insn_neutral (insn, &credit, &why))
	return drain_refuse (dump, why, insn);
    }

  drain_horizon h;
  if (!drain_decode_horizon (schedule, desc, &h, dump,
			     /*require_exact=*/false))
    return false;
  if (dump)
    fprintf (dump, "Macro-planner drain-backedge: drain=%d pending=%d"
	     " stream-credit=%u words-per-row=%u\n",
	     desc.drain_slots, h.max_dist, credit, h.words_per_row);

  if (!drain_follower_rows_ok (region, schedule, desc, h, 0, first_run_end,
			       credit, dump))
    return false;

  if (dump)
    fprintf (dump, "Macro-planner drain-schedule: loop-backedge drain"
	     " elided (drain=%d stream-credit=%u)\n",
	     h.max_dist, credit);
  return true;
}

/* ----------------------------------------------------------------------
   Lane FT window-pairing: inter-row drain tuning (macro-planner emission
   service, under -mtt-tensix-optimize-window-pairing; the planner's
   emission consumes the verdict).

   The lane-EV inter-row obligation places the FULL derived drain between
   consecutive rows whenever any launch is a fixed-VD VALUE carrier -- a
   shape rule, deliberately register-blind.  This service derives the
   MINIMAL inter-row drain from the same architectural model the boundary
   and backedge proofs above already use (L1-L3/E1-E4/H1-H2, the
   retire-before-issue transactional model), made register-, Dst-, and
   sub-unit-exact:

     - The pending horizon is decoded from the descriptor's OWN
       SequenceBits (drain_decode_horizon's doctrine: the emitted words
       are the one derivation that can never drift from what the
       hardware sequences), cross-checked require-exact against the
       descriptor's emitted drain figure.  Each decoded event is mapped
       back to its origin row instruction through the schedule's
       (macro, realized-sub-unit) key; any unaccounted or ambiguous
       event refuses.
     - Each staged event carries its realized architectural footprint:
       the origin instruction's typed effect set, widened by the
       SFPLOADMACRO override rules decoded from its OWN SequenceBits
       byte (the launch VD joins the reads; the result register is the
       launch VD or LReg[16] per the VD16 bit; the store's value
       register follows the store override cases), plus the descriptor
       template's hidden LREG writes.  [ISA] BlackholeA0 SFPLOADMACRO.md
       functional model (Insn.VB/VC/VD overrides; store VD cases
       0x40/0x80).
     - A follower access ordered after every pending writeback needs no
       footprint at all (the established rule: front-end accesses admit
       equality under retire-before-issue; staged events keep the strict
       inequality).  An access INSIDE the horizon is admitted exactly
       when its footprint is disjoint from every not-yet-retired pending
       event: LREG read/write intersection, CC (one side writing), any
       configuration write, and Dst overlap all refuse; two staged
       events at one cycle additionally refuse on sub-unit equality (the
       occupancy rule core_check_subunit_occupancy already enforces
       within a row; SFPLOADMACRO.md's four concurrent columns are the
       architectural basis for admitting distinct sub-units).
     - Dst overlap uses the audited physical-row model (the
       gimple-rvtt-transp-involution.cc access-rows audit; the
       rtl-rvtt-lp-alloc.cc dst32b window is the same fact): an access at
       constant address A touches lane rows [A & ~3, (A & ~3) + 3],
       mapped through dst32b_adjust_row for the 32-bit format class, with
       the configuration-resolved classes taking the union.  Disjoint row
       sets prove disjointness outright.  Overlapping row sets are still
       disjoint when the two accesses select OPPOSITE column parities --
       Column = (Lane & 7)*2 + ((Addr & 2) || DEST_{RD,WR}_COL_EXCHANGE)
       in both functional models (SFPLOAD.md, SFPSTORE.md), and the
       column index is preserved into the underlying DstBits storage by
       both the 16-bit and the 32-bit view (Dst.md) -- PROVIDED the
       column-exchange LaneConfig bits are architecturally default.  The
       parity clause therefore demands the same LaneConfig discipline the
       DSATUR spill machinery ships (rtl-rvtt-lp-alloc.cc,
       lreg-spill-laneconfig-unproven): any function-local write that
       could reach SFPCONFIG destination 15 -- a typed dest-15
       configuration write, a call, an unproven asm word, an unaudited
       Tensix instruction -- refuses the clause; the ambient default (no
       column exchange; the simulator's reset state, what the LLK init
       sequence leaves in place per the audited dest-15 table) is the
       flag's documented platform contract, identical to the spill
       flag's.  Mod0 10 (INT32_ALL) refuses: it couples the address to
       the Sp counter and mutates it (SFPLOAD.md), breaking the
       shared-RWC-base distance model.
     - Row-to-row Dst distance is the schedule's absorbed typed stride.
       The advancing address mode provably rides the row's LAST issued
       word and every other access carries the architectural
       no-increment mode (checked from the launch specs and the explicit
       operands), and SFPLOAD/SFPSTORE apply their address-modifier
       AFTER resolving their own address (SFPLOAD.md functional-model
       order), so every access of row r resolves at the same counter
       value and row r+j sits exactly j strides away.  Pending stores
       latch their Dst row at launch (L1), so follower counter advances
       never move them.

   The verdict is the smallest inter-row NOP count n (0 <= n <
   drain_slots) whose follower stream -- the next rows at spacing n --
   passes every rule, or drain_slots (today's bytes) with the binding
   blocker named.  Every distance is a stream-slot count: H2 makes it a
   lower bound on issue-cycle distance, and dynamic stalls only move
   followers later (the safe direction).  Emission under refusal is
   byte-identical to the established placement.  Frozen whole-word
   programs
   whose events the schedule cannot account for refuse through the
   mapping rule -- the signbit family stays on its proven rolled
   calendar.  */

namespace {

/* One staged (launched) event with its realized footprint.  */
struct wp_event
{
  int exec;			/* in-row execution time: carrier word
				   position + 1 + decoded delay (the
				   drain_decode_horizon convention)    */
  xtt_subunit_t subunit;	/* realized sub-unit (byte position)   */
  unsigned sched_ix;		/* schedule/row-insn index of origin   */
  unsigned macro_index;
  unsigned kind;		/* SequenceBits case		       */
  bool vd16, route_vb;
  uint32_t lreg_read, lreg_write;
  bool cc_read, cc_write;
  bool cfg_write;
  bool dst_read, dst_write;
  bool dst_typed;		/* constant (addr, mod0) on record     */
  int dst_addr, dst_mod0;
};

/* Audited Dst physical-row model (see the comment above; the same model
   gimple-rvtt-transp-involution.cc:access_rows audits).  */

static unsigned
wp_access_rows (int addr, int mod0, unsigned rows[20])
{
  unsigned n = 0;
  unsigned r0 = ((unsigned) addr & 1023u) & ~3u;
  bool m32 = mod0 == 3 || mod0 == 4 || mod0 == 7 || mod0 == 9 || mod0 == 12
	     || mod0 == 10;
  bool m16 = !m32;		/* incl. mod0 0: config-resolved, union */
  if (mod0 == 0)
    m32 = true;
  for (unsigned r = r0; r != r0 + 4; ++r)
    {
      unsigned lane_row = r & 1023;
      unsigned adj = ((lane_row & 0x1F8) << 1) | (lane_row & 0x207);
      if (m32)
	{
	  rows[n++] = adj & 1023;
	  rows[n++] = (adj + 8) & 1023;
	}
      if (m16)
	{
	  rows[n++] = lane_row;		/* 16-bit layout */
	  rows[n++] = adj & 1023;	/* 32-bit layout via dst16->32 */
	  rows[n++] = (adj + 8) & 1023;
	}
    }
  return n;
}

/* Whether the typed Dst accesses (ADDR_A, MOD0_A) and (ADDR_B, MOD0_B)
   touch disjoint physical row sets under the audited access-row model
   above.  */

static bool
wp_rows_disjoint (int addr_a, int mod0_a, int addr_b, int mod0_b)
{
  unsigned ra[20], rb[20];
  unsigned na = wp_access_rows (addr_a, mod0_a, ra);
  unsigned nb = wp_access_rows (addr_b, mod0_b, rb);
  for (unsigned i = 0; i != na; ++i)
    for (unsigned j = 0; j != nb; ++j)
      if (ra[i] == rb[j])
	return false;
  return true;
}

/* Whether the column-exchange LaneConfig bits are provably at their
   architectural default throughout FN: no reachable writer of SFPCONFIG
   destination 15 -- typed dest-15 writes, calls, unproven asm words, and
   unaudited Tensix instructions all refuse.  Scalar (non-Tensix)
   instructions cannot issue an SFPCONFIG.  Ambient state is the flag's
   documented platform contract (rtl-rvtt-lp-alloc.cc discipline).  */

static bool
wp_laneconfig_default_proved (function *fn)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  rtx pat = PATTERN (insn);
	  if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	    continue;
	  if (CALL_P (insn))
	    return false;
	  if (asm_noperands (pat) >= 0)
	    {
	      xtt_rwc_effect_t rwc;
	      if (!rvtt_raw_pure_dst_rwc (insn, &rwc))
		return false;
	      continue;
	    }
	  if (recog_memoized (insn) < 0)
	    return false;
	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (e.opaque || (e.config_dests_written & (1u << 15)))
	    return false;
	}
    }
  return true;
}

/* Dst disjointness of two typed accesses under the audited model.
   LANECFG is the lazily computed tri-state (-1 unknown / 0 unproven /
   1 proven).  *WHY names the failing clause.  */

static bool
wp_dst_disjoint (const wp_event &a, const wp_event &b, function *fn,
		 int *lanecfg, const char **why)
{
  if (!a.dst_typed || !b.dst_typed)
    {
      *why = "window-pairing-dst-mode-unproven";
      return false;
    }
  if (wp_rows_disjoint (a.dst_addr, a.dst_mod0, b.dst_addr, b.dst_mod0))
    return true;
  /* Column-parity clause.  */
  if (((a.dst_addr >> 1) & 1) != ((b.dst_addr >> 1) & 1))
    {
      if (*lanecfg < 0)
	*lanecfg = wp_laneconfig_default_proved (fn) ? 1 : 0;
      if (*lanecfg == 1)
	return true;
      *why = "window-pairing-laneconfig-unproven";
      return false;
    }
  *why = "window-pairing-dst-alias";
  return false;
}

/* Data-conflict verdict between follower access F and pending event P
   (order not architecturally established).  Returns the stable blocker
   name, or null when provably independent.  */

static const char *
wp_conflict (const wp_event &f, const wp_event &p, function *fn, int *lanecfg)
{
  if ((f.lreg_write & (p.lreg_read | p.lreg_write))
      || (f.lreg_read & p.lreg_write))
    return "window-pairing-lreg-overlap";
  if ((f.cc_write && (p.cc_read || p.cc_write)) || (p.cc_write && f.cc_read))
    return "window-pairing-cc-live";
  if (f.cfg_write || p.cfg_write)
    return "window-pairing-config-overlap";
  if ((f.dst_write && (p.dst_read || p.dst_write))
      || (f.dst_read && p.dst_write))
    {
      const char *why = nullptr;
      if (!wp_dst_disjoint (f, p, fn, lanecfg, &why))
	return why;
    }
  return nullptr;
}

/* Typed constant Dst operands of ORIGIN (post-admission), shifted by
   SHIFT strides.  Fails soft: leaves *EV untyped (any Dst pairing then
   refuses by name).  Mod0 10 (INT32_ALL) is never typed here (Sp-coupled
   addressing; see the header comment).  */

static void
wp_type_dst (rtx_insn *origin, const xtt_effect_set &e, int shift,
	     wp_event *ev)
{
  ev->dst_read = e.dst_mem_read;
  ev->dst_write = e.dst_mem_write;
  ev->dst_typed = false;
  if (!e.dst_mem_read && !e.dst_mem_write)
    return;
  rtx address, mode, addr_mode;
  if (!rvtt_dst_access_operands (origin, e, &address, &mode, &addr_mode)
      || !CONST_INT_P (address) || !CONST_INT_P (mode)
      || INTVAL (mode) == 10)
    return;
  ev->dst_typed = true;
  ev->dst_addr = (int) INTVAL (address) + shift;
  ev->dst_mod0 = (int) INTVAL (mode);
}

/* Complete EV's realized footprint from its origin instruction, its
   launch spec, and its already-decoded SequenceBits fields, with its
   Dst address shifted by SHIFT strides.  Returns false with *WHY set on
   any unproven piece.  */

static bool
wp_event_footprint (const macro_descriptor &desc, const rvtt_macro::caps *c,
		    rtx_insn *origin, int shift, wp_event *ev,
		    const char **why)
{
  xtt_effect_set e = rvtt_insn_effects (origin);
  if (e.opaque)
    {
      *why = "window-pairing-footprint-opaque";
      return false;
    }
  const macro_launch_spec *spec = nullptr;
  for (const macro_launch_spec &l : desc.launches)
    if (l.macro_index == ev->macro_index)
      {
	spec = &l;
	break;
      }
  if (!spec)
    {
      *why = "window-pairing-footprint-opaque";
      return false;
    }

  ev->lreg_read = e.lreg_read;
  ev->lreg_write = e.lreg_write;
  if (ev->kind != rvtt_macro::SEQ_CASE_STORE)
    {
      /* Simple/MAD/Round: VB or VC is overridden with the launch VD; the
	 result register is the launch VD or LReg[16].  */
      ev->lreg_read |= 1u << spec->vd;
      ev->lreg_write |= ev->vd16 ? (1u << 16) : (1u << spec->vd);
    }
  else
    {
      /* Store value register: LReg[16] (VD16), the template's own VD
	 (route bit; the origin already names it), or the launch VD.  */
      if (ev->vd16)
	ev->lreg_read |= 1u << 16;
      else if (!ev->route_vb)
	ev->lreg_read |= 1u << spec->vd;
    }
  if (ev->kind >= rvtt_macro::SEQ_CASE_TEMPLATE0
      && ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0 < desc.n_templates)
    ev->lreg_write |= rvtt_macro::template_hidden_lreg_writes
      (c, desc.templ[ev->kind - rvtt_macro::SEQ_CASE_TEMPLATE0]);

  ev->cc_read = e.cc_read;
  ev->cc_write = e.cc_write;
  ev->cfg_write = e.config_dests_written != 0 || e.addr_mod_slot_write;
  wp_type_dst (origin, e, shift, ev);
  return true;
}

} /* anonymous namespace */

/* Derive the minimal proven inter-row drain for REGION's uniform rows.
   Returns a value in [0, desc.drain_slots]; desc.drain_slots (today's
   bytes) on any refusal, with the blocker named to DUMP.  When the
   verdict is positive but below the full drain, *BOUND_NAME names the
   conflict that bounds it from below (the blocker at one NOP fewer).  */

int
rvtt_macro_interrow_drain_tuned (function *fn, const macro_region &region,
				 const macro_schedule &schedule,
				 const macro_descriptor &desc, FILE *dump,
				 const char **bound_name)
{
  *bound_name = nullptr;
  int full = desc.drain_slots;
  auto refuse = [&] (const char *name) -> int
    {
      rvtt_refuse_by_name (name, dump,
			   "Macro-planner window-pairing-refusal: %s\n",
			   name);
      return full;
    };

  if (full <= 0 || desc.cc.active || region.rows.length () < 2)
    return full;
  if (desc.keep_separator
      || (region.rows[0].separator && !schedule.absorbed_stride))
    return refuse ("window-pairing-separator-unproven");
  for (const macro_row &row : region.rows)
    if (row.insns.length () != schedule.events.length ())
      return refuse ("window-pairing-footprint-opaque");

  /* Rows are isomorphic to row 0 only UNDER A VALUE MAP -- their emitted
     registers and typed operands may differ per row while the derived
     footprints below come from row 0.  The tune is proven only when
     every row's per-position effect sets and typed Dst operands are
     IDENTICAL to row 0's (the pinned-VD calendars this service targets
     are exactly the identical-register shapes); any variance refuses by
     name and keeps the full drain.  */
  for (unsigned r = 1; r < region.rows.length (); ++r)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	rtx_insn *a = region.rows[0].insns[ix];
	rtx_insn *b = region.rows[r].insns[ix];
	xtt_effect_set ea = rvtt_insn_effects (a);
	xtt_effect_set eb = rvtt_insn_effects (b);
	if (ea.opaque || eb.opaque)
	  return refuse ("window-pairing-footprint-opaque");
	if (ea.lreg_read != eb.lreg_read || ea.lreg_write != eb.lreg_write
	    || ea.cc_read != eb.cc_read || ea.cc_write != eb.cc_write
	    || ea.config_dests_written != eb.config_dests_written
	    || ea.addr_mod_slot_write != eb.addr_mod_slot_write
	    || ea.dst_mem_read != eb.dst_mem_read
	    || ea.dst_mem_write != eb.dst_mem_write)
	  return refuse ("window-pairing-row-variance");
	if (ea.dst_mem_read || ea.dst_mem_write)
	  {
	    rtx addr_a, mode_a, am_a, addr_b, mode_b, am_b;
	    bool oa = rvtt_dst_access_operands (a, ea, &addr_a, &mode_a,
						&am_a);
	    bool ob = rvtt_dst_access_operands (b, eb, &addr_b, &mode_b,
						&am_b);
	    if (oa != ob)
	      return refuse ("window-pairing-row-variance");
	    if (oa
		&& (!rtx_equal_p (addr_a, addr_b)
		    || !rtx_equal_p (mode_a, mode_b)
		    || !rtx_equal_p (am_a, am_b)))
	      return refuse ("window-pairing-row-variance");
	  }
      }

  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu
    (TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
     : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR);
  if (!c)
    return refuse ("window-pairing-footprint-opaque");

  /* Issue-word positions in emission order (slots ascending), the same
     mapping the boundary proofs use.  */
  auto_vec<int> word_pos;
  word_pos.safe_grow_cleared (schedule.events.length ());
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    word_pos[ix] = -1;
  int carrier_pos[8];
  for (int m = 0; m != 8; ++m)
    carrier_pos[m] = -1;
  int words_per_row = 0, last_issue = -1;
  for (int slot = 0; slot != schedule.ii; ++slot)
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (!ev.issues_word || ev.slot != slot)
	  continue;
	word_pos[ix] = words_per_row;
	if (ev.is_carrier && ev.macro_index < 8)
	  carrier_pos[ev.macro_index] = words_per_row;
	++words_per_row;
	last_issue = ev.slot;
      }
  if (words_per_row == 0)
    return refuse ("window-pairing-footprint-opaque");
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].issues_word && word_pos[ix] < 0)
      return refuse ("window-pairing-footprint-opaque");
  int last_pos = words_per_row - 1;

  /* Every Dst access of the row must resolve at one counter value with
     the single absorbing advance riding the row's LAST issued word:
     then consecutive rows sit exactly one stride apart (SFPLOAD.md
     functional-model order: the address resolves before the modifier
     applies).  The launches' EMITTED address modes live in the launch
     specs (the origin operands predate absorption); explicit accesses
     are emitted with their own typed operands.  */
  int stride = schedule.absorbed_stride;
  /* Issue-word position of the single absorbing advance.  The compact
     form (absorber on the row's LAST issued word) is the established
     proof; under -mtt-tensix-optimize-window-pairing-stride an absorber
     riding an EARLIER word is admitted and every Dst footprint below is
     rebased by its carrying word's stride phase (0 at or before the
     absorber, 1 after it): the absorber's own access resolves before
     ApplyPartialAddrMod runs (F5) and SFPLOADMACRO-hosted events latch
     their Dst row at the launch word regardless of later advances (L1;
     SFPLOADMACRO.md StoreSubUnit Addr-resolution extra), so a word's
     POSITION relative to the absorber decides which counter value its
     accesses resolved at -- see rvtt-cost.md F5'.  */
  int absorber_pos = last_pos;
  {
    int expected_advances = stride ? 1 : 0;
    int advances = 0;
    for (const macro_launch_spec &l : desc.launches)
      {
	if (l.addr_mode == c->no_increment_addr_mode)
	  continue;
	if (l.addr_mode != c->auto_increment_dst2_addr_mode
	    || l.macro_index >= 8
	    || carrier_pos[l.macro_index] < 0)
	  return refuse ("window-pairing-stride-unproven");
	if (carrier_pos[l.macro_index] != last_pos
	    && !riscv_tt_opt_window_pairing_stride)
	  return refuse ("window-pairing-stride-unproven");
	absorber_pos = carrier_pos[l.macro_index];
	++advances;
      }
    for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
      {
	const macro_event &ev = schedule.events[ix];
	if (ev.realization != macro_event::EXPLICIT_INSN || ev.is_carrier)
	  continue;
	rtx_insn *origin = region.rows[0].insns[ix];
	xtt_effect_set e = rvtt_insn_effects (origin);
	if (e.opaque)
	  return refuse ("window-pairing-footprint-opaque");
	if (!e.dst_mem_read && !e.dst_mem_write)
	  continue;
	rtx address, mode, addr_mode;
	if (!rvtt_dst_access_operands (origin, e, &address, &mode,
				       &addr_mode)
	    || !CONST_INT_P (addr_mode)
	    || INTVAL (addr_mode) != (int) c->no_increment_addr_mode)
	  return refuse ("window-pairing-stride-unproven");
      }
    if (advances != expected_advances)
      return refuse ("window-pairing-stride-unproven");
  }

  /* Stride phase of schedule event IX: 0 when its carrying word sits at
     or before the absorbing word (its Dst address resolved at the
     row-entry counter value), 1 when after it (resolved one stride
     later).  The carrying word is the event's own issued word, or its
     carrier's launch word for launched template slots (Dst row latched
     at launch: L1).  -1 = no provable carrying word (fail closed).
     With the absorber on the last issued word every phase is 0 and the
     arithmetic below is the established compact-form model verbatim.  */
  auto stride_phase = [&] (unsigned ix) -> int
    {
      const macro_event &ev = schedule.events[ix];
      int carry = ev.issues_word ? word_pos[ix]
	: ev.macro_index < 8 ? carrier_pos[ev.macro_index] : -1;
      if (carry < 0)
	return -1;
      return carry > absorber_pos ? 1 : 0;
    };

  /* The staged events, decoded from the descriptor's OWN SequenceBits
     (the derivation that can never drift from what the hardware
     sequences) and mapped back to their origin row instructions through
     the schedule's (macro, realized-sub-unit) key.  Every launched
     schedule event must be accounted for exactly once; the decoded
     horizon must equal the descriptor's emitted drain figure.  */
  auto_vec<wp_event> staged;
  unsigned launched_total = 0;
  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
    if (schedule.events[ix].realization
	== macro_event::LAUNCHED_TEMPLATE_SLOT)
      ++launched_total;
  int max_pend = 0;
  for (unsigned m = 0; m != desc.n_seq && m != 8; ++m)
    {
      uint8_t bytes[4];
      rvtt_macro::decompose_sequence_word (desc.seq[m], bytes);
      for (int i = 0; i != 4; ++i)
	{
	  unsigned kind, delay;
	  bool vd16, route_vb;
	  if (!rvtt_macro::decode_sequence_bits (bytes[i], &kind, &delay,
						 &vd16, &route_vb))
	    return refuse ("window-pairing-delay-unproven");
	  if (kind == rvtt_macro::SEQ_CASE_SKIP
	      || kind == rvtt_macro::SEQ_CASE_NOP)
	    continue;
	  if (carrier_pos[m] < 0)
	    return refuse ("window-pairing-footprint-opaque");
	  xtt_subunit_t su = i == 0 ? XTT_SU_SIMPLE
	    : i == 1 ? XTT_SU_MAD : i == 2 ? XTT_SU_ROUND : XTT_SU_STORE;
	  int found = -1;
	  for (unsigned ix = 0; ix != schedule.events.length (); ++ix)
	    {
	      const macro_event &ev = schedule.events[ix];
	      if (ev.realization != macro_event::LAUNCHED_TEMPLATE_SLOT
		  || ev.macro_index != m)
		continue;
	      if ((xtt_subunit_t) rvtt_macro_hosted_subunit
		    (region.rows[0].insns[ix]) != su)
		continue;
	      if (found >= 0)
		return refuse ("window-pairing-footprint-opaque");
	      found = (int) ix;
	    }
	  if (found < 0)
	    return refuse ("window-pairing-footprint-opaque");
	  wp_event ev;
	  memset (&ev, 0, sizeof (ev));
	  ev.sched_ix = (unsigned) found;
	  ev.macro_index = m;
	  ev.subunit = su;
	  ev.kind = kind;
	  ev.vd16 = vd16;
	  ev.route_vb = route_vb;
	  ev.exec = carrier_pos[m] + 1 + (int) delay;
	  if (ev.exec - last_pos > max_pend)
	    max_pend = ev.exec - last_pos;
	  staged.safe_push (ev);
	}
    }
  if (staged.length () != launched_total)
    return refuse ("window-pairing-footprint-opaque");
  if (max_pend != full)
    return refuse ("window-pairing-delay-unproven");

  /* Pending horizon: the staged events still in flight past the row's
     last issued word, with their realized footprints (unshifted -- row
     r's own addresses).  */
  auto_vec<wp_event> pending;
  for (const wp_event &sv : staged)
    if (sv.exec > last_pos)
      {
	wp_event p = sv;
	p.exec = sv.exec - last_pos;	/* boundary-relative */
	int phase = stride_phase (sv.sched_ix);
	if (phase < 0)
	  return refuse ("window-pairing-stride-unproven");
	const char *why = nullptr;
	if (!wp_event_footprint (desc, c, region.rows[0].insns[p.sched_ix],
				 stride * phase, &p, &why))
	  return refuse (why);
	pending.safe_push (p);
      }

  /* Ascending search for the smallest admissible spacing.  */
  int lanecfg = -1;
  const char *blocker_at[8] = {};
  int best = full;
  for (int n = 0; n < full; ++n)
    {
      const char *blocker = nullptr;
      for (unsigned j = 1; !blocker; ++j)
	{
	  /* Follower row j at credit n: its first word issues at
	     boundary-relative slot base+1.  Beyond the horizon every
	     later access clears by time (H1+H2); run-tail rows only add
	     the full run-end drain after them (more slack).  */
	  int base = n + (int) (j - 1) * (words_per_row + n);
	  if (base + 1 > full)
	    break;
	  /* Front-end issued words: ordered after every pending
	     writeback at or past their issue slot (retire-before-issue
	     admits equality).  */
	  for (unsigned ix = 0; ix != schedule.events.length () && !blocker;
	       ++ix)
	    {
	      const macro_event &ev = schedule.events[ix];
	      if (!ev.issues_word)
		continue;
	      int t = base + 1 + word_pos[ix];
	      if (t >= full)
		continue;
	      wp_event f;
	      memset (&f, 0, sizeof (f));
	      f.exec = t;
	      xtt_effect_set e = rvtt_insn_effects (region.rows[0].insns[ix]);
	      if (e.opaque)
		{
		  blocker = "window-pairing-footprint-opaque";
		  break;
		}
	      int fphase = stride_phase (ix);
	      if (fphase < 0)
		{
		  blocker = "window-pairing-stride-unproven";
		  break;
		}
	      if (ev.is_carrier)
		{
		  /* The launch's own front-end SFPLOAD: writes the
		     launch VD, reads its carried Dst address (the
		     store-only carrier's sacrificial load included).  */
		  const macro_launch_spec *spec = nullptr;
		  for (const macro_launch_spec &l : desc.launches)
		    if (l.macro_index == ev.macro_index)
		      spec = &l;
		  if (!spec)
		    {
		      blocker = "window-pairing-footprint-opaque";
		      break;
		    }
		  f.lreg_write = 1u << spec->vd;
		  xtt_effect_set fe = e;
		  fe.dst_mem_read = true;
		  fe.dst_mem_write = false;
		  wp_type_dst (region.rows[0].insns[ix], fe,
			       stride * ((int) j + fphase), &f);
		}
	      else
		{
		  f.lreg_read = e.lreg_read;
		  f.lreg_write = e.lreg_write;
		  /* The emission may retarget an explicit reload to its
		     planned register (the template src field / the
		     coalesced launch VD); the footprint takes the
		     union.  */
		  if (ev.realization == macro_event::EXPLICIT_INSN
		      && e.dst_mem_read)
		    for (unsigned jx = 0; jx != schedule.events.length ();
			 ++jx)
		      {
			const macro_event &cons = schedule.events[jx];
			xtt_effect_set ce
			  = rvtt_insn_effects (region.rows[0].insns[jx]);
			if (ce.opaque || !(ce.lreg_read & e.lreg_write))
			  continue;
			if (cons.realization == macro_event::CC_COALESCED
			    && !desc.launches.is_empty ())
			  f.lreg_write |= 1u << desc.launches[0].vd;
			else if (cons.realization
				   == macro_event::LAUNCHED_TEMPLATE_SLOT
				 && !cons.is_store
				 && cons.template_id < desc.n_templates)
			  {
			    rvtt_macro::template_spec tspec;
			    if (rvtt_macro::decode_template
				  (desc.templ[cons.template_id], &tspec)
				&& tspec.src_c)
			      f.lreg_write |= 1u << tspec.src_c;
			  }
		      }
		  f.cc_read = e.cc_read;
		  f.cc_write = e.cc_write;
		  f.cfg_write = e.config_dests_written != 0
		    || e.addr_mod_slot_write;
		  wp_type_dst (region.rows[0].insns[ix], e,
			       stride * ((int) j + fphase), &f);
		}
	      for (const wp_event &p : pending)
		{
		  if (p.exec <= t)
		    continue;		/* retired before this issue */
		  blocker = wp_conflict (f, p, fn, &lanecfg);
		  if (blocker)
		    break;
		}
	    }
	  /* Launched events of the follower row: strict ordering against
	     every pending event; same-cycle admits only distinct
	     sub-units with disjoint data (the occupancy rule).  */
	  for (unsigned sx = 0; sx != staged.length () && !blocker; ++sx)
	    {
	      int exec_f = base + 1 + staged[sx].exec;
	      if (exec_f > full)
		continue;	/* strictly after every pending writeback */
	      wp_event f = staged[sx];
	      f.exec = exec_f;
	      int fphase = stride_phase (f.sched_ix);
	      if (fphase < 0)
		{
		  blocker = "window-pairing-stride-unproven";
		  break;
		}
	      const char *why = nullptr;
	      if (!wp_event_footprint (desc, c,
				       region.rows[0].insns[f.sched_ix],
				       stride * ((int) j + fphase), &f, &why))
		{
		  blocker = why;
		  break;
		}
	      for (const wp_event &p : pending)
		{
		  if (exec_f > p.exec)
		    continue;		/* order preserved */
		  if (exec_f == p.exec && f.subunit == p.subunit)
		    {
		      blocker = "window-pairing-subunit-collision";
		      break;
		    }
		  blocker = wp_conflict (f, p, fn, &lanecfg);
		  if (blocker)
		    break;
		}
	    }
	}
      if (n < 8)
	blocker_at[n] = blocker;
      if (!blocker)
	{
	  best = n;
	  break;
	}
    }

  if (best == full)
    return refuse (blocker_at[0] ? blocker_at[0]
		   : "window-pairing-horizon-spill");
  if (best > 0)
    *bound_name = blocker_at[best - 1];
  return best;
}
