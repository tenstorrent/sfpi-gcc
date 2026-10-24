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

/* Shared timing model for Tensix SFPU scheduling.

   Pure functions over plain data, with no GIMPLE, RTL or other GCC types,
   so that the standalone unit test rvtt-timing-test.cc can include it.
   Timing facts (result latency, next-slot acceptance stall, word counts,
   delivery-cost constants) are passed in by the caller, which reads them
   from rvtt_insn_effects and rvtt-cost.md.

   The module provides the interlock stall between a producer and its
   immediate consumer, the issue timeline of a straight-line region, the
   steady-state initiation interval of a self-loop row (a cheap
   simulation, cyclic_ii, and an exact modulo-scheduling form), the
   in-order reissue scoreboard used to price replay, and the replay-hoist
   pricing formulas.  Keeping these in one place means the schedulers and
   the cost models cannot disagree about them.

   A latency of -1 means unknown and makes the caller refuse.  The rvtt.md
   `xtt_result_latency' attribute is encoded as latency + 1 with 0 meaning
   unknown; rvtt_insn_effects removes the bias, so an attribute value of
   "2" is latency one.  The next-slot acceptance stall (xtt_next_slot_stall,
   the SFPSWAP family) does not fit a single latency and is refused by
   audited_latency; the replay scoreboard instead prices it as an extra
   issue slot.  */

#ifndef GCC_RVTT_TIMING_H
#define GCC_RVTT_TIMING_H

#include <cstdint>
#include <vector>

namespace rvtt_timing
{

/* ------------------------------------------------------------------
   Result latency.  */

/* Result latency in issue slots, or -1 when OPAQUE, when RESULT_LATENCY is
   unknown (< 0), or when the instruction has a next-slot acceptance stall
   (NEXT_SLOT_STALL), even if it has a known result latency for the
   reissue scoreboard.  */

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
   Dependence kinds.  Consumers store and compare the numeric values.  */

enum dep_kind
{
  DEP_NONE = 0,		/* independent */
  DEP_LATENCY = 1,	/* RAW or WAW: latency-weighted issue distance */
  DEP_ORDER = 2		/* WAR: issue-order only */
};

/* Classify the dependence of a later node C on an earlier node P.
   RAW_OR_WAW is true if P's defs intersect C's uses or defs; WAR is true
   if P's uses intersect C's defs.  */

inline dep_kind
classify_dependence (bool raw_or_waw, bool war)
{
  if (raw_or_waw)
    return DEP_LATENCY;
  if (war)
    return DEP_ORDER;
  return DEP_NONE;
}

/* Interlock stall cycles between a producer and the consumer issued
   immediately after it: 0 when not DEPENDENT, otherwise PRODUCER_LATENCY
   (-1 if that is unknown).  Callers decide which latencies they
   accept.  */

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
   The caller obtains WORDS and LAT from rvtt_builtin_result_latency.  */

/* Smallest split factor that makes the recurrence issue-bound:
   ceil((WORDS + LAT) / WORDS), or -1 if the latency is unknown.  */

inline int
accum_split_factor (int words, int producer_latency)
{
  if (producer_latency < 0 || words <= 0)
    return -1;
  return (words + producer_latency + words - 1) / words;
}

/* Slots per iteration saved by splitting the K-link chain into P
   partials: the serial bound minus the split bound (itself floored by the
   issue bound).  A result <= 0 means splitting does not pay.  */

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

   One issued node: WORDS issue slots occupied (an instruction with an
   acceptance stall is passed in as two slots), LAT its result latency, and
   ENTRY_PIN the earliest slot it may issue in (from the shadow of a
   producer before the region).  */

struct op
{
  int words = 0;
  int lat = 0;
  int entry_pin = 0;
};

/* A region: OPS in original index order plus the dependence matrix
   DEP, row-major n*n, where dep(i, j) constrains node j when node i
   issues earlier (both directions are stored; the relation is not
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

/* Issue timeline of S in the order given by ORDER (a permutation of
   indices into S.ops).  Fills issue slots into *ISSUE (indexed like
   S.ops) and returns the end of the region: the last occupied slot
   boundary plus the trailing shadow of every node in EXIT_SHADOW
   (nodes feeding the exit consumer, or all nodes when the region ends
   the block).  Entry pins are honored.  */

extern int simulate (const seq &s, const std::vector<int> &order,
		     std::vector<int> *issue,
		     const std::vector<bool> &exit_shadow);

/* Steady-state initiation interval of S issued repeatedly in ORDER, as
   for a self-loop row.  Entry pins do not apply; dependences reach across
   copies through the same matrix.  This simulates six copies; the exact
   RecMII/ResMII form is in the modulo-scheduling section below.  */

extern int cyclic_ii (const seq &s, const std::vector<int> &order);

/* ------------------------------------------------------------------
   In-order interlocked reissue scoreboard, used to price replay.

   One issued word group: DEPS the register mask this op waits on (reads,
   plus lane-predicated writes), WRITES the register mask it defines,
   WORDS its issue slots, LAT its result latency (< 0 if unknown, which
   makes its destinations unknown), NEXT_SLOT_STALL the acceptance stall
   (the next instruction issues one slot late).  */

const unsigned INTERLOCK_REGS = 16;

struct issue_op
{
  uint32_t deps = 0;
  uint32_t writes = 0;
  int words = 0;
  int lat = 0;
  bool next_slot_stall = false;
};

/* A 16-register ready-time scoreboard, stepped one op at a time so that
   the caller's dumps and refusals stay in stream order.  step() returns
   false, leaving the state unchanged, when OP reads a register whose
   pending producer has an unknown latency.  slots() is the issue-slot
   count so far.  */

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
   Replay-hoist pricing.

   Shared by the RTL replay-hoist decision (rtl-rvtt-replay.cc) and by the
   delivery-shape solver's prediction of that decision (rvtt-bnb.cc), so
   that the two cannot disagree.  The RTL pass passes its scoreboard slot
   count; the solver passes its predicted execution slots.

   All costs are centislots (hundredths of a Tensix issue slot).  The
   constants come from the rvtt-cost.md XTT_REPLAY_COST_* attributes.  */

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
   delivery on the execution-bound branch; the delivery-shape solver
   predicts the default and passes false.  */

extern hoist_pricing rerecord_hoist_price (const hoist_costs &c,
					   int64_t trips, int64_t words,
					   int64_t exec_slots,
					   int64_t launch_run,
					   bool completion_guard);

/* ------------------------------------------------------------------
   Modulo scheduling.

   Rau's Iterative Modulo Scheduling (Rau, MICRO-27 1994) over the same
   dependences that cyclic_ii uses.  make_mod_prob builds the dependence
   graph from a seq: an intra-iteration edge i->j (omega 0) for every pair
   i < j the matrix constrains, and a cross-iteration edge a->b (omega 1,
   self-dependences included) for every constrained pair, because every
   word of one iteration issues before every word of the next.
   Dependence distances greater than one are never assumed.

   The front end issues one slot per cycle, so the modulo reservation
   table has II columns and an op occupies `words' consecutive columns
   modulo II; ResMII is the body's total slot count.  RecMII is the
   smallest II at which the graph with edge weights (delta - II*omega)
   has no positive cycle; feasibility is monotone in II, so it is found
   by binary search.

   For modulo variable expansion (Lam, PLDI 1988), mve_kmin gives the
   number of kernel copies, ceil (max lifetime / II), and mve_live_demand
   the peak number of simultaneously live value copies, which the caller
   checks against the register file.  Lifetimes come from DEP_LATENCY
   edges; since those include WAW, a computed lifetime can only be too
   long, which is conservative.

   All loops run in index order, so results depend only on the
   problem.  */

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

/* Build the modulo-scheduling problem for S.  */

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

/* As above, but with the cross-iteration dependences taken from CROSS
   (only CROSS.dep is used; the ops come from INTRA).  The single-matrix
   form treats every constrained pair, WAW and WAR included, as
   cross-iteration, which is right for pricing an overlap but too strict
   for building the placement that modulo variable expansion will rename:
   the register reuse that the renaming removes must not constrain it.
   The caller passes in CROSS only the constraints that survive renaming
   (e.g. registers live into the row).  Any optimism here only affects
   which candidates are generated; the resulting order is checked
   afterwards by the timing model and the caller's legality checks.  */

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
   a problem infeasible at every II (an intra-iteration positive cycle,
   which make_mod_prob never produces).  */

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

  /* Defensive re-check: every constraint of the committed placement is
     verified again.  */
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

} /* namespace rvtt_timing */

#endif /* GCC_RVTT_TIMING_H */
