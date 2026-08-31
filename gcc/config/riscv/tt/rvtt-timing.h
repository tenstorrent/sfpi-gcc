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

/* FABLE_GOES_BURR.md item #11: ONE timing model instead of five.

   Pure functions over plain data in the rvtt-macro-sched-core.h style:
   no GIMPLE or RTL pointers, no GCC headers, no target facts.  Every
   audited timing fact (result latency, next-slot acceptance stall,
   word counts, delivery-cost constants) arrives as DATA from the
   caller, read once through rvtt_insn_effects / rvtt-cost.md at the
   consumer seam and never re-derived here.

   This module is the single computer of the execution-side
   stall/latency semantics that five hand-kept simulators previously
   maintained by convention (AUDIT-scheduling.md impr.1):

     1. the interlock fill's adjacency model
	(rtl-rvtt-schedule.cc adjacency_stall / audited_latency);
     2. the list scheduler's issue-timeline simulator
	(rtl-rvtt-schedule.cc ls_simulate);
     3. the cyclic steady-state II convergence probe
	(rtl-rvtt-schedule.cc ls_cyclic_ii);
     4. the pre-RA pressure scheduler's makespan mirror
	(rtl-rvtt-lp-schedule-prera.cc simulate_order);
     5. the replay pricing's interlocked-reissue scoreboard and the
	delivery-shape solver's downstream-mirror hoist arithmetic
	(rtl-rvtt-replay.cc exec_interlocked_slots + hoist_profitable_p
	shared model, rvtt-bnb.cc mirror_*_hoist_fires).

   Consumers keep their own IR walkers, dependence-discovery
   vocabularies (hard-reg sets, pseudo vectors, LREG masks), dumps and
   refusal names; the TIMING semantics live only here, so drift between
   models is structurally impossible.

   The audited-latency discipline is preserved exactly: a latency of -1
   means UNAUDITED and refuses (the rvtt.md `xtt_result_latency'
   attribute is encoded latency+1, 0 = unaudited; rvtt_insn_effects
   subtracts the bias before any value reaches this module -- attr "2"
   IS latency one, FH audit FHS-5).  The architectural next-slot
   acceptance stall (xtt_next_slot_stall, the SFPSWAP family) is
   outside the single-latency vocabulary and refuses through
   audited_latency; where it is instead PRICED (the replay reissue
   scoreboard, the crp stall-word two-slot rule) the caller passes it
   as data.

   Two precision tiers, callers choose (the item-#11 contract):
   cyclic_ii is initially the 6-copy convergence probe moved verbatim
   (CLASS-I); the RecMII/ResMII exact form arrives with item #5's MRT.

   Jointly owned seam with item #12 (one delivery-cost API): the hoist
   pricing forms below own the execution-side stall/latency simulation;
   the words->centislot economics constants arrive as data
   (hoist_costs) and migrate to item #12's module when it lands.

   The macro DAG scheduler's constraint core (rvtt-macro-sched-core.h)
   stays a calendar-occupancy legality model, not a timeline simulator;
   folding it in as this engine's inner layer is the item's documented
   optional later stage.  */

#ifndef GCC_RVTT_TIMING_H
#define GCC_RVTT_TIMING_H

#include <cstdint>
#include <vector>

namespace rvtt_timing
{

/* ------------------------------------------------------------------
   The audited-latency discipline (one spelling).  */

/* Audited result latency in issue slots; -1 refuses: opaque effects,
   an unaudited `xtt_result_latency' entry (RESULT_LATENCY < 0), or the
   architectural next-slot acceptance stall (lane BM: an instruction
   with the acceptance stall keeps refusing here even once it carries
   an audited result latency for the reissue-pricing model).  */

inline int
audited_latency (bool opaque, bool next_slot_stall, int result_latency)
{
  if (opaque)
    return -1;
  if (next_slot_stall)
    return -1;
  return result_latency;
}

/* ------------------------------------------------------------------
   Dependence-kind vocabulary (one spelling of the classification).
   The numeric values are load-bearing: consumers store and compare
   them exactly as the retired simulators did.  */

enum dep_kind
{
  DEP_NONE = 0,		/* independent */
  DEP_LATENCY = 1,	/* RAW or WAW: latency-weighted issue distance */
  DEP_ORDER = 2		/* WAR: issue-order only */
};

/* Classify the earlier node P against the later node C from the
   caller's register-vocabulary overlap facts: RAW_OR_WAW = P's defs
   intersect C's uses or defs; WAR = P's uses intersect C's defs.  */

inline dep_kind
classify_dependence (bool raw_or_waw, bool war)
{
  if (raw_or_waw)
    return DEP_LATENCY;
  if (war)
    return DEP_ORDER;
  return DEP_NONE;
}

/* Modeled interlock stall cycles between an issued producer and an
   immediately following issued consumer: 0 when independent, the
   producer's audited latency when dependent (-1 = refuse, the
   dependent-on-unaudited case).  The refuse-beyond-audited-window
   discipline (latency > 1 refuses the fill) stays with the callers'
   admission checks; this is the adjacency accounting itself.  */

inline int
adjacent_stall (bool dependent, int producer_latency)
{
  if (!dependent)
    return 0;
  return producer_latency;
}

/* ------------------------------------------------------------------
   Linear/cyclic region issue-timeline model.

   One issued node: WORDS issue slots occupied (multi-word instructions
   occupy their word count; a crp stall word arrives pre-charged at two
   slots -- the acceptance stall is an ISSUE fact the caller prices),
   LAT the audited result latency (callers admit only audited values),
   ENTRY_PIN the earliest issue slot (boundary floor from the entry
   producer's shadow).  */

struct op
{
  int words = 0;
  int lat = 0;
  int entry_pin = 0;
};

/* A region: OPS in original index order plus the dependence matrix
   DEP, row-major n*n, where dep(i, j) constrains node j when node i
   issues EARLIER (both directions are stored; the relation is not
   symmetric).  */

struct seq
{
  std::vector<op> ops;
  std::vector<unsigned char> dep;

  dep_kind
  kind (unsigned i, unsigned j) const
  {
    return static_cast<dep_kind> (dep[i * ops.size () + j]);
  }
};

/* Modeled issue timeline of S in the order given by ORDER (indices
   into S.ops; a permutation).  Fills issue slots into *ISSUE (indexed
   like S.ops) and returns the modeled end: the last occupied slot
   boundary plus the trailing shadow of every node in EXIT_SHADOW
   (nodes feeding the exit consumer, or all nodes when the region ends
   the block).  Entry pins are honored.  */

extern int simulate (const seq &s, const std::vector<int> &order,
		     std::vector<int> *issue,
		     const std::vector<bool> &exit_shadow);

/* Steady-state initiation interval of S issued repeatedly in ORDER:
   the wrapped (cyclic) issue model of a self-loop row.  Entry pins do
   not apply (the seam is the model); dependences reach across copies
   through the same dependence matrix.  Initially the 6-copy
   convergence probe moved verbatim (CLASS-I tier); the exact
   RecMII/ResMII form is the later precision tier.  */

extern int cyclic_ii (const seq &s, const std::vector<int> &order);

/* ------------------------------------------------------------------
   In-order interlocked-reissue scoreboard (the replay pricing model).

   One issued word-group: DEPS the register mask this op waits on
   (reads, plus lane-predicated writes -- the caller's dependence
   definition), WRITES the register mask it defines, WORDS its issue
   slots, LAT its audited result latency (< 0 = unaudited producer:
   its destinations become unproved), NEXT_SLOT_STALL the
   architectural acceptance stall (the next instruction issues one
   slot late -- here it is PRICED, not refused).  */

const unsigned INTERLOCK_REGS = 16;

struct issue_op
{
  uint32_t deps = 0;
  uint32_t writes = 0;
  int words = 0;
  int lat = 0;
  bool next_slot_stall = false;
};

/* The 16-register ready[] scoreboard, stepped one op at a time so the
   caller's per-insn dumps and refusals keep their original stream
   order.  step() returns false -- leaving the state untouched, so
   unproved_mask() still reports the refusing state -- when OP consumes
   a register whose pending producer is unaudited (the caller refuses
   by name).  slots() is the interlocked issue-slot count so far.  */

class interlock_sim
{
public:
  interlock_sim () { reset (); }

  void
  reset ()
  {
    m_slot = 0;
    m_unproved = 0;
    for (unsigned i = 0; i != INTERLOCK_REGS; ++i)
      m_ready[i] = 0;
  }

  bool
  step (const issue_op &op)
  {
    uint32_t deps = op.deps & 0xFFFF;
    if (deps & m_unproved)
      return false;
    int64_t at = m_slot;
    for (unsigned i = 0; i != INTERLOCK_REGS; ++i)
      if ((deps & (1u << i)) && m_ready[i] > at)
	at = m_ready[i];
    int64_t done = at + op.words;
    if (op.next_slot_stall)
      ++done;
    for (unsigned i = 0; i != INTERLOCK_REGS; ++i)
      if (op.writes & (1u << i))
	{
	  if (op.lat < 0)
	    m_unproved |= 1u << i;
	  else
	    {
	      m_unproved &= ~(1u << i);
	      m_ready[i] = done + op.lat;
	    }
	}
    m_slot = done;
    return true;
  }

  int64_t slots () const { return m_slot; }
  uint32_t unproved_mask () const { return m_unproved; }

private:
  int64_t m_slot;
  int64_t m_ready[INTERLOCK_REGS];
  uint32_t m_unproved;
};

/* ------------------------------------------------------------------
   Replay-hoist pricing forms (the ds_* downstream-mirror seam).

   One spelling of the execution-side hoist arithmetic that
   rtl-rvtt-replay.cc's gate and rvtt-bnb.cc's delivery-shape
   downstream mirror previously maintained as separate copies that
   "must agree by convention".  The RTL gate passes its scoreboard
   slot count; the mirror passes its ds_exec prediction -- prediction,
   never re-pricing, exactly as before, but through one formula.

   All costs are centislots (hundredths of a Tensix issue slot),
   int64.  The constants arrive as data (rvtt-cost.md
   XTT_REPLAY_COST_* carriers); their words->centislot economics
   migrate to item #12's delivery-cost module when it lands.  */

struct hoist_costs
{
  int64_t push = 0;		/* XTT_REPLAY_COST_RISC_PUSH_X100 */
  int64_t slot = 0;		/* XTT_REPLAY_COST_REPLAY_SLOT_X100 */
  int64_t turnaround = 0;	/* XTT_REPLAY_COST_TURNAROUND_X100 */
  int64_t record_overhead = 0;	/* XTT_REPLAY_COST_RECORD_OVERHEAD_X100 */
};

struct hoist_pricing
{
  int64_t deliver_body = 0;	/* words * push */
  int64_t deliver_record = 0;	/* (1 + words) * push */
  int64_t exec = 0;		/* exec_slots * slot */
  int64_t before = 0;
  int64_t after = 0;
  int64_t record = 0;
  int64_t benefit = 0;		/* trips * (before - after) - record */
  bool exec_bound = false;	/* re-record: execution-bound branch */
  bool hidden = false;		/* delivery-bound: record delivery hidden */
  int64_t surplus = 0;		/* delivery-bound saturation surplus */
};

/* Counted-loop capture branch (the body records nothing per trip;
   rvtt-cost.md counted-loop derivation).  */

extern hoist_pricing counted_hoist_price (const hoist_costs &c,
					  int64_t trips, int64_t words,
					  int64_t exec_slots);

/* Re-record body branches (execution-bound vs delivery-bound with the
   execution-saturation context term; rvtt-cost.md re-record
   derivation).  COMPLETION_GUARD charges the hoisted record's full
   delivery on the execution-bound branch (the completion-accurate
   shared model); the downstream mirror predicts the default model and
   passes false.  */

extern hoist_pricing rerecord_hoist_price (const hoist_costs &c,
					   int64_t trips, int64_t words,
					   int64_t exec_slots,
					   int64_t launch_run,
					   bool completion_guard);

} // namespace rvtt_timing

#endif /* GCC_RVTT_TIMING_H */
