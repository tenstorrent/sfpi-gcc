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

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "tm_p.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-tables.h"
#include "rvtt-macro-sched-core.h"
#include "rvtt-macro-sched.h"

/* The schedule is derived from the row's dataflow DAG, the generated
   Layer-1 attributes, and the Layer-4 capability tables:

   - Every Dst-memory access needs an issued word carrying its address;
     accesses with equal typed (address, data-mode) operands share one
     carrier (launch), everything else issues separately.  The initiation
     interval is the number of issued words.
   - Carrier grouping is a deterministic two-candidate search, driven by
     the caller in ascending candidate order: candidate 0 shares
     carriers maximally as above; candidate 1 demotes every Dst store to
     its own single-access carrier (the delayed-store slot), and exists
     only when candidate 0 actually merged a store with another access.
     The caller accepts the first candidate whose descriptor proves and
     refuses when none does.  The demotion rule is keyed to the access
     kind alone -- no address value, operation, or calendar identity
     participates.
   - Launched sequence events are non-Dst value operations whose subunit
     class the tables can host; a carrier that hosts no events is demoted
     to an ordinary explicit issue (macros are a scarce resource).
   - The row's typed Dst-stride separator is absorbed into the last
     carrier only when the tables' address-modifier machinery covers the
     delta.
   - Per-event programmed delays come exclusively from the tables' proven
     sequence programs.  DELAY_UNKNOWN never schedules: the derived
     structure is dumped and the schedule refuses by name (WP7-blocking
     until the architectural reference resolves the delay).
   - The launch VD policy is derived conservatively: when a launched
     event consumes the launch VD and its execution slot is not proven to
     precede the next row's launch, consecutive rows must alternate VDs.

   Nothing here names an operation, matches an opcode calendar, or
   assembles a raw word.  */

const char *macro_sched_refusal_event_delay_unproven
  = "event-delay-unproven";
const char *macro_sched_refusal_sequence_encoding_unproven
  = "sequence-encoding-unproven";
const char *macro_sched_refusal_template_capacity_exceeded
  = "template-capacity-exceeded";
const char *macro_sched_refusal_port_conflict = "port-conflict";
const char *macro_sched_refusal_latency_violation = "latency-violation";
const char *macro_sched_refusal_cc_template_unproved
  = "cc-template-unproved";

namespace {

using rvtt_macro::caps;

/* The two subunit vocabularies are value-compatible by construction.  */
static_assert ((int) XTT_SU_SIMPLE == (int) rvtt_macro::SU_SIMPLE
	       && (int) XTT_SU_ROUND == (int) rvtt_macro::SU_ROUND
	       && (int) XTT_SU_STORE == (int) rvtt_macro::SU_STORE,
	       "subunit vocabularies diverged");
static_assert ((int) XTT_SU_SIMPLE == (int) rvtt_macro_sched::CSU_SIMPLE
	       && (int) XTT_SU_STORE == (int) rvtt_macro_sched::CSU_STORE,
	       "core subunit vocabulary diverged");

static rvtt_macro::cpu_t
current_cpu ()
{
  gcc_assert (!TARGET_XTT_TENSIX_QSR || (!TARGET_XTT_TENSIX_WH
					 && !TARGET_XTT_TENSIX_BH));
  return TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
}

struct row_item
{
  rtx_insn *insn;
  xtt_effect_set effects;
  rtx address, mode, addr_mode;	/* Dst accesses only */
  int carrier;			/* carrier group index, or -1	*/
  bool launched;		/* launched sequence event	*/
  bool coalesced;		/* WP9 lane-merge, no issued word */
};

/* Find the proven sequence program matching MACRO_INDEX and the derived
   event list (unit/template/is_store; delays are outputs).  */
static const rvtt_macro::seq_program *
find_seq_program (const caps *c, unsigned macro_index,
		  const rvtt_macro::seq_event *events, unsigned n)
{
  for (unsigned ix = 0; ix != c->n_seq_programs; ++ix)
    {
      const rvtt_macro::seq_program &p = c->seq_programs[ix];
      if (p.macro_index != macro_index || p.n_events != n)
	continue;
      bool match = true;
      for (unsigned e = 0; e != n && match; ++e)
	if (p.events[e].unit != events[e].unit
	    || p.events[e].template_index != events[e].template_index
	    || p.events[e].is_store != events[e].is_store)
	  match = false;
      if (match)
	return &p;
    }
  return nullptr;
}

} // anonymous namespace

void
rvtt_macro_schedule_release (macro_schedule *sched)
{
  sched->events.release ();
}

bool
rvtt_macro_schedule_region (const macro_region &region, macro_schedule *out,
			    FILE *dump, unsigned candidate)
{
  memset (out, 0, sizeof (*out));
  out->events = vNULL;
  out->drain_slots = -1;

  const caps *c = rvtt_macro_caps_for_cpu (current_cpu ());
  if (!c)
    {
      if (dump)
	fprintf (dump, "Macro-planner schedule-refusal: %s\n",
		 rvtt_macro_caps_refusal (current_cpu ()));
      return false;
    }

  /* Classify the canonical row.  */
  const macro_row &row = region.rows[0];
  auto_vec<row_item> items;
  for (rtx_insn *insn : row.insns)
    {
      row_item item;
      item.insn = insn;
      item.effects = rvtt_insn_effects (insn);
      item.address = item.mode = item.addr_mode = nullptr;
      item.carrier = -1;
      item.launched = false;
      item.coalesced = false;
      if (item.effects.dst_mem_read || item.effects.dst_mem_write)
	if (!rvtt_dst_access_operands (insn, item.effects, &item.address,
				       &item.mode, &item.addr_mode))
	  return false;
      items.safe_push (item);
    }

  /* Carrier grouping: Dst accesses with equal typed address operands
     share one issued launch word (at most one load per carrier -- the
     launch word loads one value; a same-address store rides the carrier
     as a delayed store whose data mode lives in the misc word, not the
     launch).

     The grouping is a deterministic two-candidate search (see the file
     comment): candidate 0 shares maximally; candidate 1 demotes every
     Dst store to its own single-access carrier and exists only when
     candidate 0 merged a store with another access.  DEMOTE_STORES
     selects the rule; the return value reports whether any merge
     involved a store.  */
  auto_vec<int> carrier_first;	/* item index of each carrier's first access */
  auto_vec<bool> carrier_has_load;
  auto_vec<bool> carrier_has_store;
  auto group_carriers = [&] (bool demote_stores) -> bool
    {
      bool store_merged = false;
      carrier_first.truncate (0);
      carrier_has_load.truncate (0);
      carrier_has_store.truncate (0);
      for (row_item &item : items)
	{
	  item.carrier = -1;
	  if (!item.address)
	    continue;
	  bool is_store = item.effects.dst_mem_write;
	  if (!(demote_stores && is_store))
	    for (unsigned cix = 0; cix != carrier_first.length (); ++cix)
	      {
		const row_item &head = items[carrier_first[cix]];
		if (rtx_equal_p (head.address, item.address)
		    && !(item.effects.dst_mem_read && carrier_has_load[cix])
		    && !(demote_stores && carrier_has_store[cix]))
		  {
		    item.carrier = cix;
		    store_merged |= is_store || carrier_has_store[cix];
		    carrier_has_load[cix] = carrier_has_load[cix]
		      || item.effects.dst_mem_read;
		    carrier_has_store[cix] = carrier_has_store[cix]
		      || is_store;
		    break;
		  }
	      }
	  if (item.carrier < 0)
	    {
	      item.carrier = carrier_first.length ();
	      carrier_first.safe_push (&item - items.begin ());
	      carrier_has_load.safe_push (item.effects.dst_mem_read);
	      carrier_has_store.safe_push (is_store);
	    }
	}
      return store_merged;
    };

  if (candidate == 0)
    group_carriers (false);
  else if (candidate == 1)
    {
      /* Candidate 1 is distinct only when maximal sharing merged a
	 store; otherwise the search is exhausted.  */
      if (!group_carriers (false))
	return false;
      group_carriers (true);
      if (dump)
	fprintf (dump, "Macro-planner schedule-candidate: stores-demoted\n");
    }
  else
    return false;		/* deterministic search exhausted */

  /* WP9 CC-template rows: a row whose slice carries a predicate
     definition (a CC-writing value event reading an LREG).  Region
     discovery only admits CC writers in the definition and proven
     all-lanes-restore roles; here they select the CC hosting and
     coalescing rules below.  */
  bool row_has_cc_def = false;
  for (row_item &item : items)
    row_has_cc_def |= !item.address && item.effects.cc_write
      && item.effects.lreg_read != 0 && !item.effects.lreg_write;

  /* Launched-event hosting: a non-Dst value event is hosted on the
     carrier of its earliest LREG producer that is a Dst load; a store
     rides its own carrier as a delayed store event.  In a
     predicate-writing row, CC-READING value events are never template
     events -- their lane predication depends on the in-row definition,
     whose deferred visibility a template realization cannot honor --
     they are realized by coalescing below or refuse by name.  */
  unsigned launched_events = 0, template_events = 0;
  for (unsigned ix = 0; ix != items.length (); ++ix)
    {
      row_item &item = items[ix];
      if (item.address)
	continue;
      if (item.effects.subunit != XTT_SU_SIMPLE
	  && item.effects.subunit != XTT_SU_ROUND)
	continue;		/* stays an explicit issue (e.g. mad)  */
      if (row_has_cc_def && item.effects.cc_read && !item.effects.cc_write)
	continue;		/* lane-merge candidate (see below)    */
      uint32_t needed = item.effects.lreg_read;
      for (unsigned p = 0; p != ix && item.carrier < 0; ++p)
	if (items[p].address && items[p].effects.dst_mem_read
	    && (items[p].effects.lreg_write & needed))
	  item.carrier = items[p].carrier;
      if (item.carrier >= 0)
	{
	  item.launched = true;
	  ++launched_events;
	  ++template_events;
	}
    }

  /* The row's pure all-lanes CC restore (admitted by discovery only
     after a definition) rides the LAST-issued load carrier -- the
     latest launch available to a CC event -- so its execution follows
     every predicated event of the row.  The visibility obligations are
     proven by descriptor synthesis against the matched program's
     delays.  */
  if (row_has_cc_def)
    {
      int last_load_carrier = -1;
      for (row_item &item : items)
	if (item.address && item.effects.dst_mem_read)
	  last_load_carrier = item.carrier;
      if (last_load_carrier >= 0)
	for (row_item &item : items)
	  if (!item.address && !item.launched
	      && item.effects.cc_write && !item.effects.lreg_read
	      && !item.effects.lreg_write)
	    {
	      item.carrier = last_load_carrier;
	      item.launched = true;
	      ++launched_events;
	      ++template_events;
	    }
    }

  /* Lane-merge coalescing (WP9).  In a predicate-writing row every
     unhosted CC-reading value event must be the lane-merge shape --
     it reads its own destination (the live value), takes exactly one
     other LREG input, both produced by this row's Dst loads, and its
     result is consumed by the row's store -- realized by the
     calendar's predicated-overwrite dataflow with no issued word.
     Anything else has no proven CC realization and refuses by name.  */
  const char *cc_refusal = nullptr;
  if (row_has_cc_def)
    for (unsigned ix = 0; ix != items.length () && !cc_refusal; ++ix)
      {
	row_item &item = items[ix];
	if (item.address || item.launched
	    || !item.effects.cc_read || item.effects.cc_write)
	  continue;
	uint32_t dest = item.effects.lreg_write;
	uint32_t live = item.effects.lreg_read & dest;
	uint32_t other = item.effects.lreg_read & ~dest;
	bool merge_shape = dest && live == dest && other
	  && (other & (other - 1)) == 0;
	bool live_from_load = false, other_from_load = false;
	for (unsigned p = 0; p != ix; ++p)
	  if (items[p].address && items[p].effects.dst_mem_read)
	    {
	      live_from_load |= (items[p].effects.lreg_write & live) != 0;
	      other_from_load |= (items[p].effects.lreg_write & other) != 0;
	    }
	bool store_consumes = false;
	for (unsigned s = ix + 1; s != items.length (); ++s)
	  if (items[s].effects.dst_mem_write)
	    store_consumes |= (items[s].effects.lreg_read & dest) != 0;
	if (merge_shape && live_from_load && other_from_load
	    && store_consumes)
	  item.coalesced = true;
	else
	  cc_refusal = macro_sched_refusal_cc_template_unproved;
      }

  for (row_item &item : items)
    if (item.effects.dst_mem_write && item.carrier >= 0)
      ++launched_events;	/* the delayed store event	       */

  if (template_events > c->n_templates)
    {
      out->refusal = macro_sched_refusal_template_capacity_exceeded;
      if (dump)
	fprintf (dump, "Macro-planner schedule-refusal: %s\n", out->refusal);
      return true;
    }

  /* Demote carriers that host nothing: an ordinary explicit issue costs
     the same word and conserves macro indices.  */
  auto_vec<bool> hosts (carrier_first.length ());
  hosts.safe_grow_cleared (carrier_first.length ());
  for (row_item &item : items)
    if ((item.launched || item.effects.dst_mem_write) && item.carrier >= 0)
      hosts[item.carrier] = true;

  /* Stride absorption through the tables' address-modifier machinery,
     into the carrier of the last Dst access.  */
  int absorbed_stride = 0;
  int absorb_carrier = -1;
  /* A predicate-writing row never absorbs its separator: the explicit
     counter word occupies the issue slot in which the row-end restore's
     CC result becomes visible (cc_visibility_lag), so the NEXT row's
     store-carrying launch latches the restored all-lanes mask.
     Absorbing the stride would compress the interval and latch a stale
     predicate.  */
  if (row.separator && row.dst_delta && !row_has_cc_def)
    {
      rvtt_macro::setc16_program programs[8];
      unsigned n_programs = 0;
      bool needs_bank_base = false;
      if (rvtt_macro::addr_mod_program (c, row.dst_delta, programs,
					&n_programs, &needs_bank_base))
	{
	  for (unsigned ix = items.length (); ix-- > 0;)
	    if (items[ix].address)
	      {
		absorb_carrier = items[ix].carrier;
		break;
	      }
	  if (absorb_carrier >= 0 && hosts[absorb_carrier])
	    absorbed_stride = row.dst_delta;
	  else
	    absorb_carrier = -1;
	}
    }

  /* Deterministic issue-slot assignment: carriers and explicit issues in
     program order of their first instruction.  */
  int slot = 0;
  auto_vec<int> carrier_slot (carrier_first.length ());
  auto_vec<unsigned> carrier_macro (carrier_first.length ());
  carrier_slot.safe_grow_cleared (carrier_first.length ());
  carrier_macro.safe_grow_cleared (carrier_first.length ());
  unsigned launches = 0, explicit_issues = 0, next_macro = 0;
  auto_vec<bool> carrier_seen (carrier_first.length ());
  carrier_seen.safe_grow_cleared (carrier_first.length ());
  for (row_item &item : items)
    {
      if (item.address && hosts[item.carrier])
	{
	  if (!carrier_seen[item.carrier])
	    {
	      carrier_seen[item.carrier] = true;
	      carrier_slot[item.carrier] = slot++;
	      carrier_macro[item.carrier] = next_macro++;
	      ++launches;
	    }
	}
      else if (!item.launched && !item.coalesced)
	{
	  ++explicit_issues;
	  ++slot;
	}
    }
  int ii = slot + ((row.separator && !absorbed_stride) ? 1 : 0);

  /* Sequence lookup per carrier: derived events in program order;
     template ids in derivation order.  Delays come exclusively from the
     matched proven program.  A CC-realization refusal from the
     coalescing rule above takes precedence.  */
  const char *refusal = cc_refusal;
  unsigned next_template = 0;
  auto_vec<const rvtt_macro::seq_program *> programs (carrier_first.length ());
  programs.safe_grow_cleared (carrier_first.length ());
  for (unsigned cix = 0; cix != carrier_first.length (); ++cix)
    {
      if (!hosts[cix])
	continue;
      rvtt_macro::seq_event events[8];
      unsigned n = 0;
      for (row_item &item : items)
	{
	  if (item.launched && item.carrier == (int) cix)
	    {
	      events[n].unit = (rvtt_macro::subunit_t) item.effects.subunit;
	      events[n].template_index = next_template++;
	      events[n].delay = rvtt_macro::DELAY_UNKNOWN;
	      events[n].is_store = false;
	      ++n;
	    }
	  else if (item.effects.dst_mem_write && item.carrier == (int) cix)
	    {
	      events[n].unit = rvtt_macro::SU_STORE;
	      events[n].template_index = -1;
	      events[n].delay = rvtt_macro::DELAY_UNKNOWN;
	      events[n].is_store = true;
	      ++n;
	    }
	}
      if (n > c->n_sequence_slots)
	{
	  refusal = macro_sched_refusal_template_capacity_exceeded;
	  break;
	}
      const rvtt_macro::seq_program *p
	= find_seq_program (c, carrier_macro[cix], events, n);
      if (!p)
	{
	  if (!refusal)
	    refusal = macro_sched_refusal_sequence_encoding_unproven;
	  continue;
	}
      programs[cix] = p;
      for (unsigned e = 0; e != n && !refusal; ++e)
	if (p->events[e].delay == rvtt_macro::DELAY_UNKNOWN)
	  refusal = macro_sched_refusal_event_delay_unproven;
    }

  /* Build the event list and, where every delay is proven, validate the
     subunit-occupancy, write-port, and latency constraints through the
     shared core checkers.  */
  bool all_delays_known = true;
  auto_vec<rvtt_macro_sched::core_event> core_events;
  unsigned seq_pos[16] = {};
  for (row_item &item : items)
    {
      macro_event ev;
      ev.origin = item.insn;
      ev.subunit = item.effects.subunit;
      ev.lreg_dest = item.effects.lreg_write;
      ev.template_id = ~0u;
      ev.seq_slot = 0;
      ev.macro_index = item.carrier >= 0 ? carrier_macro[item.carrier] : 0;
      ev.is_store = item.effects.dst_mem_write;
      ev.issues_word = false;
      ev.is_carrier = false;
      ev.programmed_delay = -1;

      if (item.launched || (item.effects.dst_mem_write && item.carrier >= 0
			    && hosts[item.carrier]))
	{
	  ev.realization = macro_event::LAUNCHED_TEMPLATE_SLOT;
	  ev.slot = carrier_slot[item.carrier];
	  ev.seq_slot = seq_pos[item.carrier]++;
	  const rvtt_macro::seq_program *p = programs[item.carrier];
	  if (p && ev.seq_slot < p->n_events)
	    {
	      uint8_t d = p->events[ev.seq_slot].delay;
	      ev.programmed_delay
		= d == rvtt_macro::DELAY_UNKNOWN ? -1 : (int) d;
	      if (p->events[ev.seq_slot].template_index >= 0)
		ev.template_id = p->events[ev.seq_slot].template_index;
	    }
	  if (ev.programmed_delay < 0)
	    all_delays_known = false;
	}
      else if (item.coalesced)
	{
	  /* Realized by the predicated-overwrite dataflow: no issued
	     word, no template slot, no execution resource.  */
	  ev.realization = macro_event::CC_COALESCED;
	  ev.slot = -1;
	  ev.issues_word = false;
	}
      else
	{
	  ev.realization = macro_event::EXPLICIT_INSN;
	  ev.slot = item.address && hosts.length ()
	    && item.carrier >= 0 && hosts[item.carrier]
	    ? carrier_slot[item.carrier] : -1;
	  ev.issues_word = true;
	}
      out->events.safe_push (ev);

      rvtt_macro_sched::core_event core;
      if (item.coalesced)
	{
	  /* No physical event: excluded from occupancy/port/latency
	     checking (the descriptor layer proves its realization).  */
	  core.subunit = rvtt_macro_sched::CSU_NONE;
	  core.port = rvtt_macro_sched::CP_NONE;
	  core.carrier_slot = 0;
	  core.delay = 0;
	}
      else
	{
	  core.subunit = (int) item.effects.subunit;
	  core.port = item.effects.lreg_write
	    ? (int) get_attr_xtt_lreg_write_port (item.insn)
	    : rvtt_macro_sched::CP_NONE;
	  core.carrier_slot = ev.slot;
	  core.delay = ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT
	    ? ev.programmed_delay : 0;
	}
      core_events.safe_push (core);
    }

  /* Mark the issuing carriers.  */
  {
    auto_vec<bool> seen (carrier_first.length ());
    seen.safe_grow_cleared (carrier_first.length ());
    for (unsigned ix = 0; ix != items.length (); ++ix)
      {
	row_item &item = items[ix];
	if (item.address && item.carrier >= 0 && hosts[item.carrier]
	    && !seen[item.carrier])
	  {
	    seen[item.carrier] = true;
	    out->events[ix].issues_word = true;
	    out->events[ix].is_carrier = true;
	  }
      }
  }

  /* Explicit issue slots.  */
  {
    int next = 0;
    for (unsigned ix = 0; ix != items.length (); ++ix)
      {
	macro_event &ev = out->events[ix];
	if (ev.issues_word && ev.slot >= 0)
	  next = ev.slot + 1;
	else if (ev.issues_word)
	  {
	    ev.slot = next++;
	    core_events[ix].carrier_slot = ev.slot;
	  }
      }
  }

  if (!refusal && all_delays_known)
    {
      if (!core_check_subunit_occupancy (core_events.address (),
					 core_events.length ())
	  || !core_check_write_ports (core_events.address (),
				      core_events.length (), 3))
	refusal = macro_sched_refusal_port_conflict;
      /* Latency along LREG def->use edges.  Edges into or out of a
	 coalesced lane-merge have no physical event on either end --
	 its realization (and the timing proof) is the descriptor
	 layer's obligation.  */
      for (unsigned i = 0; i != items.length () && !refusal; ++i)
	for (unsigned j = i + 1; j != items.length () && !refusal; ++j)
	  if ((items[i].effects.lreg_write & items[j].effects.lreg_read)
	      && !items[i].coalesced && !items[j].coalesced)
	    {
	      int ready = core_writeback_slot (core_events[i]);
	      int exec = core_writeback_slot (core_events[j]);
	      if (!rvtt_macro_sched::core_check_dependency (ready, exec))
		refusal = macro_sched_refusal_latency_violation;
	    }
    }

  int last_issue = 0;
  for (macro_event &ev : out->events)
    if (ev.issues_word && ev.slot > last_issue)
      last_issue = ev.slot;
  out->ii = ii;
  out->launches = launches;
  out->explicit_issues = explicit_issues
    + ((row.separator && !absorbed_stride) ? 1 : 0);
  out->launched_events = launched_events;
  out->drain_slots = all_delays_known
    ? rvtt_macro_sched::core_drain_slots (core_events.address (),
					  core_events.length (), last_issue)
    : -1;
  out->lreg_footprint = region.internal_lregs;
  /* Conservative VD policy: a hosted launched event consumes the launch
     VD; without a proven consumption slot before the next row's launch,
     consecutive rows must alternate VDs.  */
  out->alternating_vd = launched_events != 0;
  out->absorbed_stride = absorbed_stride;
  out->refusal = refusal;

  if (dump)
    {
      char drain[16];
      if (out->drain_slots < 0)
	snprintf (drain, sizeof (drain), "unproven");
      else
	snprintf (drain, sizeof (drain), "%d", out->drain_slots);
      fprintf (dump,
	       "Macro-planner schedule: ii=%d issues=%u launches=%u"
	       " explicit=%u launched-events=%u vd=%s drain=%s\n",
	       out->ii, launches + out->explicit_issues, launches,
	       out->explicit_issues, launched_events,
	       out->alternating_vd ? "alternating" : "fixed", drain);
      for (unsigned cix = 0; cix != carrier_first.length (); ++cix)
	{
	  if (!hosts[cix])
	    continue;
	  bool has_load = false, has_store = false;
	  unsigned hosted = 0;
	  for (row_item &item : items)
	    if (item.carrier == (int) cix)
	      {
		has_load |= item.effects.dst_mem_read;
		has_store |= item.effects.dst_mem_write;
		hosted += item.launched
		  || (item.effects.dst_mem_write && hosts[cix]);
	      }
	  fprintf (dump, "Macro-planner issue %d: launch macro=%u"
		   " carries=%s%s%s hosted=%u",
		   carrier_slot[cix], carrier_macro[cix],
		   has_load ? "load" : "", has_load && has_store ? "+" : "",
		   has_store ? "store" : "", hosted);
	  if ((int) cix == absorb_carrier)
	    fprintf (dump, " absorbs-stride=%d", absorbed_stride);
	  fprintf (dump, "\n");
	}
      for (unsigned ix = 0; ix != items.length (); ++ix)
	if (out->events[ix].issues_word
	    && (!items[ix].address || !hosts[items[ix].carrier]))
	  fprintf (dump, "Macro-planner issue %d: explicit subunit=%s\n",
		   out->events[ix].slot,
		   items[ix].effects.subunit == XTT_SU_LOAD ? "load"
		   : items[ix].effects.subunit == XTT_SU_STORE ? "store"
		   : items[ix].effects.subunit == XTT_SU_MAD ? "mad"
		   : "other");
      if (refusal)
	fprintf (dump, "Macro-planner schedule-refusal: %s\n", refusal);
    }
  return true;
}
