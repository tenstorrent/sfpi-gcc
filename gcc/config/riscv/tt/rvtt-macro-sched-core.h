/* Macro-planner schedule constraint core (Layer 3) -- standalone.
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

/* Pure constraint checkers over plain data, shared between the GCC-side
   scheduler (rvtt-macro-sched.cc) and the standalone unit test
   (rvtt-macro-sched-test.cc, built like rvtt-macro-tables-test.cc).  No
   GCC headers; every limit arrives as data from Layer-1 attributes or
   the Layer-4 capability tables -- nothing is hardcoded here.  */

#ifndef GCC_RVTT_MACRO_SCHED_CORE_H
#define GCC_RVTT_MACRO_SCHED_CORE_H

namespace rvtt_macro_sched
{

/* Subunit vocabulary (values match rvtt-effects.h xtt_subunit_t and the
   capability tables' subunit_t; asserted in the GCC-side consumer).  */
enum core_subunit { CSU_NONE, CSU_SIMPLE, CSU_MAD, CSU_ROUND,
		    CSU_LOAD, CSU_STORE, CSU_CFG, CSU_SYNC };

/* Writeback-port classes (values match the generated
   xtt_lreg_write_port attribute order).  */
enum core_port { CP_NONE, CP_OWN, CP_SHARED_SIMPLE_ROUND, CP_BORROWS_MAD };

const int CORE_DELAY_UNKNOWN = -1;

/* One scheduled event: an issue-carrier (launch or explicit word) or a
   launched sequence event executing DELAY slots after its carrier.  */
struct core_event
{
  int subunit;			/* core_subunit			      */
  int port;			/* core_port; CP_NONE = no LREG write */
  int carrier_slot;		/* issue slot of the carrying word    */
  int delay;			/* programmed delay after the carrier;
				   0 for the carrier itself;
				   CORE_DELAY_UNKNOWN refuses	      */
};

/* Writeback slot of EV, or CORE_DELAY_UNKNOWN.  */
inline int
core_writeback_slot (const core_event &ev)
{
  return ev.delay == CORE_DELAY_UNKNOWN
    ? CORE_DELAY_UNKNOWN : ev.carrier_slot + ev.delay;
}

/* Subunit occupancy: at most one event per subunit per slot.  Events
   with unknown delays cannot be proven and refuse (return false).  */
inline bool
core_check_subunit_occupancy (const core_event *events, int n)
{
  for (int i = 0; i < n; ++i)
    {
      int si = core_writeback_slot (events[i]);
      if (si == CORE_DELAY_UNKNOWN)
	return false;
      for (int j = i + 1; j < n; ++j)
	{
	  int sj = core_writeback_slot (events[j]);
	  if (sj == CORE_DELAY_UNKNOWN)
	    return false;
	  if (si == sj && events[i].subunit == events[j].subunit)
	    return false;
	}
    }
  return true;
}

/* LREG write ports: at most MAX_WRITES per slot; the Simple and Round
   subunits share one port; CP_BORROWS_MAD consumes the MAD port.  Events
   with CP_NONE write no LREG.  Unknown delays refuse.  */
inline bool
core_check_write_ports (const core_event *events, int n, int max_writes)
{
  for (int i = 0; i < n; ++i)
    {
      if (events[i].port == CP_NONE)
	continue;
      int si = core_writeback_slot (events[i]);
      if (si == CORE_DELAY_UNKNOWN)
	return false;
      int writes = 1;
      for (int j = 0; j < n; ++j)
	{
	  if (j == i || events[j].port == CP_NONE)
	    continue;
	  int sj = core_writeback_slot (events[j]);
	  if (sj == CORE_DELAY_UNKNOWN)
	    return false;
	  if (si != sj)
	    continue;
	  if (j > i)
	    ++writes;
	  /* Port sharing: two same-slot writes on one physical port.  */
	  bool i_sr = events[i].port == CP_SHARED_SIMPLE_ROUND;
	  bool j_sr = events[j].port == CP_SHARED_SIMPLE_ROUND;
	  bool i_mad = events[i].port == CP_BORROWS_MAD
	    || (events[i].port == CP_OWN && events[i].subunit == CSU_MAD);
	  bool j_mad = events[j].port == CP_BORROWS_MAD
	    || (events[j].port == CP_OWN && events[j].subunit == CSU_MAD);
	  if ((i_sr && j_sr) || (i_mad && j_mad))
	    return false;
	}
      if (writes > max_writes)
	return false;
    }
  return true;
}

/* Writeback/latency legality of one dependency edge: the consumer may
   not execute before the producer's result is readable.  Latencies and
   delays arrive as data; unknown values refuse.  */
inline bool
core_check_dependency (int producer_ready_slot, int consumer_exec_slot)
{
  if (producer_ready_slot == CORE_DELAY_UNKNOWN
      || consumer_exec_slot == CORE_DELAY_UNKNOWN)
    return false;
  return consumer_exec_slot > producer_ready_slot - 1
    && consumer_exec_slot >= producer_ready_slot;
}

/* Drain: greatest remaining writeback distance past the last issue slot;
   CORE_DELAY_UNKNOWN when any event's delay is unknown.  */
inline int
core_drain_slots (const core_event *events, int n, int last_issue_slot)
{
  int drain = 0;
  for (int i = 0; i < n; ++i)
    {
      int wb = core_writeback_slot (events[i]);
      if (wb == CORE_DELAY_UNKNOWN)
	return CORE_DELAY_UNKNOWN;
      if (wb - last_issue_slot > drain)
	drain = wb - last_issue_slot;
    }
  return drain;
}

}  /* namespace rvtt_macro_sched */

#endif /* GCC_RVTT_MACRO_SCHED_CORE_H */
