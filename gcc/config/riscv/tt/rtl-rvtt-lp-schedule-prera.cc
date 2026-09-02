/* Pre-allocation pressure-cost list scheduling for Tensix SFPU regions.
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

/* -mtt-tensix-optimize-pressure-schedule-prera (default off).

   THE SEAM.  The post-allocation list scheduler (rtl-rvtt-schedule.cc)
   is honest that it cannot carry a pressure gate: after allocation the
   eight hard LREGs and their WAR/WAW edges ARE the bound, and
   "a pressure-aware dispatch over virtual registers is the
   pre-allocation scheduler's contract".  This pass is that contract:
   it runs on PSEUDO-register RTL immediately before the pre-IRA
   allocator stack (rvtt_lreg_livein reservations, rvtt_lp_alloc
   coloring, IRA), where reordering can still turn a
   ten-simultaneously-live instruction order into an
   eight-register-schedulable one -- the difference between a clean
   allocation and a spill (or the named lreg-pressure-exceeded
   refusal).

   THE ALGORITHM is GCC's own SCHED_PRESSURE_MODEL (haifa-sched.cc,
   "This is the first page of code related to SCHED_PRESSURE_MODEL"),
   adapted to the rvtt audited-region model:

     1. A MODEL SCHEDULE is built for each region, "chosen solely to
	keep register pressure down" (greedy minimum-candidate-peak
	dispatch; ties by original order).  It is not applied; it
	records the achievable maximum pressure MP.  This is the CSR
	half of Goodman & Hsu's integrated code scheduling and register
	allocation (ICS 1988).
     2. The REAL candidate is a list schedule ranked by
	ECC (insn) + insn_delay (insn): the pressure-based excess cost
	change, "effectively measured in cycles", plus the stall the
	insn would introduce if issued now.  Pressure below
	MAX (MP, 8) is free; each register the issue would push beyond
	that limit costs one unit, and a pressure-reducing issue above
	the limit earns credit.  This is the CSP/CSR switch driven by
	the current live count.
     3. TRANSACTIONAL ACCEPTANCE: a candidate is committed only when
	BOTH its modeled peak pressure and its modeled makespan are
	non-worse than the original order's, with a STRICT decrease in
	at least one; otherwise the original chain is restored exactly,
	debug insns included.  When the ECC candidate fails the joint
	predicate the model schedule itself is offered as the fallback
	candidate under the same predicate (haifa cannot do this --
	it schedules in place; a transactional pass can).

   PRESSURE GROUND TRUTH.  The unit of pressure is the simultaneous
   liveness of XTT32SI allocation units (SFPU vector pseudos plus any
   live hard LREG) -- the same count rtl-rvtt-lp-alloc.cc audits and
   the same quantity the lane-DS pressure oracle
   (testsuite lregalloc/tools/lreg_pressure_oracle.py) machine-checks
   through the rvtt_prgm_const SSA model.  The pass never trusts its
   own bookkeeping: the region's baseline per-point pressure is
   recomputed independently from the DF live-register problem (backward
   simulation from DF_LR_OUT), and a disagreement with the scheduler's
   model REFUSES by name (pressure-oracle-disagreement) before any
   mutation; after a commit the same DF recount must reproduce the
   candidate's claimed peak or the chain is restored exactly and the
   region refuses by the same name.

   REGION MODEL AND BARRIERS are inherited from the post-RA list
   scheduler's audited-region discipline, evaluated over pseudos:
   maximal straight-line runs of issued Tensix instructions with
   non-opaque typed effects, no CC write, no configuration access, no
   RWC step, no Dst memory access, no static-delay contract, and an
   AUDITED result latency inside the modeled window (latency 0 or 1).
   Every inadmissible instruction bounds the region under its barrier
   name; a node whose latency is unaudited refuses as
   pressure-model-unaudited-producer (the audited latency table is the
   throttle); an explicit replay-buffer owner ends eligibility for the
   rest of the block (fixed captures record following delivered words
   by position).  Self-loop blocks defer whole by name (the cyclic row
   seam is capture rotation's audited territory) and repeated region
   insn-code signatures inside one block defer by name (unrolled row
   copies must stay textually isomorphic for the replay and MOP
   re-rolls, which run post-allocation on this stream's descendant).

   MAKESPAN is the post-RA scheduler's model evaluated over pseudo
   dependences: issue slots (word counts) plus audited interlock
   shadows on RAW/WAW edges, an entry-producer latency floor, baseline
   pinning against an unaudited entry producer, and exit-consumer
   trailing shadows (a block-ending region drains every node).  The
   post-RA pad-site commit guard has no analog here: WH SFPNOP pads are
   inserted after allocation on the final stream, which the post-RA
   phases re-verify on their own.

   Purely structural: no operation identity, opcode calendar,
   coefficient value, or instruction-word fingerprint participates.  */

#include "config.h"
#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "memmodel.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "emit-rtl.h"
#include "function.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-timing.h"

namespace {

/* ------------------------- pressure units -------------------------- */

/* An XTT32SI allocation unit: an SFPU vector pseudo or a live hard
   LREG.  Mirror of rtl-rvtt-lp-alloc.cc xtt32_allocation_unit_p (the
   allocator this pass feeds).  */

static bool
vec_unit_p (unsigned regno)
{
  if (regno < FIRST_PSEUDO_REGISTER)
    return SFPU_REG_P (regno);
  return regno < static_cast<unsigned> (max_reg_num ()) && regno_reg_rtx[regno]
    && GET_MODE (regno_reg_rtx[regno]) == XTT32SImode;
}

static unsigned
count_vec_units (bitmap live)
{
  unsigned count = 0;
  unsigned regno;
  bitmap_iterator iterator;
  EXECUTE_IF_SET_IN_BITMAP (live, 0, regno, iterator)
    count += vec_unit_p (regno);
  return count;
}

/* ------------------------- node admission -------------------------- */

static bool
issued_tensix_p (rtx_insn *insn)
{
  return GET_CODE (insn) == INSN
    && GET_CODE (PATTERN (insn)) != USE
    && GET_CODE (PATTERN (insn)) != CLOBBER
    && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX
    && get_attr_length (insn) > 0;
}

/* Audited result latency of INSN in issue slots; -1 refuses (opaque
   effects, no audited xtt_result_latency entry, or the architectural
   next-slot acceptance stall, which is outside the single-latency
   vocabulary -- the post-RA scheduler's audited_latency discipline).
   NB the rvtt.md `xtt_result_latency' ATTRIBUTE is encoded latency+1
   (rvtt_insn_effects subtracts one): attr "2" IS latency one (FH audit
   FHS-5 -- two prior audits misread this encoding).  */

static int
audited_latency_prera (rtx_insn *insn)
{
  if (!issued_tensix_p (insn))
    return -1;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return rvtt_timing::audited_latency (e.opaque, e.next_slot_stall,
				       e.result_latency);
}

struct pnode
{
  rtx_insn *insn;
  std::vector<unsigned> uses;	/* vec units read		      */
  std::vector<unsigned> defs;	/* vec units written		      */
  int lat;			/* audited result latency	      */
  int words;			/* issue slots occupied		      */
  int orig;			/* original index within the region   */
  long cp;			/* critical-path height		      */
  int entry_pin;		/* issue-slot floor from the entry    */
  bool pin_to_baseline;		/* unaudited entry-producer hazard    */
};

/* Vector-unit references of INSN from DF, sorted and deduplicated.
   Sets *SCALAR_DEF when INSN defines a non-vector register and
   *HARD_VEC when it references a hard LREG directly (pre-allocation
   both are region boundaries: scalar def-chains and precolored LREG
   webs are outside this pass's pseudo liveness model).  */

static void
vec_refs (rtx_insn *insn, std::vector<unsigned> *uses,
	  std::vector<unsigned> *defs, bool *scalar_def, bool *hard_vec)
{
  df_ref ref;
  FOR_EACH_INSN_USE (ref, insn)
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (vec_unit_p (regno))
	{
	  if (regno < FIRST_PSEUDO_REGISTER && hard_vec)
	    *hard_vec = true;
	  if (uses)
	    uses->push_back (regno);
	}
    }
  FOR_EACH_INSN_DEF (ref, insn)
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (vec_unit_p (regno))
	{
	  if (regno < FIRST_PSEUDO_REGISTER && hard_vec)
	    *hard_vec = true;
	  if (defs)
	    defs->push_back (regno);
	}
      else if (scalar_def)
	*scalar_def = true;
    }
  auto uniq = [] (std::vector<unsigned> *v)
  {
    if (!v)
      return;
    std::sort (v->begin (), v->end ());
    v->erase (std::unique (v->begin (), v->end ()), v->end ());
  };
  uniq (uses);
  uniq (defs);
}

/* Node admission; returns false with *WHY naming the barrier class.
   The vocabulary is the post-RA scheduler's, with the audited-latency
   gap named for this pass's contract
   (pressure-model-unaudited-producer).  */

static bool
prera_admissible_p (rtx_insn *insn, pnode *node, const char **why)
{
  if (!issued_tensix_p (insn))
    {
      *why = "non-tensix";
      return false;
    }
  if (JUMP_P (insn))
    {
      *why = "control-flow";
      return false;
    }
  if (get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
    {
      *why = "replay-owner";
      return false;
    }
  if (contains_mem_rtx_p (PATTERN (insn)))
    {
      *why = "memory-operand";
      return false;
    }
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (e.opaque)
    {
      *why = "effect-opaque";
      return false;
    }
  if (e.cc_write)
    {
      *why = "cc-write";
      return false;
    }
  if (e.config_dests_written || e.config_dests_read)
    {
      *why = "config-access";
      return false;
    }
  if (e.rwc.kind != xtt_rwc_effect_t::NONE)
    {
      *why = "rwc-step";
      return false;
    }
  if (e.dst_mem_read || e.dst_mem_write)
    {
      *why = "dst-access";
      return false;
    }
  node->lat = audited_latency_prera (insn);
  if (node->lat < 0)
    {
      *why = "pressure-model-unaudited-producer";
      return false;
    }
  if (node->lat > 1)
    {
      /* The distance model is exact only for the audited zero/one-slot
	 adjacency facts; a wider audit refuses by name until distance
	 semantics are their own audited fact family (the post-RA
	 scheduler's rule, unchanged).  */
      *why = "latency-beyond-audited-window";
      return false;
    }
  if (get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    {
      *why = "static-delay";
      return false;
    }
  node->words = get_attr_length (insn) / 4;
  if (node->words < 1)
    {
      *why = "zero-length";
      return false;
    }

  bool scalar_def = false, hard_vec = false;
  node->uses.clear ();
  node->defs.clear ();
  vec_refs (insn, &node->uses, &node->defs, &scalar_def, &hard_vec);
  if (hard_vec)
    {
      /* A precolored LREG web (raw-access sentinels land later, but a
	 direct hard reference can pre-exist): outside the pseudo
	 pressure model, fail closed.  */
      *why = "hard-lreg-reference";
      return false;
    }
  if (scalar_def)
    {
      /* A vector-to-scalar def chains into the scalar stream whose
	 members are region boundaries; keep the writer with them.  */
      *why = "scalar-def";
      return false;
    }
  if (node->defs.empty ())
    {
      *why = "scalar-or-defless";
      return false;
    }
  node->insn = insn;
  return true;
}

/* ------------------------- dependence DAG -------------------------- */

static bool
vec_intersect_p (const std::vector<unsigned> &a, const std::vector<unsigned> &b)
{
  auto ia = a.begin ();
  auto ib = b.begin ();
  while (ia != a.end () && ib != b.end ())
    {
      if (*ia < *ib)
	++ia;
      else if (*ib < *ia)
	++ib;
      else
	return true;
    }
  return false;
}

/* Dependence test: does the earlier node P order the later node C?
   Kind: 0 none, 1 latency-weighted (RAW or WAW), 2 issue-order (WAR).
   Complete for region members: admitted nodes define only vector
   pseudos, carry no memory, CC, configuration, RWC or Dst effects, and
   any scalar they read is defined outside the region (scalar defs are
   barriers), so vector def/use overlap is the entire ordering
   relation.  */

static int
pnode_dependence (const pnode &p, const pnode &c)
{
  return rvtt_timing::classify_dependence
    (vec_intersect_p (p.defs, c.uses) || vec_intersect_p (p.defs, c.defs),
     vec_intersect_p (p.uses, c.defs));
}

/* Marshal the region NODES into the timing engine's plain-data
   vocabulary (the pseudo-dependence twin of the post-RA marshaller).  */

static rvtt_timing::seq
pnode_timing_seq (const std::vector<pnode> &nodes)
{
  rvtt_timing::seq s;
  unsigned n = nodes.size ();
  s.ops.resize (n);
  s.dep.resize (n * n);
  for (unsigned i = 0; i != n; ++i)
    {
      s.ops[i].words = nodes[i].words;
      s.ops[i].lat = nodes[i].lat;
      s.ops[i].entry_pin = nodes[i].entry_pin;
      for (unsigned j = 0; j != n; ++j)
	s.dep[i * n + j]
	  = (unsigned char) pnode_dependence (nodes[i], nodes[j]);
    }
  return s;
}

/* --------------------- pressure model (region) --------------------- */

/* Region liveness context.  LIVE_BEFORE/LIVE_AFTER are the vector
   units live at region entry/exit (order-independent: the prefix and
   suffix of the block are unchanged by any candidate, and the first
   region reference kind of every pseudo is fixed by the dependence
   edges).  USE_COUNT counts member instructions reading each unit.  */

struct pressure_ctx
{
  std::vector<unsigned> live_before;
  std::vector<unsigned> live_after;
  std::vector<std::pair<unsigned, int>> use_count;
};

static int *
ctx_use_slot (pressure_ctx &ctx, unsigned regno)
{
  for (auto &entry : ctx.use_count)
    if (entry.first == regno)
      return &entry.second;
  return nullptr;
}

/* Modeled peak pressure of NODES issued in ORDER under CTX: the
   maximum simultaneous vector-unit liveness sampled at region entry
   and after every member.  Deaths are read before the result is born
   (a destructive result may legally reuse an operand whose final read
   is the same operation -- the rvtt_lp_schedule model, which the DS
   oracle validates).  */

static unsigned
pressure_of_order (const std::vector<pnode> &nodes,
		   const std::vector<int> &order, const pressure_ctx &ctx)
{
  std::vector<std::pair<unsigned, int>> remaining = ctx.use_count;
  std::vector<unsigned> live = ctx.live_before;
  auto live_p = [&live] (unsigned regno)
  {
    return std::find (live.begin (), live.end (), regno) != live.end ();
  };
  auto after_p = [&ctx] (unsigned regno)
  {
    return std::find (ctx.live_after.begin (), ctx.live_after.end (), regno)
      != ctx.live_after.end ();
  };
  unsigned peak = live.size ();
  for (int k : order)
    {
      const pnode &n = nodes[k];
      for (unsigned regno : n.uses)
	for (auto &entry : remaining)
	  if (entry.first == regno && --entry.second == 0
	      && !after_p (regno))
	    live.erase (std::remove (live.begin (), live.end (), regno),
			live.end ());
      for (unsigned regno : n.defs)
	{
	  bool needed = after_p (regno);
	  if (!needed)
	    for (const auto &entry : remaining)
	      if (entry.first == regno && entry.second > 0)
		needed = true;
	  if (needed && !live_p (regno))
	    live.push_back (regno);
	}
      peak = MAX (peak, (unsigned) live.size ());
    }
  return peak;
}

/* ---------------------- makespan model (region) -------------------- */

/* Modeled issue timeline of NODES in ORDER; fills *ISSUE and returns
   the modeled end including exit shadows.  The post-RA scheduler's
   ls_simulate over pseudo dependences.  */

static int
simulate_order (const std::vector<pnode> &nodes, const std::vector<int> &order,
		std::vector<int> *issue, const std::vector<bool> &exit_shadow)
{
  /* The dependence matrix is register-set-based over the marshalled
     pseudo references: the engine's P is whichever node the simulated
     order issues earlier, whatever its original index (the post-RA
     ls_simulate discipline, now the ONE engine's).  */
  return rvtt_timing::simulate (pnode_timing_seq (nodes), order, issue,
				exit_shadow);
}

/* ------------------------- schedulers ------------------------------ */

/* Critical-path heights over the issue-distance weights.  */

static void
compute_cp (std::vector<pnode> &nodes)
{
  unsigned n = nodes.size ();
  for (unsigned i = n; i--;)
    {
      long cp = nodes[i].words + nodes[i].lat;
      for (unsigned j = i + 1; j != n; ++j)
	{
	  int kind = pnode_dependence (nodes[i], nodes[j]);
	  if (!kind)
	    continue;
	  long via = nodes[j].cp
	    + nodes[i].words + (kind == 1 ? nodes[i].lat : 0);
	  if (via > cp)
	    cp = via;
	}
      nodes[i].cp = cp;
    }
}

/* The MODEL SCHEDULE: greedy minimum-candidate-peak dispatch, "chosen
   solely to keep register pressure down"; ties broken toward lower
   resulting liveness, then original order.  Returns the order and sets
   *MODEL_PEAK.  */

static std::vector<int>
model_schedule (const std::vector<pnode> &nodes, const pressure_ctx &ctx,
		unsigned *model_peak)
{
  unsigned n = nodes.size ();
  std::vector<std::pair<unsigned, int>> remaining = ctx.use_count;
  std::vector<unsigned> live = ctx.live_before;
  std::vector<bool> issued (n, false);
  std::vector<int> order;
  order.reserve (n);
  unsigned peak = live.size ();

  auto after_p = [&ctx] (unsigned regno)
  {
    return std::find (ctx.live_after.begin (), ctx.live_after.end (), regno)
      != ctx.live_after.end ();
  };

  while (order.size () != n)
    {
      int best = -1;
      unsigned best_peak = ~0u;
      unsigned best_live = ~0u;
      for (unsigned i = 0; i != n; ++i)
	{
	  if (issued[i])
	    continue;
	  /* Readiness: every earlier-original dependence issued.  (Any
	     register-sharing pair is mutually dependent under the
	     set-based test, so readiness by original index keeps every
	     sharing pair in original relative order -- only provably
	     independent instructions commute.)  */
	  bool deps_done = true;
	  for (unsigned j = 0; j != i && deps_done; ++j)
	    if (!issued[j] && pnode_dependence (nodes[j], nodes[i]))
	      deps_done = false;
	  if (!deps_done)
	    continue;

	  /* Candidate liveness delta.  */
	  int deaths = 0;
	  for (unsigned regno : nodes[i].uses)
	    for (const auto &entry : remaining)
	      if (entry.first == regno && entry.second == 1
		  && !after_p (regno))
		++deaths;
	  int births = 0;
	  for (unsigned regno : nodes[i].defs)
	    {
	      bool needed = after_p (regno);
	      if (!needed)
		for (const auto &entry : remaining)
		  if (entry.first == regno && entry.second > 0)
		    needed = true;
	      bool already
		= std::find (live.begin (), live.end (), regno) != live.end ();
	      if (needed && !already)
		++births;
	    }
	  unsigned candidate_live = live.size () - deaths + births;
	  unsigned candidate_peak = MAX (candidate_live, peak);
	  if (best < 0 || candidate_peak < best_peak
	      || (candidate_peak == best_peak && candidate_live < best_live))
	    {
	      best = i;
	      best_peak = candidate_peak;
	      best_live = candidate_live;
	    }
	}
      gcc_assert (best >= 0);
      issued[best] = true;
      order.push_back (best);
      for (unsigned regno : nodes[best].uses)
	for (auto &entry : remaining)
	  if (entry.first == regno && --entry.second == 0 && !after_p (regno))
	    live.erase (std::remove (live.begin (), live.end (), regno),
			live.end ());
      for (unsigned regno : nodes[best].defs)
	{
	  bool needed = after_p (regno);
	  if (!needed)
	    for (const auto &entry : remaining)
	      if (entry.first == regno && entry.second > 0)
		needed = true;
	  if (needed
	      && std::find (live.begin (), live.end (), regno) == live.end ())
	    live.push_back (regno);
	}
      peak = MAX (peak, (unsigned) live.size ());
    }
  *model_peak = peak;
  return order;
}

/* The ECC list schedule: ready nodes ranked by ECC + delay (then
   delay, then critical-path height, then original order).  ECC is the
   number of registers the issue would push beyond
   LIMIT = MAX (model peak, capacity), negative when a
   pressure-reducing issue pulls the count back under an exceeded
   limit; pressure at or below the limit is free (the model schedule
   proved the region needs that much).  */

static std::vector<int>
ecc_schedule (const std::vector<pnode> &nodes, const pressure_ctx &ctx,
	      unsigned limit)
{
  unsigned n = nodes.size ();
  std::vector<std::pair<unsigned, int>> remaining = ctx.use_count;
  std::vector<unsigned> live = ctx.live_before;
  std::vector<bool> issued (n, false);
  std::vector<int> ready_at (n, 0);
  std::vector<int> order;
  order.reserve (n);
  for (unsigned i = 0; i != n; ++i)
    ready_at[i] = nodes[i].entry_pin;

  auto after_p = [&ctx] (unsigned regno)
  {
    return std::find (ctx.live_after.begin (), ctx.live_after.end (), regno)
      != ctx.live_after.end ();
  };

  int t = 0;
  while (order.size () != n)
    {
      int best = -1;
      long best_rank = 0;
      int best_delay = 0;
      for (unsigned i = 0; i != n; ++i)
	{
	  if (issued[i])
	    continue;
	  /* Readiness by original index, as in the model schedule.  */
	  bool deps_done = true;
	  for (unsigned j = 0; j != i && deps_done; ++j)
	    if (!issued[j] && pnode_dependence (nodes[j], nodes[i]))
	      deps_done = false;
	  if (!deps_done)
	    continue;

	  int delay = MAX (ready_at[i] - t, 0);

	  /* Liveness delta of issuing I now.  */
	  int deaths = 0;
	  for (unsigned regno : nodes[i].uses)
	    for (const auto &entry : remaining)
	      if (entry.first == regno && entry.second == 1
		  && !after_p (regno))
		++deaths;
	  int births = 0;
	  for (unsigned regno : nodes[i].defs)
	    {
	      bool needed = after_p (regno);
	      if (!needed)
		for (const auto &entry : remaining)
		  if (entry.first == regno && entry.second > 0)
		    needed = true;
	      bool already
		= std::find (live.begin (), live.end (), regno) != live.end ();
	      if (needed && !already)
		++births;
	    }
	  int now = live.size ();
	  int then = now - deaths + births;
	  int ecc = MAX (then - (int) limit, 0) - MAX (now - (int) limit, 0);

	  long rank = (long) ecc + delay;
	  if (best < 0 || rank < best_rank
	      || (rank == best_rank
		  && (delay < best_delay
		      || (delay == best_delay
			  && (nodes[i].cp > nodes[best].cp
			      || (nodes[i].cp == nodes[best].cp
				  && nodes[i].orig < nodes[best].orig))))))
	    {
	      best = i;
	      best_rank = rank;
	      best_delay = delay;
	    }
	}
      gcc_assert (best >= 0);
      if (ready_at[best] > t)
	t = ready_at[best];		/* the chosen deliberate stall */
      issued[best] = true;
      order.push_back (best);
      int done = t + nodes[best].words;
      for (unsigned j = 0; j != n; ++j)
	{
	  if (issued[j] || j == (unsigned) best)
	    continue;
	  /* Unissued dependent successors are all later-original (an
	     unissued earlier-original dependence would have blocked
	     BEST's readiness), so BEST is the producer.  */
	  int kind = pnode_dependence (nodes[best], nodes[j]);
	  if (!kind)
	    continue;
	  int need = done + (kind == 1 ? nodes[best].lat : 0);
	  if (need > ready_at[j])
	    ready_at[j] = need;
	}
      t = done;

      for (unsigned regno : nodes[best].uses)
	for (auto &entry : remaining)
	  if (entry.first == regno && --entry.second == 0 && !after_p (regno))
	    live.erase (std::remove (live.begin (), live.end (), regno),
			live.end ());
      for (unsigned regno : nodes[best].defs)
	{
	  bool needed = after_p (regno);
	  if (!needed)
	    for (const auto &entry : remaining)
	      if (entry.first == regno && entry.second > 0)
		needed = true;
	  if (needed
	      && std::find (live.begin (), live.end (), regno) == live.end ())
	    live.push_back (regno);
	}
    }
  return order;
}

/* --------------------- DF pressure verification -------------------- */

/* Independent per-point pressure of the CURRENT stream over the region
   whose members are NODES (in whatever order they now sit in BB),
   recomputed from the DF live-register problem by backward simulation
   from DF_LR_OUT.  This is the pass's oracle hookup: the same liveness
   family the allocator audits and the DS pressure oracle
   machine-checks.  Returns the peak over the samples at region entry,
   between members, and at region exit.  */

static unsigned
df_region_peak (basic_block bb, const std::vector<pnode> &nodes)
{
  std::vector<rtx_insn *> members;
  members.reserve (nodes.size ());
  for (const pnode &n : nodes)
    members.push_back (n.insn);
  auto member_p = [&members] (rtx_insn *insn)
  {
    return std::find (members.begin (), members.end (), insn)
      != members.end ();
  };

  /* Stream-order first and last member.  */
  rtx_insn *first = nullptr, *last = nullptr;
  for (rtx_insn *insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    if (INSN_P (insn) && member_p (insn))
      {
	if (!first)
	  first = insn;
	last = insn;
      }
  gcc_assert (first && last);

  auto_bitmap live;
  bitmap_copy (live, DF_LR_OUT (bb));
  df_simulate_initialize_backwards (bb, live);

  unsigned peak = 0;
  bool sampling = false;
  bool reached_first = false;
  for (rtx_insn *insn = BB_END (bb);
       insn && insn != PREV_INSN (BB_HEAD (bb)); insn = PREV_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (insn == last)
	{
	  sampling = true;
	  peak = MAX (peak, count_vec_units (live)); /* region exit */
	}
      df_simulate_one_insn_backwards (bb, insn, live);
      if (sampling && member_p (insn))
	peak = MAX (peak, count_vec_units (live)); /* before INSN */
      if (insn == first)
	{
	  reached_first = true;
	  break;
	}
    }
  gcc_assert (reached_first);
  return peak;
}

/* ------------------------- region driver --------------------------- */

/* The entry producer of a region starting at FIRST: the nearest
   preceding issued Tensix instruction, skipping scalars, USE/CLOBBER
   markers, and zero-length ghosts (no word, no event); a jump or call
   boundary yields none.  The post-RA scheduler's walk, unchanged.  */

static rtx_insn *
prera_entry_producer (basic_block bb, rtx_insn *first)
{
  for (rtx_insn *w = PREV_INSN (first); w && w != PREV_INSN (BB_HEAD (bb));
       w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (GET_CODE (w) != INSN)
	return nullptr;
      rtx pat = PATTERN (w);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (recog_memoized (w) < 0)
	return nullptr;
      if (get_attr_type (w) != TYPE_TENSIX)
	continue;
      if (!get_attr_length (w))
	continue;
      return w;
    }
  return nullptr;
}

static rtx_insn *
prera_exit_consumer (basic_block bb, rtx_insn *after)
{
  for (rtx_insn *w = NEXT_INSN (after);
       w && w != NEXT_INSN (BB_END (bb)); w = NEXT_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (GET_CODE (w) != INSN)
	return nullptr;
      rtx pat = PATTERN (w);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (recog_memoized (w) < 0)
	return nullptr;
      if (get_attr_type (w) != TYPE_TENSIX)
	continue;
      if (!get_attr_length (w))
	continue;
      return w;
    }
  return nullptr;
}

/* Build the region's liveness context from DF, and record the DF
   per-point baseline peak.  LIVE_AFTER is the set at region exit,
   LIVE_BEFORE the set at region entry, both restricted to vector
   units; USE_COUNT counts member reads per unit.  */

static void
build_pressure_ctx (basic_block bb, const std::vector<pnode> &nodes,
		    pressure_ctx *ctx, unsigned *df_base_peak)
{
  auto_bitmap live;
  bitmap_copy (live, DF_LR_OUT (bb));
  df_simulate_initialize_backwards (bb, live);

  rtx_insn *first = nodes.front ().insn;
  rtx_insn *last = nodes.back ().insn;
  auto member_p = [&nodes] (rtx_insn *insn)
  {
    for (const pnode &n : nodes)
      if (n.insn == insn)
	return true;
    return false;
  };

  auto snapshot = [] (bitmap b, std::vector<unsigned> *out)
  {
    out->clear ();
    unsigned regno;
    bitmap_iterator iterator;
    EXECUTE_IF_SET_IN_BITMAP (b, 0, regno, iterator)
      if (vec_unit_p (regno))
	out->push_back (regno);
    std::sort (out->begin (), out->end ());
  };

  unsigned peak = 0;
  bool sampling = false;
  bool reached_first = false;
  for (rtx_insn *insn = BB_END (bb);
       insn && insn != PREV_INSN (BB_HEAD (bb)); insn = PREV_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (insn == last)
	{
	  sampling = true;
	  snapshot (live, &ctx->live_after);
	  peak = MAX (peak, count_vec_units (live));
	}
      df_simulate_one_insn_backwards (bb, insn, live);
      if (sampling && member_p (insn))
	peak = MAX (peak, count_vec_units (live));
      if (insn == first)
	{
	  reached_first = true;
	  break;
	}
    }
  gcc_assert (reached_first);
  snapshot (live, &ctx->live_before);
  *df_base_peak = peak;

  ctx->use_count.clear ();
  for (const pnode &n : nodes)
    for (unsigned regno : n.uses)
      {
	int *slot = ctx_use_slot (*ctx, regno);
	if (slot)
	  ++*slot;
	else
	  ctx->use_count.emplace_back (regno, 1);
      }
}

/* Schedule one region transactionally.  Returns true when a candidate
   was committed.  */

static bool
prera_schedule_region (basic_block bb, std::vector<pnode> &nodes,
		       rtx_insn *anchor, rtx_insn *entry_producer,
		       rtx_insn *exit_consumer,
		       const std::vector<unsigned> &unaudited_defs)
{
  unsigned n = nodes.size ();

  /* Entry boundary: latency floor from an audited entry producer;
     baseline pinning against an unaudited one (through its defs).  */
  std::vector<unsigned> ep_defs;
  int ep_lat = 0;
  if (entry_producer)
    {
      vec_refs (entry_producer, nullptr, &ep_defs, nullptr, nullptr);
      ep_lat = audited_latency_prera (entry_producer);
      if (ep_lat < 0 || ep_lat > 1)
	ep_lat = 0;
    }
  for (unsigned i = 0; i != n; ++i)
    {
      nodes[i].entry_pin = 0;
      nodes[i].pin_to_baseline
	= vec_intersect_p (unaudited_defs, nodes[i].uses)
	  || vec_intersect_p (unaudited_defs, nodes[i].defs);
      if (entry_producer
	  && (vec_intersect_p (ep_defs, nodes[i].uses)
	      || vec_intersect_p (ep_defs, nodes[i].defs))
	  && ep_lat > nodes[i].entry_pin)
	nodes[i].entry_pin = ep_lat;
    }

  /* Exit boundary: nodes feeding the next issued Tensix instruction
     keep their trailing shadow; a block-ending region drains all.  */
  std::vector<bool> exit_shadow (n, false);
  if (exit_consumer)
    {
      std::vector<unsigned> xc_uses, xc_defs;
      vec_refs (exit_consumer, &xc_uses, &xc_defs, nullptr, nullptr);
      std::vector<unsigned> wanted = xc_uses;
      wanted.insert (wanted.end (), xc_defs.begin (), xc_defs.end ());
      std::sort (wanted.begin (), wanted.end ());
      wanted.erase (std::unique (wanted.begin (), wanted.end ()),
		    wanted.end ());
      for (unsigned i = 0; i != n; ++i)
	exit_shadow[i] = vec_intersect_p (nodes[i].defs, wanted);
    }
  else
    for (unsigned i = 0; i != n; ++i)
      exit_shadow[i] = true;

  /* Liveness context + the independent DF baseline.  */
  pressure_ctx ctx;
  unsigned df_base_peak = 0;
  build_pressure_ctx (bb, nodes, &ctx, &df_base_peak);

  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  unsigned base_peak = pressure_of_order (nodes, base_order, ctx);

  if (base_peak != df_base_peak)
    {
      rvtt_refuse (RVTT_REF_PRESSURE_ORACLE_DISAGREEMENT, dump_file,
		   "Prera-pressure-schedule refused: "
		   "pressure-oracle-disagreement (baseline model=%u df=%u) "
		   "in bb %d region at uid=%d\n",
		   base_peak, df_base_peak, bb->index,
		   INSN_UID (nodes[0].insn));
      return false;
    }

  std::vector<int> base_issue (n, 0);
  int base_end = simulate_order (nodes, base_order, &base_issue, exit_shadow);
  for (unsigned i = 0; i != n; ++i)
    if (nodes[i].pin_to_baseline)
      nodes[i].entry_pin = base_issue[i];

  if (dump_file)
    fprintf (dump_file, "Prera-pressure-schedule region: bb=%d nodes=%u "
	     "live-in=%zu base-peak=%u base-makespan=%d\n",
	     bb->index, n, ctx.live_before.size (), base_peak, base_end);

  compute_cp (nodes);

  /* 1. Model schedule (pressure only): records MP; also the fallback
     candidate.  2. ECC candidate against limit MAX (MP, capacity).  */
  unsigned model_peak = 0;
  std::vector<int> model_order = model_schedule (nodes, ctx, &model_peak);
  unsigned limit = MAX (model_peak, (unsigned) SFPU_REG_NUM);
  std::vector<int> ecc_order = ecc_schedule (nodes, ctx, limit);

  struct candidate
  {
    const char *name;
    std::vector<int> *order;
    unsigned peak;
    int end;
    bool moves;
    bool accepted;
  };
  candidate cands[2] = {
    { "ecc", &ecc_order, 0, 0, false, false },
    { "model", &model_order, 0, 0, false, false },
  };
  for (candidate &c : cands)
    {
      c.peak = pressure_of_order (nodes, *c.order, ctx);
      std::vector<int> issue (n, 0);
      c.end = simulate_order (nodes, *c.order, &issue, exit_shadow);
      c.moves = *c.order != base_order;
      c.accepted = c.moves && c.peak <= base_peak && c.end <= base_end
	&& (c.peak < base_peak || c.end < base_end);
    }

  if (dump_file)
    fprintf (dump_file, "Prera-pressure-schedule candidates: ecc peak=%u "
	     "end=%d moves=%d; model peak=%u end=%d moves=%d; limit=%u\n",
	     cands[0].peak, cands[0].end, cands[0].moves,
	     cands[1].peak, cands[1].end, cands[1].moves, limit);

  candidate *chosen = nullptr;
  for (candidate &c : cands)
    if (c.accepted
	&& (!chosen || c.peak < chosen->peak
	    || (c.peak == chosen->peak && c.end < chosen->end)))
      chosen = &c;

  if (!chosen)
    {
      if (dump_file)
	fprintf (dump_file, "Prera-pressure-schedule refused: no joint "
		 "pressure/makespan improvement in bb %d region at uid=%d "
		 "(pressure %u -> ecc %u / model %u; "
		 "makespan %d -> ecc %d / model %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_peak,
		 cands[0].peak, cands[1].peak, base_end,
		 cands[0].end, cands[1].end);
      return false;
    }

  /* Exact-restore record: the chain from ANCHOR to the region's last
     member, debug insns included.  */
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == nodes[n - 1].insn)
	break;
    }

  /* Commit: relink the region in schedule order after ANCHOR.  */
  rtx_insn *after = anchor;
  for (unsigned k = 0; k != n; ++k)
    {
      rtx_insn *insn = nodes[(*chosen->order)[k]].insn;
      if (PREV_INSN (insn) != after)
	reorder_insns (insn, insn, after);
      after = insn;
    }

  /* Post-commit oracle re-verification: the DF recount of the NEW
     stream must reproduce the candidate's claimed peak.  */
  unsigned df_new_peak = df_region_peak (bb, nodes);
  if (df_new_peak != chosen->peak)
    {
      after = anchor;
      for (rtx_insn *insn : chain)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      rvtt_refuse (RVTT_REF_PRESSURE_ORACLE_DISAGREEMENT, dump_file,
		   "Prera-pressure-schedule refused: "
		   "pressure-oracle-disagreement (post-commit model=%u df=%u), "
		   "restored bb %d region at uid=%d\n",
		   chosen->peak, df_new_peak, bb->index,
		   INSN_UID (nodes[0].insn));
      return false;
    }

  if (dump_file)
    {
      fprintf (dump_file, "Prera-pressure-schedule: bb %d nodes=%u "
	       "pressure %u -> %u makespan %d -> %d model-peak=%u "
	       "candidate=%s target=%s\n",
	       bb->index, n, base_peak, chosen->peak, base_end, chosen->end,
	       model_peak, chosen->name,
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "Prera-pressure-schedule order=%u uid=%d\n",
		 k, INSN_UID (nodes[(*chosen->order)[k]].insn));
    }
  return true;
}

/* One collected candidate region of a block.  */

struct prera_region
{
  std::vector<pnode> nodes;
  rtx_insn *anchor;
  rtx_insn *entry_producer;
  std::vector<unsigned> unaudited_defs;
  std::vector<int> signature;	/* insn codes, for the repeat deferral */
};

static void
prera_schedule_function (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file, "Prera-pressure-schedule refused: no audited "
		 "latency facts for this target\n");
      return;
    }

  df_analyze ();

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      /* A self-loop row executes back-to-back across the backedge: the
	 row is a cycle, and both this pass's boundary makespan model
	 and its region liveness sampling assume a linear seam.  The
	 cyclic adjacency is capture rotation's audited territory.  */
      bool self_loop = false;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == bb)
	  self_loop = true;
      if (self_loop)
	{
	  if (dump_file)
	    fprintf (dump_file, "Prera-pressure-schedule deferred: cyclic "
		     "row adjacency in bb %d (capture rotation owns the "
		     "backedge seam)\n", bb->index);
	  continue;
	}

      /* Phase 1: collect the block's candidate regions.  */
      std::vector<prera_region> regions;
      std::vector<pnode> nodes;
      rtx_insn *anchor = nullptr;
      rtx_insn *entry_producer = nullptr;
      std::vector<unsigned> region_unaudited;
      bool stop_block = false;

      auto flush = [&] ()
      {
	if (nodes.size () == 2 && dump_file)
	  rvtt_refuse (RVTT_REF_TWO_NODE, dump_file,
		       "Prera-pressure-schedule skipped: two-node "
		       "region at uid=%d in bb %d (below the interleave "
		       "minimum)\n", INSN_UID (nodes[0].insn), bb->index);
	if (nodes.size () >= 3)
	  {
	    prera_region r;
	    r.nodes = std::move (nodes);
	    r.anchor = anchor;
	    r.entry_producer = entry_producer;
	    r.unaudited_defs = region_unaudited;
	    for (const pnode &nd : r.nodes)
	      r.signature.push_back (INSN_CODE (nd.insn));
	    regions.push_back (std::move (r));
	  }
	nodes.clear ();
      };

      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (stop_block)
	    break;
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  pnode node;
	  const char *why = nullptr;
	  if (prera_admissible_p (insn, &node, &why))
	    {
	      if (nodes.empty ())
		{
		  anchor = PREV_INSN (insn);
		  entry_producer = prera_entry_producer (bb, insn);
		  region_unaudited.clear ();
		  if (entry_producer)
		    {
		      int ep_lat = audited_latency_prera (entry_producer);
		      if (ep_lat < 0 || ep_lat > 1)
			vec_refs (entry_producer, nullptr, &region_unaudited,
				  nullptr, nullptr);
		    }
		}
	      node.orig = (int) nodes.size ();
	      nodes.push_back (std::move (node));
	      continue;
	    }

	  /* Barrier.  */
	  if (dump_file && GET_CODE (insn) == INSN
	      && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_length (insn))
	    fprintf (dump_file, "Prera-pressure-schedule barrier: %s "
		     "uid=%d\n", why, INSN_UID (insn));
	  flush ();
	  if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    stop_block = true;	/* established capture discipline */
	}
      if (!stop_block)
	flush ();

      /* Phase 2: repeated region shapes defer by name -- unrolled row
	 copies must stay textually isomorphic for the post-allocation
	 replay and MOP re-rolls that consume this stream's
	 descendant.  */
      for (unsigned i = 0; i != regions.size (); ++i)
	{
	  bool repeated = false;
	  for (unsigned j = 0; j != regions.size (); ++j)
	    if (j != i && regions[j].signature == regions[i].signature)
	      repeated = true;
	  if (repeated)
	    {
	      rvtt_refuse (RVTT_REF_REPEATED_ROW, dump_file,
			   "Prera-pressure-schedule deferred: "
			   "repeated-row shape at uid=%d in bb %d (replay "
			   "capture formation owns row isomorphism)\n",
			   INSN_UID (regions[i].nodes[0].insn), bb->index);
	      continue;
	    }
	  prera_schedule_region (bb, regions[i].nodes, regions[i].anchor,
				 regions[i].entry_producer,
				 prera_exit_consumer
				   (bb, regions[i].nodes.back ().insn),
				 regions[i].unaudited_defs);
	}
    }
}

const pass_data pass_data_rvtt_lp_schedule_prera =
{
  RTL_PASS, /* type */
  "rvtt_lp_schedule_prera", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_lp_schedule_prera : public rtl_opt_pass
{
public:
  pass_rvtt_lp_schedule_prera (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lp_schedule_prera, ctxt)
  {}

  bool gate (function *) final override
  {
    return optimize > 0 && TARGET_XTT_TENSIX
      && riscv_tt_opt_pressure_schedule_prera;
  }

  unsigned execute (function *fn) final override
  {
    prera_schedule_function (fn);
    return 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_lp_schedule_prera (gcc::context *ctxt)
{
  return new pass_rvtt_lp_schedule_prera (ctxt);
}
