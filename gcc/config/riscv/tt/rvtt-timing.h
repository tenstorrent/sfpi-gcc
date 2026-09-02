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

/* ONE timing model instead of five.

   Pure functions over plain data in the rvtt-macro-sched-core.h style:
   no GIMPLE or RTL pointers, no GCC headers, no target facts.  Every
   audited timing fact (result latency, next-slot acceptance stall,
   word counts, delivery-cost constants) arrives as DATA from the
   caller, read once through rvtt_insn_effects / rvtt-cost.md at the
   consumer seam and never re-derived here.

   This module is the single computer of the execution-side
   stall/latency semantics that five hand-kept simulators previously
   maintained by convention:

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

   Two precision tiers, callers choose (the module contract):
   cyclic_ii is the 6-copy convergence probe moved verbatim (CLASS-I,
   the acceptance oracle); the exact tier (ResMII / RecMII / the Rau
   iterative-modulo-scheduling placement over an II-column modulo
   reservation table) lives below in the modulo-scheduling
   section -- same seq/dep vocabulary, one marshaller (make_mod_prob),
   so the MRT and the acceptance simulator cannot drift.

   Jointly owned seam with the delivery-cost API: the hoist
   pricing forms below own the execution-side stall/latency simulation;
   the words->centislot economics constants arrive as data
   (hoist_costs) and belong to rvtt-delivery-cost-core.h.

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
   architectural next-slot acceptance stall (an instruction
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
   Loop-carried accumulator splitting arithmetic.

   A loop-carried associative chain of K links per iteration, each
   link WORDS issue slots with audited result latency LAT, has the
   serial recurrence bound K * (WORDS + LAT) slots per iteration
   (every link waits for the previous link's writeback).  Split into P
   round-robin partial accumulators the recurrence bound becomes
   ceil(K/P) * (WORDS + LAT), floored by the issue bound K * WORDS.
   Pure data-in/data-out: the caller reads WORDS/LAT once at its
   audited seam (rvtt_builtin_result_latency) and admits only audited
   values.  */

/* Smallest split factor that makes the recurrence issue-bound:
   ceil((WORDS + LAT) / WORDS).  -1 refuses on an unaudited latency.  */

inline int
accum_split_factor (int words, int producer_latency)
{
  if (producer_latency < 0 || words <= 0)
    return -1;
  return (words + producer_latency + words - 1) / words;
}

/* Modeled recurrence-bound slots per iteration saved by splitting the
   K-link chain into P partials: serial bound minus the split bound
   (itself floored by the issue bound).  <= 0 = unprofitable.  */

inline int64_t
accum_split_saving (int64_t k, int64_t p, int64_t words, int64_t lat)
{
  if (k <= 0 || p <= 0 || words <= 0 || lat < 0)
    return 0;
  int64_t serial = k * (words + lat);
  int64_t split = (k + p - 1) / p * (words + lat);
  int64_t issue = k * words;
  return serial - (split > issue ? split : issue);
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
   belong to the delivery-cost module.  */

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

/* ------------------------------------------------------------------
   Modulo-scheduling exact tier.

   Rau's Iterative Modulo Scheduling (Rau, MICRO-27 1994) over the SAME
   dependence vocabulary the acceptance simulator (cyclic_ii above)
   consumes: one marshaller, make_mod_prob, derives the dependence-
   distance graph from a seq exactly as the 6-copy probe consults its
   matrix -- an intra-iteration edge i->j (omega 0) for every ordered
   pair i < j the matrix constrains, and a cross-iteration edge a->b
   (omega 1, the diagonal's cross-copy self-dependence included) for
   EVERY constrained pair, because in the wrapped stream every word of
   an earlier iteration issues before every word of a later one.
   Deeper dependence distances are never manufactured here (the
   consumer's `ims-dependence-distance-unproven' contract).

   The reservation model is the single-issue Tensix front end: the MRT
   is II issue-slot columns, an op occupies `words' consecutive columns
   modulo II (a priced acceptance-stall word arrives with its extra
   slot already in `words', exactly as in the acceptance model), so
   ResMII is the body's total issue-slot count.  RecMII is exact: the
   smallest II at which the constraint graph with edge weights
   (delta - II*omega) carries no positive cycle (longest-path
   feasibility; monotone in II, so binary search).

   Modulo variable expansion (Lam, PLDI 1988) bookkeeping: mve_kmin is
   the kernel-copy count ceil(maxlifetime/II) the placement's value
   lifetimes demand, and mve_live_demand the peak simultaneously-live
   value-copy count of the steady state -- the consumer prices it
   against the register file (the pressure engine's capacity) and
   refuses `mve-rename-exhausted' when it does not fit.  Lifetimes are read
   from DEP_LATENCY edges; the vocabulary merges RAW and WAW, so a
   WAW-only edge can only LENGTHEN a computed lifetime -- conservative
   in the refusing direction, never admitting.

   Pure data throughout: header-inline, no GCC or IR types, so the
   standalone unit test (rvtt-timing-test.cc) compiles this section
   directly.  All loops are index-ordered; results are deterministic
   functions of the problem alone.  */

struct mod_edge
{
  unsigned from = 0, to = 0;
  int delta = 0;	/* required issue-slot distance */
  int omega = 0;	/* iteration distance (0 or 1) */
  dep_kind kind = DEP_NONE;
};

struct mod_prob
{
  std::vector<int> words;	/* issue slots per node */
  std::vector<mod_edge> edges;
};

/* The one marshaller from the acceptance vocabulary.  */

inline mod_prob
make_mod_prob (const seq &s)
{
  mod_prob p;
  const unsigned n = s.ops.size ();
  p.words.resize (n);
  for (unsigned i = 0; i != n; ++i)
    p.words[i] = s.ops[i].words;
  for (unsigned a = 0; a != n; ++a)
    for (unsigned b = 0; b != n; ++b)
      {
	dep_kind kind = s.kind (a, b);
	if (kind == DEP_NONE)
	  continue;
	int delta = s.ops[a].words
		    + (kind == DEP_LATENCY ? s.ops[a].lat : 0);
	mod_edge e;
	e.from = a;
	e.to = b;
	e.delta = delta;
	e.kind = kind;
	if (a < b)
	  {
	    e.omega = 0;	/* original program order */
	    p.edges.push_back (e);
	  }
	e.omega = 1;		/* the wrapped-stream constraint */
	p.edges.push_back (e);
      }
  return p;
}

/* Realization-tier marshaller (modulo variable expansion): identical edge
   derivation, with the CROSS-iteration kind matrix supplied as its own
   seq (ops shared with INTRA; only CROSS.dep is consulted for the
   omega-1 edges).  The single-matrix marshaller above wraps the kernel
   onto its own STORAGE -- every constrained pair, WAW/WAR included --
   which is exactly right for pricing an unrealized overlap (stage 1's
   conservative bookkeeping) and exactly wrong for generating the
   realized placement: the storage collisions the modulo-variable-
   expansion ROTATION removes must not bound the placement it exists to
   enable.  The caller supplies in CROSS only the cross-iteration
   constraints that survive its rotation vocabulary (e.g. registers
   live into the row); everything dropped here is optimism in the
   CANDIDATE-GENERATION direction only -- the realized order is judged
   by the exact acceptance model and the caller's legality/lockstep
   belts downstream, so a wrong optimism can only produce a refused
   candidate, never an unsound commit.  */

inline mod_prob
make_mod_prob (const seq &intra, const seq &cross)
{
  mod_prob p;
  const unsigned n = intra.ops.size ();
  p.words.resize (n);
  for (unsigned i = 0; i != n; ++i)
    p.words[i] = intra.ops[i].words;
  for (unsigned a = 0; a != n; ++a)
    for (unsigned b = 0; b != n; ++b)
      {
	dep_kind k0 = intra.kind (a, b);
	if (k0 != DEP_NONE && a < b)
	  {
	    mod_edge e;
	    e.from = a;
	    e.to = b;
	    e.delta = intra.ops[a].words
		      + (k0 == DEP_LATENCY ? intra.ops[a].lat : 0);
	    e.kind = k0;
	    e.omega = 0;
	    p.edges.push_back (e);
	  }
	dep_kind k1 = cross.kind (a, b);
	if (k1 != DEP_NONE)
	  {
	    mod_edge e;
	    e.from = a;
	    e.to = b;
	    e.delta = intra.ops[a].words
		      + (k1 == DEP_LATENCY ? intra.ops[a].lat : 0);
	    e.kind = k1;
	    e.omega = 1;
	    p.edges.push_back (e);
	  }
      }
  return p;
}

/* Resource-minimum II of the single-issue front end: the body's total
   issue-slot count.  */

inline int
resmii (const mod_prob &p)
{
  int sum = 0;
  for (unsigned i = 0; i != p.words.size (); ++i)
    sum += p.words[i];
  return sum;
}

/* Longest-path feasibility of candidate II: no positive cycle under
   edge weights (delta - II*omega).  */

inline bool
mod_ii_feasible (const mod_prob &p, int ii)
{
  const unsigned n = p.words.size ();
  std::vector<long> x (n, 0);
  for (unsigned pass = 0; pass <= n; ++pass)
    {
      bool changed = false;
      for (unsigned k = 0; k != p.edges.size (); ++k)
	{
	  const mod_edge &e = p.edges[k];
	  long need = x[e.from] + e.delta - (long) ii * e.omega;
	  if (need > x[e.to])
	    {
	      x[e.to] = need;
	      changed = true;
	    }
	}
      if (!changed)
	return true;
    }
  return false;
}

/* Exact recurrence-minimum II: the smallest feasible II (monotone in
   II -- raising II only loosens omega-carrying edges).  Returns -1 on
   a problem infeasible at every II (an intra-iteration positive cycle;
   impossible for a program-order marshalling, kept fail-closed).  */

inline int
recmii (const mod_prob &p)
{
  long cap = 1;
  for (unsigned k = 0; k != p.edges.size (); ++k)
    if (p.edges[k].delta > 0)
      cap += p.edges[k].delta;
  if (!mod_ii_feasible (p, (int) cap))
    return -1;
  int lo = 0, hi = (int) cap;
  while (lo < hi)
    {
      int mid = lo + (hi - lo) / 2;
      if (mod_ii_feasible (p, mid))
	hi = mid;
      else
	lo = mid + 1;
    }
  return lo;
}

struct mod_placement
{
  bool scheduled = false;
  bool budget_exhausted = false;
  int ii = 0;
  std::vector<int> sigma;	/* absolute issue slots */
};

/* One Rau IMS attempt at fixed II under an eviction BUDGET (total
   placements).  Deterministic: height-directed priority (longest path
   over (delta - II*omega) weights), index order on ties; the modulo
   reservation table is II single-issue columns; an op finding no
   conflict-free slot in its II-wide window force-places and evicts the
   occupants and any dependence-violated successors (Rau's rule).
   Returns false with *EXHAUSTED set when the budget runs out.  */

inline bool
ims_try (const mod_prob &p, int ii, int budget, std::vector<int> *sigma_out,
	 bool *exhausted)
{
  const unsigned n = p.words.size ();
  *exhausted = false;
  if (ii <= 0)
    return false;
  for (unsigned v = 0; v != n; ++v)
    if (p.words[v] > ii)
      return false;

  /* Heights at this II (bounded iff II is feasible).  */
  std::vector<long> height (n, 0);
  for (unsigned pass = 0;; ++pass)
    {
      bool changed = false;
      for (unsigned k = 0; k != p.edges.size (); ++k)
	{
	  const mod_edge &e = p.edges[k];
	  long h = height[e.to] + e.delta - (long) ii * e.omega;
	  if (h > height[e.from])
	    {
	      height[e.from] = h;
	      changed = true;
	    }
	}
      if (!changed)
	break;
      if (pass > n)
	return false;		/* infeasible II */
    }

  /* Priority: height descending, index ascending.  */
  std::vector<unsigned> prio (n);
  for (unsigned i = 0; i != n; ++i)
    prio[i] = i;
  for (unsigned i = 1; i < n; ++i)	/* stable insertion sort */
    {
      unsigned v = prio[i];
      unsigned j = i;
      while (j > 0 && (height[prio[j - 1]] < height[v]
		       || (height[prio[j - 1]] == height[v]
			   && prio[j - 1] > v)))
	{
	  prio[j] = prio[j - 1];
	  --j;
	}
      prio[j] = v;
    }

  std::vector<int> sigma (n, -1);
  std::vector<int> prev (n, -1);
  std::vector<int> owner (ii, -1);	/* MRT column -> op or -1 */

  auto unschedule = [&] (unsigned w)
  {
    for (int c = 0; c != p.words[w]; ++c)
      {
	int col = (sigma[w] + c) % ii;
	if (owner[col] == (int) w)
	  owner[col] = -1;
      }
    sigma[w] = -1;
  };

  unsigned placed = 0;
  while (placed != n)
    {
      /* Highest-priority unscheduled op.  */
      unsigned v = n;
      for (unsigned k = 0; k != n; ++k)
	if (sigma[prio[k]] < 0)
	  {
	    v = prio[k];
	    break;
	  }
      if (budget-- <= 0)
	{
	  *exhausted = true;
	  return false;
	}

      /* Earliest start from scheduled predecessors.  */
      long estart = 0;
      for (unsigned k = 0; k != p.edges.size (); ++k)
	{
	  const mod_edge &e = p.edges[k];
	  if (e.to != v || e.from == v || sigma[e.from] < 0)
	    continue;
	  long need = sigma[e.from] + e.delta - (long) ii * e.omega;
	  if (need > estart)
	    estart = need;
	}

      /* First conflict-free slot in the II-wide window.  */
      long t = -1;
      for (long s = estart; s != estart + ii; ++s)
	{
	  bool free_slot = true;
	  for (int c = 0; c != p.words[v] && free_slot; ++c)
	    free_slot = owner[(s + c) % ii] < 0;
	  if (free_slot)
	    {
	      t = s;
	      break;
	    }
	}
      if (t < 0)
	t = (prev[v] < 0 || estart > prev[v]) ? estart : prev[v] + 1;

      /* Evict MRT occupants of the chosen columns.  */
      for (int c = 0; c != p.words[v]; ++c)
	{
	  int col = (int) ((t + c) % ii);
	  if (owner[col] >= 0 && owner[col] != (int) v)
	    unschedule ((unsigned) owner[col]);
	}
      sigma[v] = (int) t;
      prev[v] = (int) t;
      for (int c = 0; c != p.words[v]; ++c)
	owner[(t + c) % ii] = (int) v;

      /* Evict dependence-violated scheduled successors (predecessor
	 constraints hold by estart).  */
      for (unsigned k = 0; k != p.edges.size (); ++k)
	{
	  const mod_edge &e = p.edges[k];
	  if (e.from != v || e.to == v || sigma[e.to] < 0)
	    continue;
	  if ((long) sigma[e.to] < sigma[v] + e.delta - (long) ii * e.omega)
	    unschedule (e.to);
	}

      placed = 0;
      for (unsigned k = 0; k != n; ++k)
	if (sigma[k] >= 0)
	  ++placed;
    }

  /* Belt: every constraint of the committed placement re-verified.  */
  for (unsigned k = 0; k != p.edges.size (); ++k)
    {
      const mod_edge &e = p.edges[k];
      if ((long) sigma[e.to] < sigma[e.from] + e.delta - (long) ii * e.omega)
	return false;
    }
  *sigma_out = sigma;
  return true;
}

/* Iterate II from MII to MAX_II; the first success wins.  */

inline mod_placement
ims_schedule (const mod_prob &p, int mii, int max_ii, int budget)
{
  mod_placement pl;
  if (mii < 1)
    mii = 1;
  for (int ii = mii; ii <= max_ii; ++ii)
    {
      bool exhausted = false;
      std::vector<int> sigma;
      if (ims_try (p, ii, budget, &sigma, &exhausted))
	{
	  pl.scheduled = true;
	  pl.ii = ii;
	  pl.sigma = sigma;
	  return pl;
	}
      if (exhausted)
	pl.budget_exhausted = true;
    }
  return pl;
}

/* MVE kernel-copy count: ceil of the longest value lifetime over the
   II.  Lifetimes are DEP_LATENCY producer-to-consumer spans of the
   placement (>= delta by construction).  */

inline int
mve_kmin (const mod_prob &p, const mod_placement &pl)
{
  long maxlife = 0;
  for (unsigned k = 0; k != p.edges.size (); ++k)
    {
      const mod_edge &e = p.edges[k];
      if (e.kind != DEP_LATENCY || e.from == e.to)
	continue;
      long life = pl.sigma[e.to] + (long) pl.ii * e.omega
		  - pl.sigma[e.from];
      if (life > maxlife)
	maxlife = life;
    }
  if (maxlife <= 0)
    return 1;
  return (int) ((maxlife + pl.ii - 1) / pl.ii);
}

/* Peak simultaneously-live value-copy count of the steady state: at
   each modulo phase, every producer contributes one copy per started-
   but-undrained iteration of its longest lifetime.  */

inline unsigned
mve_live_demand (const mod_prob &p, const mod_placement &pl)
{
  const unsigned n = p.words.size ();
  std::vector<long> life (n, 0);
  for (unsigned k = 0; k != p.edges.size (); ++k)
    {
      const mod_edge &e = p.edges[k];
      if (e.kind != DEP_LATENCY || e.from == e.to)
	continue;
      long l = pl.sigma[e.to] + (long) pl.ii * e.omega - pl.sigma[e.from];
      if (l > life[e.from])
	life[e.from] = l;
    }
  unsigned peak = 0;
  for (int s = 0; s != pl.ii; ++s)
    {
      unsigned live = 0;
      for (unsigned v = 0; v != n; ++v)
	{
	  if (life[v] <= 0)
	    continue;
	  long r = ((long) s - pl.sigma[v]) % pl.ii;
	  if (r < 0)
	    r += pl.ii;
	  if (life[v] > r)
	    live += (unsigned) ((life[v] - r - 1) / pl.ii + 1);
	}
      if (live > peak)
	peak = live;
    }
  return peak;
}

} // namespace rvtt_timing

#endif /* GCC_RVTT_TIMING_H */
