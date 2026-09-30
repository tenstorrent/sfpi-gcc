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
#include "rvtt-refuse.h"
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
     structure is dumped and the schedule refuses by name (formation-
     blocking until an architectural reference resolves the delay).
   - The launch VD policy is derived conservatively: when a launched
     event consumes the launch VD and its execution slot is not proven to
     precede the next row's launch, consecutive rows must alternate VDs.

   Nothing here names an operation, matches an opcode calendar, or
   assembles a raw word.  */

/* Scheduler-facing derivation helpers (rvtt-macro-desc.cc): the realized
   hosted sub-unit, the shared sacrificial-VD formula, and the derived
   template-class probe backing capacity-aware hosting.  */
extern int rvtt_macro_hosted_subunit (rtx_insn *);
extern bool rvtt_macro_derived_template_probe (rtx_insn *, int launch_vd,
					       uint8_t *opcode, uint8_t *mod1,
					       uint8_t *src_c, uint16_t *imm12);

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
const char *macro_sched_refusal_imm_stride_unabsorbed
  = "imm-stride-not-absorbed";

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

/* Map the compilation target to the capability tables' cpu_t
   vocabulary; the target flags are asserted mutually exclusive.  */

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
  bool coalesced;		/* CC-template lane-merge, no issued word */
  bool absorbs;			/* compact-calendar auto-inc absorber */
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

} /* anonymous namespace */

/* Free the heap storage owned by SCHED (the event vector).  */

void
rvtt_macro_schedule_release (macro_schedule *sched)
{
  sched->events.release ();
}

/* One deterministic scheduling attempt: grouping candidate CANDIDATE
   with the hosting proposals in BANNED (bit per row-item index) forced
   to stay explicit issues.  BANNED = 0 is the established maximal
   greedy hosting; the IMS repair driver below enumerates reduced
   hosted sets when the maximal proposal refuses downstream.  */

static bool
schedule_region_1 (const macro_region &region, macro_schedule *out,
		   FILE *dump, unsigned candidate, uint64_t banned)
{
  memset (out, 0, sizeof (*out));
  out->events = vNULL;
  out->drain_slots = -1;

  const caps *c = rvtt_macro_caps_for_cpu (current_cpu ());
  if (!c)
    {
      rvtt_refuse_by_name (rvtt_macro_caps_refusal (current_cpu ()), dump,
			   "Macro-planner schedule-refusal: %s\n",
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
      item.absorbs = false;
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

  /* CC-template rows: a row whose slice carries a predicate
     definition (a CC-writing value event reading an LREG).  Region
     discovery only admits CC writers in the definition and proven
     all-lanes-restore roles; here they select the CC hosting and
     coalescing rules below.  */
  bool row_has_cc_def = false;
  for (row_item &item : items)
    row_has_cc_def |= !item.address && item.effects.cc_write
      && item.effects.lreg_read != 0 && !item.effects.lreg_write;

  /* Compact calendar: predicate-writing rows gain one more deterministic candidate
     AHEAD of the established two -- the COMPACT CC calendar (the
     production handwritten select protocol's own 3-slot shape): the
     all-lanes restore rides the second-issued load carrier, the
     trailing payload load stays explicit and absorbs the row stride
     through its own auto-increment address mode, and the store format
     rides the launch (misc UsesLoadMod0ForStore).  Non-CC rows keep
     the established candidate numbering unchanged.  */
  bool cc_compact = false;
  unsigned grouping_candidate = candidate;
  if (row_has_cc_def)
    {
      if (candidate == 0)
	{
	  cc_compact = true;
	  grouping_candidate = 0;
	}
      else
	grouping_candidate = candidate - 1;
    }

  if (grouping_candidate == 0)
    group_carriers (false);
  else if (grouping_candidate == 1)
    {
      /* This grouping is distinct only when maximal sharing merged a
	 store; otherwise the search is exhausted.  */
      if (!group_carriers (false))
	return false;
      group_carriers (true);
      if (dump)
	fprintf (dump, "Macro-planner schedule-candidate: stores-demoted\n");
    }
  else
    return false;		/* deterministic search exhausted */

  if (cc_compact && dump)
    fprintf (dump, "Macro-planner schedule-candidate: cc-compact\n");

  /* Launched-event hosting (generalized with the derived template classes):

     - CC-DEFINITION events (a CC write reading LREGs, writing none)
       keep the established rule: hosted on the carrier of their
       earliest producing Dst load (the select programs key
       on exactly this assignment).

     - Any SIMPLE/ROUND/MAD value event whose single written register
       is a carrier load's destination is hosted on that carrier: the
       launch-VD chain realization (a scheduled template's result is
       always routed to the launch VD or LReg16, SFPLOADMACRO.md).

     - The store's sole data producer, when its result register is no
       carrier's destination, rides the store's own carrier (the
       LReg16 / same-macro-VD store-source realizations the descriptor
       layer proves).

     One event per (carrier, realized sub-unit) -- the realized unit is
     the capability-table placement descriptor synthesis will prove (an
     in-place immediate shift realizes on Round through the proven
     SHFT2 pair); an event whose unit slot is taken simply stays an
     explicit issue.  In a predicate-writing row, CC-READING value
     events are never template events -- their lane predication depends
     on the in-row definition, whose deferred visibility a template
     realization cannot honor -- they are realized by coalescing below
     or refuse by name.  Hosting is a candidate proposal: operand
     encodability and timing are proven (or refused by name)
     downstream.  */
  unsigned launched_events = 0, template_events = 0;
  auto_vec<uint8_t> carrier_unit_taken (carrier_first.length ());
  carrier_unit_taken.safe_grow_cleared (carrier_first.length ());
  /* The row's store and its sole-producer relationship.  */
  int store_ix = -1;
  bool multiple_stores = false;
  for (unsigned ix = 0; ix != items.length (); ++ix)
    if (items[ix].effects.dst_mem_write)
      {
	multiple_stores |= store_ix >= 0;
	store_ix = (int) ix;
      }
  /* Capacity-aware tuple budget for the derived template classes: the
     hosting pass counts DISTINCT probed field tuples against the
     InstructionTemplate budget (bit-identical tuples share a slot);
     probe-refused events hosted through the established rule are the
     proven whole-word programs' territory and consume no tuple here.  */
  struct probe_tuple { uint8_t opcode, mod1, src_c; uint16_t imm12; };
  probe_tuple tuples[4];
  unsigned n_tuples = 0;
  const unsigned max_tuples = c->n_templates > 4 ? 4 : c->n_templates;

  /* One hosting attempt; PASS selects the eligible rules so the store's
     sole producer is admitted before the chain events contend for the
     template budget (it is load-bearing for the derived store-source
     realization).  */
  auto try_host = [&] (unsigned ix, bool producer_pass) -> void
    {
      row_item &item = items[ix];
      if (ix < 64 && ((banned >> ix) & 1))
	return;			/* IMS repair: forced explicit	       */
      if (item.address || item.launched)
	return;
      if (item.effects.subunit != XTT_SU_SIMPLE
	  && item.effects.subunit != XTT_SU_ROUND
	  && item.effects.subunit != XTT_SU_MAD)
	return;			/* stays an explicit issue	       */
      if (row_has_cc_def && item.effects.cc_read && !item.effects.cc_write)
	return;			/* lane-merge candidate (see below)    */

      bool cc_definition = item.effects.cc_write
	&& item.effects.lreg_read != 0 && !item.effects.lreg_write;
      if (cc_definition && producer_pass)
	return;
      int cand = -1;
      int launch_vd = -1;
      bool legacy_reads_rule = false;
      if (cc_definition)
	{
	  /* Established select-program rule.  */
	  uint32_t needed = item.effects.lreg_read;
	  for (unsigned p = 0; p != ix && cand < 0; ++p)
	    if (items[p].address && items[p].effects.dst_mem_read
		&& (items[p].effects.lreg_write & needed))
	      cand = items[p].carrier;
	}
      else
	{
	  /* The launch-VD-routed result is a physical L0..L7 register;
	     audited writes to hardware constant registers (the
	     dropped-constant-side class of the swap family, L8+) ride
	     the proven program envelopes and do not constrain the
	     routing.  Events OUTSIDE the single-result class (the
	     dual-result binary swap of the frozen binary-periodic
	     program) keep the established read-based hosting
	     unchanged: SIMPLE/ROUND only, on the carrier of the
	     earliest producing Dst load, no tuple accounting (the
	     proven whole-word programs own their realization).  */
	  uint32_t dest = item.effects.lreg_write & 0xffu;
	  if (!dest || (dest & (dest - 1)) != 0)
	    {
	      if (producer_pass)
		return;
	      if (item.effects.subunit != XTT_SU_SIMPLE
		  && item.effects.subunit != XTT_SU_ROUND)
		return;
	      uint32_t needed = item.effects.lreg_read;
	      for (unsigned p = 0; p != ix && cand < 0; ++p)
		if (items[p].address && items[p].effects.dst_mem_read
		    && (items[p].effects.lreg_write & needed))
		  cand = items[p].carrier;
	      if (cand < 0)
		return;
	      int lunit = rvtt_macro_hosted_subunit (item.insn);
	      if (lunit != XTT_SU_SIMPLE && lunit != XTT_SU_MAD
		  && lunit != XTT_SU_ROUND)
		return;
	      if ((carrier_unit_taken[cand] >> lunit) & 1)
		return;
	      carrier_unit_taken[cand] |= (uint8_t) (1u << lunit);
	      item.carrier = cand;
	      item.launched = true;
	      ++launched_events;
	      ++template_events;
	      return;
	    }
	  bool is_producer = false;
	  /* Sole store producer: dataflow-ordered consumer scan (only
	     readers AFTER the producer consume its value).  */
	  if (store_ix >= 0 && !multiple_stores && (unsigned) store_ix > ix
	      && (items[store_ix].effects.lreg_read & dest))
	    {
	      is_producer = true;
	      for (unsigned jx = ix + 1;
		   jx != items.length () && is_producer; ++jx)
		if ((int) jx != store_ix
		    && (items[jx].effects.lreg_read & dest))
		  is_producer = false;
	    }
	  if (producer_pass && !is_producer)
	    return;
	  /* (a) launch-VD chain: a carrier load writes this register.  */
	  for (unsigned p = 0; p != items.length () && cand < 0; ++p)
	    if (items[p].address && items[p].effects.dst_mem_read
		&& items[p].effects.lreg_write == dest)
	      {
		cand = items[p].carrier;
		launch_vd = ctz_hwi (items[p].effects.lreg_write);
	      }
	  /* (b) the sole store producer rides the store's carrier.  */
	  if (cand < 0 && is_producer)
	    {
	      cand = items[store_ix].carrier;
	      /* A store-only carrier's sacrificial VD holds unread
		 garbage: -2 = no VD identity (descriptor synthesis
		 selects and proves the register).  A merged
		 (load+store) carrier's VD is its load's destination.  */
	      launch_vd = -2;
	      if (carrier_has_load[cand])
		for (unsigned p = 0; p != items.length (); ++p)
		  if (items[p].address && items[p].effects.dst_mem_read
		      && items[p].carrier == cand
		      && items[p].effects.lreg_write
		      && !(items[p].effects.lreg_write
			   & (items[p].effects.lreg_write - 1)))
		    launch_vd = ctz_hwi (items[p].effects.lreg_write);
	    }
	  /* Probe-refused events fall back to the established
	     read-based rule (the proven whole-word programs host their
	     own event classes, e.g. the cast-round STOCHRND).  */
	  probe_tuple t;
	  if (cand >= 0
	      && rvtt_macro_derived_template_probe (item.insn, launch_vd,
						    &t.opcode, &t.mod1,
						    &t.src_c, &t.imm12))
	    {
	      int slot = -1;
	      for (unsigned k = 0; k != n_tuples && slot < 0; ++k)
		if (tuples[k].opcode == t.opcode && tuples[k].mod1 == t.mod1
		    && tuples[k].src_c == t.src_c
		    && tuples[k].imm12 == t.imm12)
		  slot = (int) k;
	      if (slot < 0)
		{
		  if (n_tuples == max_tuples)
		    return;	/* template budget exhausted	       */
		  tuples[n_tuples++] = t;
		}
	    }
	  else if (cand >= 0)
	    {
	      /* Established rule only: the event must read a Dst
		 load's written register and hosts on that load's
		 carrier.  */
	      cand = -1;
	      uint32_t needed = item.effects.lreg_read;
	      for (unsigned p = 0; p != ix && cand < 0; ++p)
		if (items[p].address && items[p].effects.dst_mem_read
		    && (items[p].effects.lreg_write & needed))
		  cand = items[p].carrier;
	      legacy_reads_rule = true;
	    }
	  else if (cand < 0)
	    return;
	  (void) legacy_reads_rule;
	}
      if (cand < 0)
	return;
      int unit = rvtt_macro_hosted_subunit (item.insn);
      if (unit != XTT_SU_SIMPLE && unit != XTT_SU_MAD && unit != XTT_SU_ROUND)
	return;
      if (!cc_definition && (carrier_unit_taken[cand] >> unit) & 1)
	return;			/* unit slot taken: stays explicit     */
      carrier_unit_taken[cand] |= (uint8_t) (1u << unit);
      item.carrier = cand;
      item.launched = true;
      ++launched_events;
      ++template_events;
    };
  for (unsigned ix = 0; ix != items.length (); ++ix)
    try_host (ix, true);	/* the store's sole producer first     */
  for (unsigned ix = 0; ix != items.length (); ++ix)
    try_host (ix, false);

  /* The row's pure all-lanes CC restore (admitted by discovery only
     after a definition) rides a load carrier; which one is the
     candidate's CC hosting rule:

     - established: the LAST-issued load carrier -- the latest
       launch available to a CC event -- so its execution follows every
       predicated event of the row;

     - compact: the EARLIEST-issued load carrier that is not the
       definition's -- the second launch of the handwritten select
       protocol -- freeing the trailing payload load to stay explicit
       (the deferred-CC visibility lag keeps the restore's write
       invisible to that same-interval trailing issue; the timing
       obligations are proven, as always, by the descriptor CC model).

     The visibility obligations are proven by descriptor synthesis
     against the matched program's delays.  */
  if (row_has_cc_def)
    {
      int def_carrier = -1;
      for (row_item &item : items)
	if (!item.address && item.launched && item.effects.cc_write
	    && item.effects.lreg_read != 0 && !item.effects.lreg_write)
	  def_carrier = item.carrier;
      int host_carrier = -1;
      for (row_item &item : items)
	if (item.address && item.effects.dst_mem_read)
	  {
	    if (cc_compact && item.carrier == def_carrier)
	      continue;
	    host_carrier = item.carrier;
	    if (cc_compact)
	      break;		/* earliest non-definition load carrier */
	  }
      if (host_carrier >= 0)
	for (row_item &item : items)
	  if (!item.address && !item.launched
	      && item.effects.cc_write && !item.effects.lreg_read
	      && !item.effects.lreg_write)
	    {
	      item.carrier = host_carrier;
	      item.launched = true;
	      ++launched_events;
	      ++template_events;
	    }
    }

  /* Lane-merge coalescing.  In a predicate-writing row every
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

  /* More launched events than InstructionTemplate destinations is no
     longer a hard stop (template sharing): bit-identical derived template words
     share one destination, and only descriptor synthesis derives the
     words.  The refusal is recorded for the schedule dump and carved
     out at synthesis, whose own post-sharing capacity gate is
     authoritative.  */
  const char *capacity_refusal = nullptr;
  if (template_events > c->n_templates)
    {
      capacity_refusal = macro_sched_refusal_template_capacity_exceeded;
      if (dump)
	fprintf (dump, "Macro-planner schedule-note: template capacity"
		 " pre-sharing overflow (synthesis decides)\n");
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
  bool absorb_into_explicit = false;
  const char *cc_compact_refusal = nullptr;
  /* A predicate-writing row's ESTABLISHED calendar never absorbs
     its separator: the explicit counter word occupies the issue slot in
     which the row-end restore's CC result becomes visible
     (cc_visibility_lag), so the next row opens under the restored
     all-lanes mask.  Absorbing the stride would compress the interval
     below the restore's visibility.  Whether the calendar's delayed
     store itself executes under the restored mask is the descriptor CC
     model's live-mask race constraint (cc-restore-store-race) -- the
     established 4-slot calendar fails it and refuses there.  */
  /* The row's per-trip Dst advance: the classic separator-carried
     delta, or -- for an immediate-delta region -- the
     discovery-proven uniform absolute advance
     (macro_region::imm_stride), which the absorbed calendar reproduces
     exactly (discovery proved the total separator advance equals
     rows * imm_stride).  An immediate-delta region that fails to
     absorb refuses by name below: its rows' address immediates cannot
     be replayed verbatim.  */
  int row_stride = row.separator && row.dst_delta ? row.dst_delta : 0;
  if (region.imm_stride)
    row_stride = region.imm_stride;
  if (row_stride && !row_has_cc_def)
    {
      rvtt_macro::setc16_program programs[8];
      unsigned n_programs = 0;
      bool needs_bank_base = false;
      if (rvtt_macro::addr_mod_program (c, row_stride, programs,
					&n_programs, &needs_bank_base))
	{
	  for (unsigned ix = items.length (); ix-- > 0;)
	    if (items[ix].address)
	      {
		absorb_carrier = items[ix].carrier;
		break;
	      }
	  if (absorb_carrier >= 0 && hosts[absorb_carrier])
	    absorbed_stride = row_stride;
	  else
	    absorb_carrier = -1;
	}
    }
  /* The compact CC calendar MANDATES absorption -- into the
     trailing EXPLICIT payload load's own auto-increment address mode
     (the restore was re-hosted onto the middle carrier precisely so
     the interval could compress: the restore becomes visible in the
     next row's first slot, and -- the hardware-proven live-store-mask
     constraint -- retires one cycle BEFORE the delayed store
     executes).  When the row's last Dst access is not a
     demoted explicit load, or the tables' address-modifier machinery
     does not cover the delta, this candidate is unprovable and the
     search advances to the established calendar.  */
  else if (cc_compact)
    {
      rvtt_macro::setc16_program programs[8];
      unsigned n_programs = 0;
      bool needs_bank_base = false;
      int last_load = -1;
      for (unsigned ix = items.length (); ix-- > 0;)
	if (items[ix].address && items[ix].effects.dst_mem_read)
	  {
	    last_load = (int) ix;
	    break;
	  }
      if (row.separator && row.dst_delta && last_load >= 0
	  && !hosts[items[last_load].carrier]
	  && rvtt_macro::addr_mod_program (c, row.dst_delta, programs,
					   &n_programs, &needs_bank_base))
	{
	  absorbed_stride = row.dst_delta;
	  absorb_into_explicit = true;
	  items[last_load].absorbs = true;
	  /* The absorber must be the row's LAST issued word: every
	     other issue's typed address is consumed (launch-latched or
	     dispatched) before the auto-increment executes.  Proven
	     after slot assignment below.  */
	}
      else
	cc_compact_refusal = macro_sched_refusal_cc_template_unproved;
    }
  /* A predicate-writing row that keeps its typed separator (the
     established 4-slot select calendar) is no longer refused here
     structurally: the hardware-adjudicated mis-select was
     root-caused (via the corrected reference simulator) to the
     ARCHITECTURAL live-store-
     mask race -- that calendar retires its all-lanes restore in the
     same cycle as its delayed store -- which the descriptor CC model
     now derives from the slots and proven delays and refuses by name
     (cc-restore-store-race), independent of the separator
     structure.  */

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
     coalescing rule takes precedence over the compact-absorption
     one.  */
  /* An immediate-delta region is only expressible through the absorbed
     calendar (its rows' address immediates differ and cannot replay
     verbatim); anything else refuses by name (fail-closed).  */
  const char *imm_refusal
    = (region.imm_stride && absorbed_stride != region.imm_stride)
    ? macro_sched_refusal_imm_stride_unabsorbed : nullptr;
  const char *refusal = cc_refusal ? cc_refusal
    : cc_compact_refusal ? cc_compact_refusal
    : capacity_refusal ? capacity_refusal : imm_refusal;
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
  /* Sized to the actual carrier count: carrier grouping appends one
     carrier per distinct typed Dst address with no admission cap, so a
     fixed seq_pos[16] wrote one past its end on a 17-carrier row
     (probe-confirmed reachable from plain typed source; the row then
     refuses event-delay-unproven downstream, so the defect was silent
     cc1plus stack corruption, never miscompilation -- FH audit FHP-1).  */
  auto_vec<unsigned> seq_pos (carrier_macro.length ());
  seq_pos.safe_grow_cleared (carrier_macro.length ());
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
      ev.absorbs_stride = item.absorbs;
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

  /* The compact absorber must occupy the row's last issue slot: every
     other issued word's typed address is consumed -- launch-latched or
     dispatched at issue -- before the absorbing load's auto-increment
     advances the counter.  */
  if (absorb_into_explicit && !refusal)
    for (unsigned ix = 0; ix != items.length (); ++ix)
      if (out->events[ix].absorbs_stride
	  && out->events[ix].slot != ii - 1)
	refusal = macro_sched_refusal_cc_template_unproved;

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
  out->absorb_into_explicit = absorb_into_explicit;
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
	  fprintf (dump, "Macro-planner issue %d: explicit subunit=%s%s\n",
		   out->events[ix].slot,
		   items[ix].effects.subunit == XTT_SU_LOAD ? "load"
		   : items[ix].effects.subunit == XTT_SU_STORE ? "store"
		   : items[ix].effects.subunit == XTT_SU_MAD ? "mad"
		   : "other",
		   items[ix].absorbs ? " absorbs-stride" : "");
      if (refusal)
	rvtt_refuse_by_name (refusal, dump,
			     "Macro-planner schedule-refusal: %s\n", refusal);
    }
  return true;
}

/* IMS placement repair (Rau's iterative
   modulo scheduling adapted to the macro sub-unit calendar).  The
   established search is all-or-nothing per grouping candidate: when the
   maximal greedy hosting proposal refuses anywhere downstream
   (sequence derivation, descriptor synthesis, Layer-7 verification),
   the whole region refuses.  Under -mtt-tensix-macro-ims the candidate
   space instead continues past the established candidates with
   deterministically enumerated REDUCED hosted sets -- bounded
   unplacement, the backtracking half of IMS; the reservation-table
   half (per-sub-unit occupancy modulo the interval, delay ranges,
   port and hazard bounds) is the existing derivation core, which
   remains the only feasibility oracle.  Enumeration order is
   best-first: established (maximal) proposals first, then single
   unplacements in reverse program order (the event furthest from its
   carrier is the likeliest delay-range/hazard participant), then
   pairs, capped by a fixed budget.  The first candidate whose
   synthesis and verification prove is committed by the caller, so a
   repair can only recover regions the established search refused --
   never change one it already proves.  Rows carrying a predicate
   definition keep the established CC candidate space untouched.  Off,
   the candidate space is byte-identical to the established search.  */

/* Enumeration budget, NOT a cost-model constant: bounds the repair
   variant SEARCH per grouping (refusal-biased -- exhausting it leaves
   candidates unformed, never admits an unproven one).  No rvtt-cost.md
   derivation exists (FH audit FHP-5); widening or deriving it is the
   planner lane's follow-up.  */
static const unsigned IMS_REPAIR_BUDGET = 12; /* variants per grouping */

/* Public entry point (contract in rvtt-macro-sched.h): derive the
   timing schedule for REGION's candidate grouping CANDIDATE into *OUT,
   dumping to DUMP.  Runs the established search first; under the IMS
   repair mode (see the block comment above) a refused region may
   additionally try up to IMS_REPAIR_BUDGET repair variants -- never
   replacing a schedule the established search already proves.  */

bool
rvtt_macro_schedule_region (const macro_region &region, macro_schedule *out,
			    FILE *dump, unsigned candidate)
{
  if (!riscv_tt_macro_ims)
    return schedule_region_1 (region, out, dump, candidate, 0);

  /* Predicate-definition rows keep the established candidate space:
     their hosting rules are the proven CC select programs' territory.  */
  for (rtx_insn *insn : region.rows[0].insns)
    {
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (!(e.dst_mem_read || e.dst_mem_write) && e.cc_write
	  && e.lreg_read != 0 && !e.lreg_write)
	return schedule_region_1 (region, out, dump, candidate, 0);
    }

  /* Established candidates first, byte-identically.  */
  unsigned n_established = 0;
  {
    macro_schedule probe;
    while (schedule_region_1 (region, &probe, nullptr, n_established, 0))
      {
	rvtt_macro_schedule_release (&probe);
	++n_established;
      }
  }
  if (candidate < n_established)
    return schedule_region_1 (region, out, dump, candidate, 0);

  /* Repair variants: per established grouping, the greedy hosted set
     reduced by one, then by two, in reverse program order (drop-latest
     first), capped at IMS_REPAIR_BUDGET variants per grouping.  */
  unsigned idx = candidate - n_established;
  for (unsigned g = 0; g != n_established; ++g)
    {
      macro_schedule greedy;
      if (!schedule_region_1 (region, &greedy, nullptr, g, 0))
	continue;
      auto_vec<unsigned> hosted;	/* item indices, program order  */
      for (unsigned ix = 0; ix != greedy.events.length (); ++ix)
	if (greedy.events[ix].realization
	      == macro_event::LAUNCHED_TEMPLATE_SLOT
	    && !greedy.events[ix].is_store && ix < 64)
	  hosted.safe_push (ix);
      rvtt_macro_schedule_release (&greedy);

      auto_vec<uint64_t> masks;
      for (unsigned i = hosted.length (); i-- > 0
	   && masks.length () < IMS_REPAIR_BUDGET;)
	masks.safe_push (uint64_t (1) << hosted[i]);
      for (unsigned i = hosted.length (); i-- > 0;)
	for (unsigned j = i; j-- > 0;)
	  {
	    if (masks.length () >= IMS_REPAIR_BUDGET)
	      break;
	    masks.safe_push ((uint64_t (1) << hosted[i])
			     | (uint64_t (1) << hosted[j]));
	  }

      if (idx < masks.length ())
	{
	  if (dump)
	    {
	      fprintf (dump, "Macro-planner schedule-candidate: ims-repair"
		       " grouping=%u banned-items={", g);
	      bool first = true;
	      for (unsigned ix = 0; ix != 64; ++ix)
		if ((masks[idx] >> ix) & 1)
		  {
		    fprintf (dump, "%s%u", first ? "" : ",", ix);
		    first = false;
		  }
	      fprintf (dump, "} greedy-hosted=%u\n", hosted.length ());
	    }
	  return schedule_region_1 (region, out, dump, g, masks[idx]);
	}
      idx -= masks.length ();
    }
  return false;			/* deterministic search exhausted */
}
