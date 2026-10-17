/* Macro-planner timing-calendar derivation core -- standalone.
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

/* Derivation of the per-macro sequence words and per-event delays of a
   macro calendar from a scheduled row, using only the architectural facts
   in the capability tables (rvtt-macro-tables.h).  This header depends only
   on those tables and <stdint.h>, so it is shared by descriptor synthesis,
   the descriptor verifier and the unit test rvtt-macro-derive-test.cc,
   which checks it against the sequence words of every calendar in the
   tables.

   The inputs are the structure of the schedule and the outputs are packed
   words; no IR is inspected.  On failure nothing is derived and a stable
   refusal name is returned.  */

#ifndef GCC_RVTT_MACRO_DERIVE_CORE_H
#define GCC_RVTT_MACRO_DERIVE_CORE_H

#include "rvtt-macro-tables.h"

namespace rvtt_macro_derive
{

const unsigned MAX_EVENTS = 8;

/* Refusal names, used in dumps.  */
inline const char *refusal_placement () { return "subunit-placement-unproven"; }
inline const char *refusal_store_source ()
{ return "store-source-unreachable"; }
inline const char *refusal_hazard () { return "sequence-derivation-hazard"; }
inline const char *refusal_delay_range () { return "delay-range-exceeded"; }
inline const char *refusal_delay_model () { return "delay-model-unproven"; }
inline const char *refusal_capacity () { return "template-capacity-exceeded"; }

/* One row member executed by the macro calendar: a value event or the
   delayed store.  Events are in program order and dep_mask refers only to
   earlier events.  */
struct event_spec
{
  uint8_t opcode;	/* template opcode byte; unused for the store  */
  bool is_store;
  int macro_index;
  int carrier_slot;	/* issue slot of the carrying launch	       */
  uint8_t dep_mask;	/* events producing its inputs, one bit each   */
  /* Latest issue slot whose instruction writes one of this event's
     input registers (its own carrier's load, another carrier's load,
     or an explicit issue); -1 when every input is an event result.  */
  int latest_issued_input_slot;
  /* The event reads the physical register its carrier's launch loads
     (the launch-VD register; the value may be the load's or an earlier
     same-register event result).  */
  bool reads_carrier_vd_reg;
  uint8_t planned_src_c;	/* template VC field; 0 = unused       */
  /* Template sharing: events with identical template words may share one
     InstructionTemplate slot.  0 gives the event its own slot; a nonzero
     value K assigns template index K-1.  The caller must give equal keys
     exactly to events with equal template words.  */
  uint8_t template_key;
  /* Template modifier.  Store-source routing uses it to distinguish
     opcode realizations whose LReg16 evaluator support differs by mode.  */
  uint8_t template_mod1;
  /* Template imm12 field (always 0 for the swap class).  Kept here so
     that the derivation and the descriptor encoder see the same value.  */
  uint16_t template_imm12;
  /* Hazards against the row's explicit issues and earlier events, for
     events that share registers with explicit row members.  The slot
     values are slot + 1, with 0 meaning no constraint:

     - war_floor_slot_p1: the event writes a register an earlier explicit
       issue reads; the write must retire after that read
       (exec >= slot + 1).
     - issue_consumer_slot_p1: a later explicit issue reads the event's
       result, which must be available by then
       (exec + (latency - 1) <= slot; scheduled events retire before
       issues in the same cycle).
     - issue_overwrite_slot_p1: a later explicit issue overwrites the
       register the event writes; the event must retire no later than
       that cycle (exec <= slot).
     - anti_dep_mask: earlier events that read a register this event
       writes.  Events in the same cycle read the values from before any
       of their writes, so exec >= their exec suffices.  */
  int16_t war_floor_slot_p1;
  int16_t issue_consumer_slot_p1;
  int16_t issue_overwrite_slot_p1;
  uint8_t anti_dep_mask;
};

/* An explicitly issued instruction inside the row's window.  */
struct explicit_issue
{
  int slot;
  unsigned unit_mask;	/* sub-unit byte indices it executes on; 0 =
			   load-class or non-SFPU (never conflicts)    */
};

struct row_spec
{
  event_spec events[MAX_EVENTS];
  unsigned n_events;
  unsigned n_macros;
  int macro_slot[4];		/* issue slot per macro carrier	       */
  int ii;			/* row initiation interval	       */
  int last_issue_slot;
  /* 16 is the architectural sequencer limit; longer rows are refused.  */
  explicit_issue explicits[16];
  unsigned n_explicits;
  bool vd_alternates;		/* value-carrier VD rewrite period is
				   two rows (else one)		       */
  bool window_all_sfpu;		/* every issue slot in the window is an
				   SFPU-class instruction	       */
  int store_event;		/* index of the delayed store	       */
  int store_producer;		/* event producing the store's data;
				   -1 = the launch-VD register written
				   only by issued loads		       */
  /* Only when store_producer < 0: the last issue slot writing the store's
     source register, and the slot of its next overwrite (typically the
     first writing slot plus the period).  */
  int store_input_last_slot;
  int store_vd_next_write;
  unsigned max_templates;	/* available template slots (<= 4)     */
  unsigned max_macros;		/* available sequence slots (<= 4)     */
};

struct derived_calendar
{
  uint32_t seq_words[4];
  unsigned unit_of[MAX_EVENTS];	/* SEQ_UNIT_*			       */
  unsigned delay_of[MAX_EVENTS];
  int exec_of[MAX_EVENTS];
  int template_index_of[MAX_EVENTS];	/* -1 for the store	       */
  bool writes_l16[MAX_EVENTS];
  bool route_vb[MAX_EVENTS];
  bool store_reads_l16;
  bool has_staging_copy;
  int staging_macro;
  unsigned staging_delay;
  int staging_exec;
  int staging_template_index;
  unsigned n_templates;
  unsigned delay_kind_mask;
  int drain;
  const char *refusal;		/* null on success		       */
};

/* Return true if A and B are congruent modulo II, i.e. events at cycles A
   and B execute in the same cycle for some pair of row instances of the
   periodic schedule.  */
inline bool
same_residue (int a, int b, int ii)
{
  int d = (a - b) % ii;
  return d == 0;
}

/* Derive the per-macro sequence words and per-event delays for ROW with
   capability table C into *OUT.  Place every value event on a legal
   sub-unit and template slot; decide how the delayed store reads its
   source (via LReg16, the same macro's VD, or a staging copy); compute
   execution cycles from the dependences, delaying events to avoid
   structural hazards; then check delay ranges, value lifetimes across
   row instances, delay counting and operand routing, and pack the
   sequence bytes and the drain.  Return true on success, otherwise false
   with the refusal name in OUT->refusal.  */

inline bool
derive_calendar (const rvtt_macro::caps *c, const row_spec &row,
		 derived_calendar *out)
{
  using namespace rvtt_macro;

  for (unsigned i = 0; i < 4; ++i)
    out->seq_words[i] = 0;
  for (unsigned i = 0; i < MAX_EVENTS; ++i)
    {
      out->unit_of[i] = 0;
      out->delay_of[i] = 0;
      out->exec_of[i] = -1;
      out->template_index_of[i] = -1;
      out->writes_l16[i] = false;
      out->route_vb[i] = false;
    }
  out->store_reads_l16 = false;
  out->has_staging_copy = false;
  out->staging_macro = -1;
  out->staging_delay = 0;
  out->staging_exec = -1;
  out->staging_template_index = -1;
  out->n_templates = 0;
  out->delay_kind_mask = 0;
  out->drain = -1;
  out->refusal = nullptr;

  if (!c || row.n_events == 0 || row.n_events > MAX_EVENTS
      || row.n_macros == 0 || row.n_macros > 4 || row.ii <= 0
      || row.store_event < 0
      || (unsigned) row.store_event >= row.n_events
      || row.n_macros > row.max_macros)
    {
      out->refusal = refusal_capacity ();
      return false;
    }

  /* 1. Sub-unit placement: put each value event on a sub-unit its
     template opcode may execute on, at most one event per sub-unit of
     each macro.  */
  bool unit_taken[4][4] = {};
  unsigned next_template = 0;
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      const event_spec &ev = row.events[e];
      if (ev.macro_index < 0 || ev.macro_index >= (int) row.n_macros)
	{
	  out->refusal = refusal_capacity ();
	  return false;
	}
      if (ev.is_store)
	{
	  if (unit_taken[ev.macro_index][SEQ_UNIT_STORE])
	    {
	      out->refusal = refusal_placement ();
	      return false;
	    }
	  unit_taken[ev.macro_index][SEQ_UNIT_STORE] = true;
	  out->unit_of[e] = SEQ_UNIT_STORE;
	  continue;
	}
      unsigned mask = subunit_legal_mask (c, ev.opcode);
      unsigned unit = 4;
      for (unsigned u = 0; u < 3; ++u)
	if (((mask >> u) & 1) && !unit_taken[ev.macro_index][u])
	  {
	    unit = u;
	    break;
	  }
      if (unit == 4)
	{
	  out->refusal = refusal_placement ();
	  return false;
	}
      unit_taken[ev.macro_index][unit] = true;
      out->unit_of[e] = unit;
      /* Template slot: shared by key, otherwise one per event.
	 next_template counts distinct slots.  */
      if (ev.template_key)
	{
	  unsigned idx = ev.template_key - 1;
	  out->template_index_of[e] = (int) idx;
	  if (idx + 1 > next_template)
	    next_template = idx + 1;
	}
      else
	out->template_index_of[e] = next_template++;
    }

  /* 2. Store source.  The delayed store can read only its own launch's
     VD register or LReg16; a producer that cannot write either needs a
     staging copy.  */
  const int st = row.store_event;
  const int sp = row.store_producer;
  int l16_writer = -1;		/* event or staging copy writing L16   */
  bool store_via_vd_direct = false;
  if (sp >= 0)
    {
      if ((unsigned) sp >= row.n_events || row.events[sp].is_store)
	{
	  out->refusal = refusal_store_source ();
	  return false;
	}
      bool sole_consumer = true;
      for (unsigned e = 0; e < row.n_events; ++e)
	if ((int) e != st && (row.events[e].dep_mask >> sp) & 1)
	  sole_consumer = false;
      /* LReg16 has no encodable VD, so an event writing it executes
	 through the direct template evaluator, which is only known to be
	 correct for some opcodes (opcode_l16_target_proven); SFPABS, for
	 one, computes wrong results on Blackhole this way.  Other opcodes
	 use the VD or the staging copy instead.  */
      if (!opcode_reads_vd (c, row.events[sp].opcode) && sole_consumer
	  && opcode_l16_target_proven (c, row.events[sp].opcode,
				     row.events[sp].template_mod1))
	{
	  out->writes_l16[sp] = true;
	  out->store_reads_l16 = true;
	  l16_writer = sp;
	}
      else if (row.events[sp].macro_index == row.events[st].macro_index)
	store_via_vd_direct = true;
      else
	{
	  /* A staging copy on the producer's macro.  */
	  staging_copy_facts copy;
	  if (!staging_copy_realization (c, &copy)
	      || unit_taken[row.events[sp].macro_index][copy.seq_unit])
	    {
	      out->refusal = refusal_store_source ();
	      return false;
	    }
	  unit_taken[row.events[sp].macro_index][copy.seq_unit] = true;
	  out->has_staging_copy = true;
	  out->staging_macro = row.events[sp].macro_index;
	  out->staging_template_index = next_template++;
	  out->store_reads_l16 = true;
	  l16_writer = (int) MAX_EVENTS;	/* the copy	       */
	}
    }
  out->n_templates = next_template;
  if (next_template > row.max_templates)
    {
      out->refusal = refusal_capacity ();
      return false;
    }
  /* Allow one LReg16 writer per row; a second would race with the
     store's read across row instances.  */
  {
    unsigned writers = out->has_staging_copy ? 1 : 0;
    for (unsigned e = 0; e < row.n_events; ++e)
      writers += out->writes_l16[e] ? 1 : 0;
    if (writers > 1)
      {
	out->refusal = refusal_store_source ();
	return false;
      }
  }
  /* Compute events cannot read LReg16 (no encodable index).  */
  for (unsigned e = 0; e < row.n_events; ++e)
    for (unsigned d = 0; d < e; ++d)
      if (((row.events[e].dep_mask >> d) & 1) && out->writes_l16[d]
	  && (int) e != st)
	{
	  out->refusal = refusal_store_source ();
	  return false;
	}

  /* 3. Execution cycles: iterate the dependence constraints to a
     fixpoint, then delay events one cycle at a time (a bounded number of
     times) to resolve sub-unit, VD16, SFPSWAP-adjacency, explicit-issue
     and write-set hazards.  */
  int exec[MAX_EVENTS + 1];	/* [MAX_EVENTS] = staging copy	       */
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      const event_spec &ev = row.events[e];
      int start = ev.carrier_slot + 1;
      if (ev.latest_issued_input_slot >= 0
	  && ev.latest_issued_input_slot + 1 > start)
	start = ev.latest_issued_input_slot + 1;
      if (ev.is_store && sp < 0
	  && row.store_input_last_slot + 1 > start)
	start = row.store_input_last_slot + 1;
      if (ev.war_floor_slot_p1 > start)
	start = ev.war_floor_slot_p1;
      exec[e] = start;
    }
  exec[MAX_EVENTS] = out->has_staging_copy
    ? row.macro_slot[out->staging_macro] + 1 : -1;

  const int period = row.ii * (row.vd_alternates ? 2 : 1);
  staging_copy_facts copy_facts;
  copy_facts.opcode = 0;
  copy_facts.mod1 = 0;
  copy_facts.seq_unit = SEQ_UNIT_ROUND;
  if (out->has_staging_copy && !staging_copy_realization (c, &copy_facts))
    {
      out->refusal = refusal_store_source ();
      return false;
    }
  for (unsigned pass = 0; pass < 64; ++pass)
    {
      /* Dependence fixpoint (execs only grow).  */
      bool changed = true;
      unsigned guard = 0;
      while (changed && guard++ < 256)
	{
	  changed = false;
	  for (unsigned e = 0; e < row.n_events; ++e)
	    for (unsigned d = 0; d < e; ++d)
	      {
		if ((row.events[e].dep_mask >> d) & 1)
		  {
		    int ready = exec[d]
		      + (int) subunit_result_latency (out->unit_of[d]);
		    if (exec[e] < ready)
		      {
			exec[e] = ready;
			changed = true;
		      }
		  }
		if ((row.events[e].anti_dep_mask >> d) & 1
		    && exec[e] < exec[d])
		  {
		    exec[e] = exec[d];
		    changed = true;
		  }
	      }
	  if (out->has_staging_copy)
	    {
	      int ready = exec[sp]
		+ (int) subunit_result_latency (out->unit_of[sp]);
	      if (exec[MAX_EVENTS] < ready)
		{
		  exec[MAX_EVENTS] = ready;
		  changed = true;
		}
	      int st_ready = exec[MAX_EVENTS]
		+ (int) subunit_result_latency (copy_facts.seq_unit);
	      if (exec[st] < st_ready)
		{
		  exec[st] = st_ready;
		  changed = true;
		}
	    }
	  else if (sp >= 0)
	    {
	      int st_ready = exec[sp]
		+ (int) subunit_result_latency (out->unit_of[sp]);
	      if (exec[st] < st_ready)
		{
		  exec[st] = st_ready;
		  changed = true;
		}
	    }
	}

      /* Hazard scan.  On a violation, delay the youngest participant a
	 cycle and retry; the delay field width bounds the growth.  */
      int bump = -1;		/* index; MAX_EVENTS = staging copy    */

      /* Collect the schedulable items: (unit, exec, vd16, is_swap).  */
      struct item { unsigned unit; int exec; bool vd16; bool swap; };
      item items[MAX_EVENTS + 1];
      unsigned n_items = 0;
      unsigned item_index[MAX_EVENTS + 1];
      for (unsigned e = 0; e < row.n_events; ++e)
	{
	  items[n_items].unit = out->unit_of[e];
	  items[n_items].exec = exec[e];
	  items[n_items].vd16 = out->writes_l16[e]
	    || (row.events[e].is_store && out->store_reads_l16);
	  items[n_items].swap = !row.events[e].is_store
	    && opcode_needs_swap_adjacency (c, row.events[e].opcode);
	  item_index[n_items++] = e;
	}
      if (out->has_staging_copy)
	{
	  items[n_items].unit = copy_facts.seq_unit;
	  items[n_items].exec = exec[MAX_EVENTS];
	  items[n_items].vd16 = true;
	  items[n_items].swap = false;
	  item_index[n_items++] = MAX_EVENTS;
	}

      for (unsigned i = 0; i < n_items && bump < 0; ++i)
	for (unsigned j = i + 1; j < n_items && bump < 0; ++j)
	  {
	    if (!same_residue (items[i].exec, items[j].exec, row.ii))
	      continue;
	    /* One event per sub-unit per cycle, across row instances.  */
	    if (items[i].unit == items[j].unit)
	      bump = (int) item_index[j];
	    /* A Simple/Round pair can share a cycle only if exactly one
	       of them uses VD16.  */
	    else if (((items[i].unit == SEQ_UNIT_SIMPLE
		       && items[j].unit == SEQ_UNIT_ROUND)
		      || (items[i].unit == SEQ_UNIT_ROUND
			  && items[j].unit == SEQ_UNIT_SIMPLE))
		     && items[i].vd16 == items[j].vd16)
	      bump = (int) item_index[j];
	    /* Same-cycle L16 write-set overlap.  */
	    else if (items[i].vd16 && items[j].vd16
		     && items[i].unit != SEQ_UNIT_STORE
		     && items[j].unit != SEQ_UNIT_STORE)
	      bump = (int) item_index[j];
	  }

      /* SFPSWAP needs MAD idle in its cycle and Simple and Round idle
	 in the next, including against its own next-row instance.  */
      for (unsigned i = 0; i < n_items && bump < 0; ++i)
	{
	  if (!items[i].swap)
	    continue;
	  if (row.ii == 1)
	    {
	      out->refusal = refusal_hazard ();
	      return false;	/* conflicts with its own next instance  */
	    }
	  for (unsigned j = 0; j < n_items && bump < 0; ++j)
	    {
	      if (j == i)
		continue;
	      if (items[j].unit == SEQ_UNIT_MAD
		  && same_residue (items[j].exec, items[i].exec, row.ii))
		bump = (int) item_index[j];
	      else if ((items[j].unit == SEQ_UNIT_SIMPLE
			|| items[j].unit == SEQ_UNIT_ROUND)
		       && same_residue (items[j].exec, items[i].exec + 1,
					row.ii))
		bump = (int) item_index[j];
	    }
	}

      /* A scheduled event on the sub-unit of an explicitly issued
	 instruction in its issue cycle silently discards the instruction,
	 so keep them apart.  */
      for (unsigned i = 0; i < n_items && bump < 0; ++i)
	for (unsigned x = 0; x < row.n_explicits && bump < 0; ++x)
	  if (((row.explicits[x].unit_mask >> items[i].unit) & 1)
	      && same_residue (items[i].exec, row.explicits[x].slot,
			       row.ii))
	    bump = (int) item_index[i];

      if (bump < 0)
	break;			/* hazard-free		       */
      if (pass == 63)
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
      if (bump == (int) MAX_EVENTS)
	++exec[MAX_EVENTS];
      else
	++exec[bump];
    }

  /* Deadlines from explicit issues.  Cycles above are the earliest
     feasible ones, so a missed deadline cannot be fixed.  */
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      const event_spec &ev = row.events[e];
      if (ev.is_store)
	continue;
      int lat = (int) subunit_result_latency (out->unit_of[e]);
      if (ev.issue_consumer_slot_p1
	  && exec[e] + (lat > 0 ? lat - 1 : 0)
	     > ev.issue_consumer_slot_p1 - 1)
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
      if (ev.issue_overwrite_slot_p1
	  && exec[e] > ev.issue_overwrite_slot_p1 - 1)
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
    }

  /* Delay ranges.  */
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      int delay = exec[e] - row.events[e].carrier_slot - 1;
      if (delay < 0 || delay > (int) SEQ_MAX_DELAY)
	{
	  out->refusal = refusal_delay_range ();
	  return false;
	}
      out->exec_of[e] = exec[e];
      out->delay_of[e] = (unsigned) delay;
    }
  if (out->has_staging_copy)
    {
      int delay = exec[MAX_EVENTS] - row.macro_slot[out->staging_macro] - 1;
      if (delay < 0 || delay > (int) SEQ_MAX_DELAY)
	{
	  out->refusal = refusal_delay_range ();
	  return false;
	}
      out->staging_exec = exec[MAX_EVENTS];
      out->staging_delay = (unsigned) delay;
    }

  /* Value lifetimes across row instances.  A rewrite in the same cycle
     is allowed: scheduled events retire before issues in the same cycle,
     and events in the same cycle read the values from before their
     writes, so a store executing exactly when the next row instance
     rewrites its source still reads this row's value.  */
  if (out->store_reads_l16)
    {
      int writer_exec = l16_writer == (int) MAX_EVENTS
	? exec[MAX_EVENTS] : exec[l16_writer];
      /* The next row instance rewrites LReg16 one interval later.  */
      if (!(exec[st] > writer_exec && exec[st] <= writer_exec + row.ii))
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
    }
  else if (store_via_vd_direct)
    {
      /* The store's macro VD is reloaded by its own launch every
	 PERIOD slots.  */
      if (!(exec[st] > exec[sp]
	    && exec[st] <= row.macro_slot[row.events[st].macro_index]
			   + period))
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
    }
  else if (sp < 0)
    {
      if (!(exec[st] > row.store_input_last_slot
	    && exec[st] <= row.store_vd_next_write))
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}
    }

  /* Fixed launch VD: the calendar keeps the row's own load destinations
     because explicit row members use them.  The next row instance's
     launch rewrites the register one interval later, so every event
     reading it must execute no later than that (the same cycle is fine,
     as above).  Alternating calendars satisfy this by construction.  */
  if (!row.vd_alternates)
    for (unsigned e = 0; e < row.n_events; ++e)
      if (row.events[e].reads_carrier_vd_reg && !row.events[e].is_store
	  && exec[e] > row.macro_slot[row.events[e].macro_index] + row.ii)
	{
	  out->refusal = refusal_hazard ();
	  return false;
	}

  /* An event that uses a value issued after its own launch needs its
     sub-unit to count instructions for the delay, which is only correct
     when every instruction issued in the window is an SFPU one.  */
  for (unsigned e = 0; e < row.n_events; ++e)
    if (row.events[e].latest_issued_input_slot
	> row.events[e].carrier_slot)
      out->delay_kind_mask |= 1u << out->unit_of[e];
  if (out->delay_kind_mask && !row.window_all_sfpu)
    {
      out->refusal = refusal_delay_model ();
      return false;
    }

  /* Operand routing: route=1 for the SHFT2 immediate class, opcodes that
     read VD, and events that must keep a planned VC; route=0 passes the
     launch VD to the VC operand.  */
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      const event_spec &ev = row.events[e];
      if (ev.is_store)
	{
	  out->route_vb[e] = false;
	  continue;
	}
      route_class rc = opcode_route_class (c, ev.opcode);
      if (rc == RC_SHFT2 || opcode_reads_vd (c, ev.opcode))
	out->route_vb[e] = true;
      else if (ev.reads_carrier_vd_reg && ev.planned_src_c == 0)
	out->route_vb[e] = false;
      else
	out->route_vb[e] = ev.planned_src_c != 0;
    }

  /* Byte packing.  */
  uint8_t bytes[4][4] = {};
  for (unsigned e = 0; e < row.n_events; ++e)
    {
      const event_spec &ev = row.events[e];
      unsigned case_kind = ev.is_store ? SEQ_CASE_STORE
	: SEQ_CASE_TEMPLATE0 + (unsigned) out->template_index_of[e];
      bool vd16 = ev.is_store ? out->store_reads_l16 : out->writes_l16[e];
      uint8_t byte = 0;
      if (!encode_sequence_bits (case_kind, out->delay_of[e], vd16,
				 out->route_vb[e], &byte))
	{
	  out->refusal = refusal_capacity ();
	  return false;
	}
      bytes[ev.macro_index][out->unit_of[e]] = byte;
    }
  if (out->has_staging_copy)
    {
      uint8_t byte = 0;
      if (!encode_sequence_bits (SEQ_CASE_TEMPLATE0
				 + (unsigned) out->staging_template_index,
				 out->staging_delay, true, true, &byte))
	{
	  out->refusal = refusal_capacity ();
	  return false;
	}
      bytes[out->staging_macro][copy_facts.seq_unit] = byte;
    }
  for (unsigned m = 0; m < row.n_macros; ++m)
    out->seq_words[m] = rvtt_macro::compose_sequence_word (bytes[m]);

  /* Drain: how far the last event executes past the last issue slot.  */
  int last_exec = 0;
  for (unsigned e = 0; e < row.n_events; ++e)
    if (exec[e] > last_exec)
      last_exec = exec[e];
  if (out->has_staging_copy && exec[MAX_EVENTS] > last_exec)
    last_exec = exec[MAX_EVENTS];
  out->drain = last_exec - row.last_issue_slot;
  if (out->drain < 0)
    out->drain = 0;
  return true;
}

}  /* namespace rvtt_macro_derive */

#endif /* GCC_RVTT_MACRO_DERIVE_CORE_H */
