/* One reservation/latency timing engine for Tensix SFPU scheduling.
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

/* FABLE_GOES_BURR.md item #11 -- see rvtt-timing.h for the module
   contract.  Every function here is a verdict-identical (CLASS-I) move
   of a previously hand-kept simulator; the provenance of each is named
   at its definition.  */

#include "config.h"
#define INCLUDE_VECTOR
#include "system.h"
#include "coretypes.h"
#include "rvtt-timing.h"

namespace rvtt_timing
{

/* Moved verbatim from rtl-rvtt-schedule.cc ls_simulate (and its
   pre-RA pseudo-dependence mirror, rtl-rvtt-lp-schedule-prera.cc
   simulate_order): the modeled issue timeline.  In-order: each node
   issues no earlier than the running end of the previously issued
   node; a latency-weighted dependence (RAW/WAW) additionally requires
   issue distance >= producer words + producer latency, an issue-order
   dependence (WAR) >= producer words (vacuous in this in-order
   timeline, kept for the model's completeness).  */

int
simulate (const seq &s, const std::vector<int> &order,
	  std::vector<int> *issue, const std::vector<bool> &exit_shadow)
{
  int t = 0;
  for (unsigned k = 0; k != order.size (); ++k)
    {
      const op &n = s.ops[order[k]];
      int ready = n.entry_pin;
      for (unsigned j = 0; j != k; ++j)
	{
	  dep_kind kind = s.kind (order[j], order[k]);
	  if (!kind)
	    continue;
	  const op &p = s.ops[order[j]];
	  int need = (*issue)[order[j]] + p.words
		     + (kind == DEP_LATENCY ? p.lat : 0);
	  if (need > ready)
	    ready = need;
	}
      if (ready > t)
	t = ready;
      (*issue)[order[k]] = t;
      t += n.words;
    }
  int end = t;
  for (unsigned i = 0; i != s.ops.size (); ++i)
    if (exit_shadow[i])
      {
	int drain = (*issue)[i] + s.ops[i].words + s.ops[i].lat;
	if (drain > end)
	  end = drain;
      }
  return end;
}

/* Moved verbatim from rtl-rvtt-schedule.cc ls_cyclic_ii: steady-state
   initiation interval by the 6-copy convergence probe -- issue the
   order repeatedly, declare convergence when two successive
   start-deltas agree (the CLASS-I precision tier; the RecMII/ResMII
   exact form is the documented later tier).  */

int
cyclic_ii (const seq &s, const std::vector<int> &order)
{
  unsigned n = s.ops.size ();
  const unsigned COPIES = 6;
  std::vector<int> issue (n * COPIES, 0);
  std::vector<int> start (COPIES, 0);
  int t = 0;
  int last_d = 0;
  for (unsigned c = 0; c != COPIES; ++c)
    {
      for (unsigned k = 0; k != n; ++k)
	{
	  const op &nd = s.ops[order[k]];
	  int ready = t;
	  for (unsigned pc = 0; pc <= c; ++pc)
	    for (unsigned j = 0; j != (pc == c ? k : n); ++j)
	      {
		dep_kind kind = s.kind (order[j], order[k]);
		if (!kind)
		  continue;
		const op &p = s.ops[order[j]];
		int need = issue[pc * n + order[j]] + p.words
			   + (kind == DEP_LATENCY ? p.lat : 0);
		if (need > ready)
		  ready = need;
	      }
	  issue[c * n + order[k]] = ready;
	  t = ready + nd.words;
	  if (k == 0)
	    start[c] = ready;
	}
      if (c >= 2)
	{
	  int d1 = start[c] - start[c - 1];
	  int d2 = start[c - 1] - start[c - 2];
	  last_d = d1;
	  if (d1 == d2)
	    return d1;
	}
      else if (c == 1)
	last_d = start[1] - start[0];
    }
  return last_d;
}

/* Moved verbatim from rtl-rvtt-replay.cc hoist_profitable_p's shared
   model (counted-loop capture branch) == rvtt-bnb.cc
   mirror_counted_hoist_fires (validated there against the recorded
   pin-13 hardshrink refusal, -383 at trips 31, words 9, ds_exec 10).  */

hoist_pricing
counted_hoist_price (const hoist_costs &c, int64_t trips, int64_t words,
		     int64_t exec_slots)
{
  hoist_pricing r;
  r.exec = exec_slots * c.slot;
  r.deliver_body = words * c.push;
  r.deliver_record = (1 + words) * c.push;
  r.before = r.deliver_body > r.exec ? r.deliver_body : r.exec;
  r.after = c.push > r.exec + c.turnaround ? c.push
					   : r.exec + c.turnaround;
  r.record = r.deliver_record + c.record_overhead;
  r.benefit = trips * (r.before - r.after) - r.record;
  return r;
}

/* Moved verbatim from rtl-rvtt-replay.cc hoist_profitable_p's shared
   model (re-record branches, including the execution-saturation
   context term) == rvtt-bnb.cc mirror_rerecord_hoist_fires.  */

hoist_pricing
rerecord_hoist_price (const hoist_costs &c, int64_t trips, int64_t words,
		      int64_t exec_slots, int64_t launch_run,
		      bool completion_guard)
{
  hoist_pricing r;
  r.exec = exec_slots * c.slot;
  r.deliver_body = words * c.push;
  r.deliver_record = (1 + words) * c.push;
  r.after = c.push > r.exec + c.turnaround ? c.push
					   : r.exec + c.turnaround;
  r.exec_bound = r.exec >= r.deliver_record;
  if (r.exec_bound)
    {
      /* Execution-bound: the record engine's per-pass overhead rides
	 the critical path; the hoisted pass's delivery hides behind
	 the loop's own execution backlog (Reduce-class silicon A/B) --
	 unless the completion-accurate guard charges it in full.  */
      r.before = r.exec + c.record_overhead;
      r.record = c.record_overhead;
      if (completion_guard)
	r.record += r.deliver_record;
    }
  else
    {
      /* Delivery-bound: pin-11-calibrated delivery pricing, with the
	 execution-saturation context term (silicon-witnessed on the
	 unary-maxmin shape): when the contiguous run of sibling
	 launches has enough execution surplus to hide the record
	 pass's delivery, hoisting relieves nothing per trip.  */
      r.before = r.deliver_record;
      r.record = r.deliver_record + c.record_overhead;
      r.surplus = launch_run * (r.exec - c.push);
      if (r.surplus >= r.deliver_record)
	{
	  r.hidden = true;
	  r.before = r.after;
	}
    }
  r.benefit = trips * (r.before - r.after) - r.record;
  return r;
}

} // namespace rvtt_timing
