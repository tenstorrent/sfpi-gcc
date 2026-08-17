/* Macro-planner DAG scheduling over typed effect sets (Layer 3).
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

#ifndef GCC_RVTT_MACRO_SCHED_H
#define GCC_RVTT_MACRO_SCHED_H

#include "rvtt-effects.h"
#include "rvtt-macro-region.h"

/* The schedule of one region row: events realized either as launched
   template-slot events or as explicit instructions, derived from the row
   dataflow, the Layer-1 attributes, and the Layer-4 capability tables --
   never from opcode calendars.  Deterministic: identical input produces
   identical output.  Analysis-only at this stage.  */

struct macro_event
{
  rtx_insn *origin;		/* region insn this event realizes     */
  xtt_subunit_t subunit;
  /* CC_COALESCED (WP9): a lane-merge value event (a CC-predicated
     operation that reads its own destination) realized by the
     calendar's predicated-overwrite dataflow -- the shared launch VD
     receives every payload load and the post-visibility load is the
     lane-predicated write -- so the event issues no word and occupies
     no template slot.  The dataflow and timing proof obligations live
     in descriptor synthesis and the Layer-7 verifier.  */
  enum realization_t { LAUNCHED_TEMPLATE_SLOT, EXPLICIT_INSN,
		       CC_COALESCED } realization;
  int  slot;			/* issue slot of the carrying word     */
  int  programmed_delay;	/* slots after the carrier; -1 unknown */
  unsigned lreg_dest;		/* write-port accounting	       */
  unsigned template_id;		/* valid when LAUNCHED (else ~0u)      */
  unsigned seq_slot;		/* sequence position within the macro  */
  unsigned macro_index;		/* carrier macro (LAUNCHED / carriers) */
  bool is_store;
  bool issues_word;		/* carrier launch or explicit issue    */
  bool is_carrier;		/* the Dst access carried by a launch  */
  /* WP10 compact CC calendar: this explicit trailing load absorbs the
     row's typed Dst stride through its own auto-increment address mode
     (the tables' owned address-modifier slot), replacing the deleted
     separator.  */
  bool absorbs_stride;
};

struct macro_schedule
{
  int ii;			/* initiation interval (slots per row) */
  vec<macro_event> events;
  unsigned launches;		/* issued launch words per row	       */
  unsigned explicit_issues;	/* issued explicit words per row       */
  unsigned launched_events;	/* launched sequence events per row    */
  int drain_slots;		/* -1 when a delay is unproven	       */
  uint32_t lreg_footprint;
  bool alternating_vd;		/* derived (conservatively), not assumed */
  int absorbed_stride;		/* Dst delta absorbed by auto-inc mode */
  /* WP10: the absorber is the trailing EXPLICIT load (compact CC
     calendar) rather than the last carrier; launches keep the
     no-increment mode.  */
  bool absorb_into_explicit;
  const char *refusal;		/* stable name; null = no refusal      */
};

/* Stable refusal vocabulary (append-only; names are dump API).  The
   target-macro-encoding-unproven name is owned by the capability
   tables.  */
extern const char *macro_sched_refusal_event_delay_unproven;
extern const char *macro_sched_refusal_sequence_encoding_unproven;
extern const char *macro_sched_refusal_template_capacity_exceeded;
extern const char *macro_sched_refusal_port_conflict;
extern const char *macro_sched_refusal_latency_violation;
/* A CC-dependent value event in a predicate-writing row whose
   realization (template hosting or coalescing) cannot be proven; shared
   spelling with the descriptor layer's CC refusal.  */
extern const char *macro_sched_refusal_cc_template_unproved;
/* The 2026-08-17 silicon adjudication's separator-kept mis-select is
   no longer a scheduler-level structural refusal
   (cc-separator-kept-silicon-unproven, retired): the corrected CRAQ
   delivery model (craq-sim 9f324140) pinned the ARCHITECTURAL cause --
   the store's lane predicate is live at execution and the 4-slot
   calendar retires its all-lanes restore in the store's own cycle --
   so the descriptor CC model now refuses such schedules by the
   architectural name cc-restore-store-race (rvtt-macro-desc.h),
   derived from the calendar's slots and proven delays rather than
   from the kept-separator structure.  */

/* Derive and dump the schedule of REGION's canonical row for carrier
   grouping CANDIDATE.  The grouping search is deterministic and
   ascending: candidate 0 shares a carrier between Dst accesses with
   equal typed address operands (maximal sharing); candidate 1 demotes
   every Dst store to its own single-access carrier (the delayed-store
   slot) and exists only when candidate 0 merged a store with another
   access.  The caller iterates candidates in order and accepts the
   first whose descriptor synthesis proves; when the search is
   exhausted the region refuses byte-identically.  Returns true when a
   schedule structure was derived (its refusal field still names any
   commitment blocker); false when scheduling could not begin (no
   capability table) or CANDIDATE names no distinct grouping.  Never
   mutates the function.  */
extern bool rvtt_macro_schedule_region (const macro_region &region,
					macro_schedule *out, FILE *dump,
					unsigned candidate = 0);
extern void rvtt_macro_schedule_release (macro_schedule *);

#endif /* GCC_RVTT_MACRO_SCHED_H */
