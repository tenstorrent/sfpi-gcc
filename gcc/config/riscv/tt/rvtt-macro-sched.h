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
  enum realization_t { LAUNCHED_TEMPLATE_SLOT, EXPLICIT_INSN } realization;
  int  slot;			/* issue slot of the carrying word     */
  int  programmed_delay;	/* slots after the carrier; -1 unknown */
  unsigned lreg_dest;		/* write-port accounting	       */
  unsigned template_id;		/* valid when LAUNCHED (else ~0u)      */
  unsigned seq_slot;		/* sequence position within the macro  */
  unsigned macro_index;		/* carrier macro (LAUNCHED / carriers) */
  bool is_store;
  bool issues_word;		/* carrier launch or explicit issue    */
  bool is_carrier;		/* the Dst access carried by a launch  */
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

/* Derive and dump the schedule of REGION's canonical row.  Returns true
   when a schedule structure was derived (its refusal field still names
   any commitment blocker); false when scheduling could not begin (no
   capability table).  Never mutates the function.  */
extern bool rvtt_macro_schedule_region (const macro_region &region,
					macro_schedule *out, FILE *dump);
extern void rvtt_macro_schedule_release (macro_schedule *);

#endif /* GCC_RVTT_MACRO_SCHED_H */
