/* Tensix scheduling: region list scheduling
   Copyright (C) 2022-2026 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).
   Rewritten Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

/* The region list-scheduling unit of the Tensix scheduler
   (-mtt-tensix-optimize-list-schedule, -round-interleave,
   -cyclic-region-schedule, -ims): straight-line region collection
   and critical-path list scheduling, the isomorphic-pair and
   cyclic-interior extensions, the region-scoped storage-collision
   renamer, and the Rau IMS candidate order.  Split from
   rtl-rvtt-schedule.cc; the algorithm essay lives there.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "cfgrtl.h"
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "insn-constants.h"
#include "recog.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-macro-tables.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rtl-rvtt-sched-int.h"

/* ---- DAG list scheduling over audited straight-line SFPU regions ----

   The three phases above are single-move bubble fillers: each closes one
   exposed slot with one independent instruction.  They cannot express the
   general latency objective -- interleaving two independent dependence
   chains so every audited result-latency shadow is filled (the documented
   dual-Horner P/Q case: a serial P-chain-then-Q-chain stream carries one
   modeled stall per dependent adjacency; the interleaved stream carries
   none).  This phase is the real scheduler: it builds the dependence DAG
   of a maximal audited straight-line region and list-schedules it against
   the modeled issue timeline, committing the new order only on a strict
   modeled-makespan decrease.

   Region admission, refusing by name (fail-closed; every barrier bounds
   the region and keeps its own position, and nothing crosses it):
   - a node is an issued Tensix instruction that is pure-LREG (every
     register reference an allocatable SFPU register, no memory), either
     the bare unpredicated copy or on record in the typed effect table
     with no CC write, no configuration access, no RWC step, and no Dst
     traffic.  This is shadow_filler_p's effect vocabulary made STRICTER:
     no XTT_LATENCY_REORDER_SAFE acceptance (an audited-attribute class
     without a typed effect record never schedules here);
   - a node's result latency must be AUDITED (audited_latency >= 0, which
     also refuses the architectural next-slot acceptance stall) AND
     within the proven adjacency window (latency <= 1): the distance
     model below is exact only for the audited zero/one-slot facts, so a
     wider audit landing in the table refuses by name here
     ("latency-beyond-audited-window") until distance semantics are
     audited as their own fact family -- the same discipline as
     fill_interlock_shadows' by-name latency>1 refusal;
   - CC-writing instructions (setcc/encc and every other lane-state
     mutator) are named barriers: a region therefore executes under ONE
     CC state, so lane-predicated members read the same lane enables at
     any position inside it.  A lane-predicated write is a
     read-modify-write, but no explicit defs-join-uses conservatism is
     needed for the EDGE SET: the implicit read targets only the node's
     own destinations, and every ordering that read could demand is
     already carried latency-weighted by the WAW edge on the same
     register (earlier writer) or the WAR edge from the earlier reader's
     use set (later writer) -- the WAW/WAR edges subsume the RMW read;
   - a STATIC-delay contract is a named barrier (its pad precedes any
     non-nop instruction: reordering cannot close it);
   - an explicit replay-buffer owner ends eligibility for the REST of
     the block (a fixed capture records the following delivered words by
     position), matching the established phase discipline.  Formed
     macro-planner emissions are effect-opaque and therefore barriers.

   FORMATION INTERACTION (this pass runs BEFORE replay formation,
   dst-autoincr, and MOP formation, which consume the scheduled stream):
   - a SELF-LOOP block defers entirely, by name: a counted row executes
     back-to-back across the backedge (and, captured, across every
     playback), so the row is a CYCLE -- this scheduler's linear
     boundary model mispredicts the seam adjacency, which is capture
     rotation's audited territory;
   - REPEATED region shapes inside one block defer, by name: unrolled
     row copies must remain textually isomorphic for the replay
     former's re-roll and the MOP re-roll to recognize them, and
     boundary-context differences (a first copy's entry producer, a
     last copy's exit consumer) would otherwise schedule sibling copies
     differently.  Two regions with the same insn-code signature in one
     block therefore both refuse ("repeated-row shape deferred to
     replay capture formation").  Named residual: RUNTIME-unrolled
     copies living in separate blocks (branches between copies) evade
     both deferrals -- exact/counted unrolls land in one block and are
     covered; the corpus formation gate owns the residual.
   - Both deferrals are LIFTED, for exactly the shapes whose proofs
     hold, by -mtt-tensix-optimize-round-interleave (default off): a
     one-region self-loop row schedules under the wrapped steady-state
     II model, and an exactly-two isomorphic-copy family schedules as a
     pair under one shared permutation; every other shape keeps its
     deferral by name.  See the round-chain interleave section below.

   Dependence DAG, over DF hard-register references (complete for
   allocatable registers after allocation, as established above):
   - RAW and WAW edges require issue distance >= words(producer) +
     audited latency(producer); WAR edges require >= words(producer).

   Objective: modeled makespan of the region -- issue slots (multi-word
   instructions occupy their word count) plus modeled interlock stalls
   (the audited xtt_result_latency facts; on WH the same count appears
   as required SFPNOP words, on BH as transparent scoreboard stalls),
   plus the boundary terms:
   - the immediately preceding issued Tensix instruction (the entry
     producer) contributes its audited latency to nodes that touch its
     destinations; with every admitted latency <= 1, one issued
     instruction is the complete audited entry horizon (a producer two
     issue slots back has an expired shadow), and an entry producer
     carrying a latency above the window refuses into the pin
     discipline below;
   - an entry producer whose latency is UNAUDITED (or beyond the
     window) pins every region node that touches its destinations to
     its baseline issue slot or later ("entry-pinned"): the node's
     distance to the unknown-latency producer never decreases, so the
     unmodeled stall can only shrink.  Unknown-latency producers deeper
     than the entry adjacency are unmodeled in baseline and candidate
     alike -- the exposure class the fill phases already carry when a
     filler moves toward them.  Since the baseline-first node is always
     pinned to slot zero, a pinned node can occupy the region's first
     stream position only if it already held it, which is also what
     keeps the entry producer's DYNAMIC pad state from flipping (the
     commit guard below re-verifies it anyway);
   - the nearest following issued Tensix instruction (the exit consumer)
     adds the trailing shadow of the nodes that feed it; a region ending
     the block drains every node's shadow (conservative, applied to
     baseline and candidate identically).

   List scheduling itself is deterministic: ready nodes issue by
   greatest critical-path height (edge weights = the issue-distance
   requirements above), ties broken by original order.

   REGISTER PRESSURE: this pass runs post-allocation, where the eight
   allocatable hard LREGs themselves are the pressure bound -- register
   reuse appears as WAR/WAW edges that serialize the schedule, so no
   order this scheduler can emit needs a ninth name.  A pressure-aware
   dispatch over virtual registers is the pre-allocation scheduler's
   contract (the allocator lane), not this pass's; claiming one here
   would be a gate that cannot fire.

   The commit is transactional: the candidate order is adopted only on a
   strict modeled-makespan decrease, then re-verified against the nop
   inserter's own probe -- the count of DYNAMIC-delay pad sites over
   the region members must not grow, and the ENTRY producer's pad state
   must not flip on (the vacated-seam discipline of the fill phases);
   any failure restores the original chain exactly, debug insns
   included.  On a committed reorder debug insns keep their original
   chain links (codegen-identical; var-location bindings may drift, as
   under any scheduler).  Purely structural: no operation identity,
   opcode calendar, coefficient value, or instruction-word fingerprint
   participates.  */

/* Node admission; returns false with *WHY naming the barrier class.  */

static bool
ls_admissible_p (rtx_insn *insn, ls_node *node, const char **why)
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
  if (!bare_lreg_copy_p (insn))
    {
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
      /* No defs-join-uses conservatism for lane-predicated writes: the
	 RMW's implicit read targets only the node's own destinations,
	 and the latency-weighted WAW edge (against an earlier writer)
	 or the WAR edge from the earlier reader's use set (against a
	 later writer) already carries every ordering it could demand
	 (see the head comment).  */
    }
  node->lat = audited_latency (insn);
  if (node->lat < 0)
    {
      *why = "unaudited-latency";
      return false;
    }
  if (node->lat > 1)
    {
      /* The distance model is exact only for the audited zero/one-slot
	 adjacency facts (fill_interlock_shadows' discipline): a wider
	 audit refuses by name until distance semantics are their own
	 audited fact family.  */
      *why = "latency-beyond-audited-window";
      return false;
    }
  if (get_attr_xtt_delay (insn) == XTT_DELAY_STATIC)
    {
      *why = "static-delay";
      return false;
    }
  if (!collect_sfpu_regs (insn, &node->regs))
    {
      *why = "scalar-or-defless";
      return false;
    }
  node->raw_defs = node->regs.defs;
  node->insn = insn;
  node->words = get_attr_length (insn) / 4;
  if (node->words < 1)
    {
      *why = "zero-length";
      return false;
    }
  return true;
}

/* Dependence test: does the earlier node P order the later node C?
   Kind: 0 none, 1 latency-weighted (RAW or WAW), 2 issue-order (WAR).  */

int
ls_dependence (const ls_node &p, const ls_node &c)
{
  return rvtt_timing::classify_dependence
    (hard_reg_set_intersect_p (p.raw_defs, c.regs.uses)
     || hard_reg_set_intersect_p (p.raw_defs, c.raw_defs),
     hard_reg_set_intersect_p (p.regs.uses, c.raw_defs));
}

/* Marshal the region NODES into the timing engine's plain-data
   vocabulary: per-node {words, lat, entry_pin} plus the full
   dependence matrix (both directions; the diagonal carries the
   cross-copy self-dependence the cyclic model consumes).  */

static rvtt_timing::seq
ls_timing_seq (const std::vector<ls_node> &nodes)
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
	  = (unsigned char) ls_dependence (nodes[i], nodes[j]);
    }
  return s;
}

/* Modeled issue timeline of NODES in the order given by ORDER (indices
   into NODES).  Fills issue slots into *ISSUE (indexed like NODES) and
   returns the modeled end: the last occupied slot boundary plus the
   trailing shadow of every node in EXIT_MASK (nodes feeding the exit
   consumer, or all nodes when the region ends the block).  Entry pins
   and the entry producer's latency floor are honored.  */

static int
ls_simulate (const std::vector<ls_node> &nodes,
	     const std::vector<int> &order, std::vector<int> *issue,
	     const std::vector<bool> &exit_shadow)
{
  return rvtt_timing::simulate (ls_timing_seq (nodes), order, issue,
				exit_shadow);
}

/* Count the DYNAMIC-delay pad sites over the region members (the nop
   inserter's own probe): the commit guard's before/after metric.  */

unsigned
ls_pad_sites (std::vector<basic_block> &visited, basic_block bb,
	      const std::vector<ls_node> &nodes)
{
  unsigned pads = 0;
  for (const ls_node &n : nodes)
    if (get_attr_xtt_delay (n.insn) == XTT_DELAY_DYNAMIC
	&& delay_nop_needed_p (visited, bb, n.insn, XTT_DELAY_DYNAMIC))
      ++pads;
  return pads;
}

/* Deterministic list schedule of NODES honoring the DAG: critical-path
   priority, original order on ties.  Returns the chosen order.  */

static std::vector<int>
ls_list_order (std::vector<ls_node> &nodes)
{
  unsigned n = nodes.size ();

  /* Critical-path heights over the issue-distance weights.  */
  for (unsigned i = n; i--;)
    {
      long cp = nodes[i].words + nodes[i].lat;
      for (unsigned j = i + 1; j != n; ++j)
	{
	  int kind = ls_dependence (nodes[i], nodes[j]);
	  if (!kind)
	    continue;
	  long via = nodes[j].cp
	    + nodes[i].words + (kind == 1 ? nodes[i].lat : 0);
	  if (via > cp)
	    cp = via;
	}
      nodes[i].cp = cp;
    }

  std::vector<int> order;
  std::vector<bool> issued (n, false);
  std::vector<int> ready_at (n, 0);
  for (unsigned i = 0; i != n; ++i)
    ready_at[i] = nodes[i].entry_pin;

  int t = 0;
  while (order.size () != n)
    {
      int best = -1;
      int soonest = INT_MAX;
      for (unsigned i = 0; i != n; ++i)
	{
	  if (issued[i])
	    continue;
	  /* Readiness: every earlier-original dependence already issued
	     and its distance satisfied.  */
	  int ready = ready_at[i];
	  bool deps_done = true;
	  for (unsigned j = 0; j != n; ++j)
	    {
	      if (j == i || nodes[j].orig > nodes[i].orig)
		continue;
	      int kind = ls_dependence (nodes[j], nodes[i]);
	      if (!kind)
		continue;
	      if (!issued[j])
		{
		  deps_done = false;
		  break;
		}
	    }
	  if (!deps_done)
	    continue;
	  if (ready < soonest)
	    soonest = ready;
	  if (ready > t)
	    continue;
	  if (best < 0 || nodes[i].cp > nodes[best].cp
	      || (nodes[i].cp == nodes[best].cp
		  && nodes[i].orig < nodes[best].orig))
	    best = i;
	}
      if (best < 0)
	{
	  /* Nothing ready this slot: advance to the earliest ready
	     time (a modeled stall).  */
	  gcc_assert (soonest != INT_MAX && soonest > t);
	  t = soonest;
	  continue;
	}
      order.push_back (best);
      issued[best] = true;
      int done = t + nodes[best].words;
      /* Successor readiness floors.  */
      for (unsigned j = 0; j != n; ++j)
	{
	  if (issued[j] || j == (unsigned) best)
	    continue;
	  int kind = ls_dependence (nodes[best], nodes[j]);
	  if (!kind)
	    continue;
	  int need = done + (kind == 1 ? nodes[best].lat : 0);
	  if (need > ready_at[j])
	    ready_at[j] = need;
	}
      t = done;
    }
  return order;
}

/* Schedule one region.  NODES are the region members in original order
   (orig fields set).  ANCHOR is the unmoved insn immediately before the
   region.  UNAUDITED_DEFS is the entry-adjacent hazard: the entry
   producer's defs when its latency is outside the audited window
   (empty otherwise; deeper unknown-latency producers are unmodeled in
   both arms, see the head comment).  FORCED_ORDER, when given, replaces
   the list heuristic (the isomorphic-pair extension applies one
   region's chosen permutation to its sibling; legality is the caller's
   proven obligation via positional dependence-matrix equality);
   CHOSEN_ORDER, when given, receives the committed permutation.
   Returns true if the region was reordered (committed).  */

static bool
ls_schedule_region (basic_block bb, std::vector<ls_node> &nodes,
		    rtx_insn *anchor, rtx_insn *entry_producer,
		    rtx_insn *exit_consumer,
		    const HARD_REG_SET &unaudited_defs,
		    std::vector<basic_block> &visited,
		    const std::vector<int> *forced_order = nullptr,
		    std::vector<int> *chosen_order = nullptr)
{
  unsigned n = nodes.size ();

  /* Entry boundary: latency floor from the audited entry producer.
     An entry producer whose latency is unaudited or beyond the window
     contributes through UNAUDITED_DEFS (entry-adjacent, never a
     modeled floor).  */
  insn_regs ep_regs;
  CLEAR_HARD_REG_SET (ep_regs.uses);
  CLEAR_HARD_REG_SET (ep_regs.defs);
  int ep_lat = 0;
  if (entry_producer)
    {
      sfpu_reg_refs (entry_producer, &ep_regs);
      ep_lat = audited_latency (entry_producer);
      if (ep_lat < 0 || ep_lat > 1)
	ep_lat = 0;
    }
  for (unsigned i = 0; i != n; ++i)
    {
      nodes[i].entry_pin = 0;
      nodes[i].pin_to_baseline
	= hard_reg_set_intersect_p (unaudited_defs, nodes[i].regs.uses)
	  || hard_reg_set_intersect_p (unaudited_defs, nodes[i].raw_defs);
      if (entry_producer
	  && (hard_reg_set_intersect_p (ep_regs.defs, nodes[i].regs.uses)
	      || hard_reg_set_intersect_p (ep_regs.defs, nodes[i].raw_defs))
	  && ep_lat > nodes[i].entry_pin)
	nodes[i].entry_pin = ep_lat;
    }

  /* Exit boundary: nodes feeding the first following issued insn keep
     their trailing shadow in the makespan; a block-ending region drains
     everything.  */
  std::vector<bool> exit_shadow (n, false);
  if (exit_consumer)
    {
      insn_regs xc;
      sfpu_reg_refs (exit_consumer, &xc);
      HARD_REG_SET wanted = xc.uses;
      wanted |= xc.defs;
      for (unsigned i = 0; i != n; ++i)
	exit_shadow[i]
	  = hard_reg_set_intersect_p (nodes[i].raw_defs, wanted);
    }
  else
    for (unsigned i = 0; i != n; ++i)
      exit_shadow[i] = true;

  /* Baseline: the original order under the same model.  */
  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  std::vector<int> base_issue (n, 0);
  int base_end = ls_simulate (nodes, base_order, &base_issue, exit_shadow);

  /* Baseline pins land AFTER the baseline itself is modeled.  */
  for (unsigned i = 0; i != n; ++i)
    if (nodes[i].pin_to_baseline)
      nodes[i].entry_pin = base_issue[i];

  /* Candidate.  */
  std::vector<int> order
    = forced_order ? *forced_order : ls_list_order (nodes);
  std::vector<int> cand_issue (n, 0);
  int cand_end = ls_simulate (nodes, order, &cand_issue, exit_shadow);

  if (cand_end >= base_end)
    {
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no modeled makespan "
		 "decrease in bb %d region at uid=%d (%d -> %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_end, cand_end);
      return false;
    }

  /* Commit guard data: the nop inserter's pad-site count over the
     region members, AND the ENTRY producer's pad state -- reordering
     changes which member is physically first, which can flip the pad
     need of the preceding dynamic-delay producer (the vacated-seam
     discipline of the fill phases, prev_needed_before).  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  bool ep_dynamic
    = entry_producer
      && get_attr_xtt_delay (entry_producer) == XTT_DELAY_DYNAMIC;
  bool ep_needed_before
    = ep_dynamic
      && delay_nop_needed_p (visited, bb, entry_producer, XTT_DELAY_DYNAMIC);

  /* Exact-restore record: the chain from ANCHOR to the region's last
     member, debug insns included.  Notes are not recorded: a mid-block
     note can migrate relative to insns across a commit-then-restore
     (it emits no code; post-RA mid-block notes are rare).  */
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
      rtx_insn *insn = nodes[order[k]].insn;
      if (PREV_INSN (insn) != after)
	reorder_insns (insn, insn, after);
      after = insn;
    }

  unsigned pads_after = ls_pad_sites (visited, bb, nodes);
  bool ep_flipped
    = ep_dynamic && !ep_needed_before
      && delay_nop_needed_p (visited, bb, entry_producer,
			     XTT_DELAY_DYNAMIC);
  if (pads_after > pads_before || ep_flipped)
    {
      /* Restore the recorded chain exactly (debug insns included).  */
      after = anchor;
      for (rtx_insn *insn : chain)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: %s, restored bb %d "
		 "region at uid=%d\n",
		 ep_flipped ? "entry-producer pad flip"
			    : "pad-site increase",
		 bb->index, INSN_UID (nodes[0].insn));
      return false;
    }

  if (dump_file)
    {
      fprintf (dump_file, "List-schedule: bb %d nodes=%u makespan "
	       "%d -> %d target=%s\n",
	       bb->index, n, base_end, cand_end,
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "List-schedule slot=%d uid=%d\n",
		 cand_issue[order[k]], INSN_UID (nodes[order[k]].insn));
    }
  if (chosen_order)
    *chosen_order = order;
  return true;
}

/* ---- Round-chain interleave extensions (default off) ----

   -mtt-tensix-optimize-round-interleave lifts the two formation
   deferrals above for exactly the shapes whose proofs hold, refusing
   the rest by name:

   (1) SELF-LOOP rows: the deferral exists because the linear boundary
       model mispredicts the backedge seam.  The cyclic extension
       replaces the boundary terms with the seam itself: acceptance is
       judged on the STEADY-STATE INITIATION INTERVAL of the wrapped
       dependence model (the body simulated as replicated back-to-back
       copies under the same issue-distance rules, converged when two
       successive iteration start distances agree -- the achieved-II of
       the makespan oracle's RecMII extension), committed only on a
       strict II decrease.  The reorder itself never crosses the
       backedge (per-iteration semantics are untouched), so
       bit-exactness holds exactly as in the straight-line case.
       Admission fails closed: the row must be ONE region with no other
       issued Tensix word (a seam barrier word breaks the modeled
       adjacency), no replay owner, and no call.  Cross-block producers
       into iteration one remain unmodeled in baseline and candidate
       alike -- the same exposure class the straight-line pass carries
       for block-head regions.  Before judging the interleave, a
       region-scoped storage-collision rename (ls_cyclic_rename_
       collisions, the lreg-rename pass's discipline) breaks the
       allocator-packed false WAW/WAR recurrences between the unrolled
       copies; a refusal restores the original registers exactly.

   (2) REPEATED (isomorphic) region pairs: the deferral exists because
       sibling copies scheduled differently stop being textually
       isomorphic for the replay/MOP re-roll.  For EXACTLY TWO regions
       sharing one insn-code signature whose positional dependence
       matrices are equal, ONE permutation (the first region's list
       order) is applied to both: the copies stay isomorphic by
       construction, each region is judged by its own boundary model,
       and the commit is transactional across the PAIR (a second-region
       refusal restores the first).  Three or more copies stay deferred
       to replay formation (its re-roll material), and unequal
       dependence matrices refuse by name
       ("copies-not-dataflow-isomorphic").  */

/* Queue every occurrence of hard reg OLDR inside *LOC as a
   validate_change to NEWR (the lreg-rename pass's replacement helper,
   restated here for the region-scoped rename below).  */

void
ls_queue_reg_replacements (rtx_insn *insn, rtx *loc, unsigned oldr,
			   unsigned newr)
{
  rtx x = *loc;
  if (!x)
    return;
  if (REG_P (x))
    {
      if (REGNO (x) == oldr)
	validate_change (insn, loc, gen_rtx_REG (GET_MODE (x), newr), true);
      return;
    }
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	ls_queue_reg_replacements (insn, &XEXP (x, i), oldr, newr);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  ls_queue_reg_replacements (insn, &XVECEXP (x, i, j), oldr, newr);
    }
}

/* Storage-collision rename for the cyclic doubled row (round-interleave
   extension; the lreg-rename pass's discipline, region-scoped).  The
   allocator packs the two unrolled copies' short lifetimes into the
   same LREGs, manufacturing a false WAW/WAR recurrence that serializes
   the doubled row.  A colliding definition web -- node I defining reg
   R that an EARLIER region node also defines -- moves to a provably
   untouched LREG when:
   - R is not live INTO the block (excludes every loop-carried value:
     the self-loop's live-in is exactly the backedge-carried set);
   - the renamed value dies inside the region: a LATER region writer of
     R exists, or R is not live OUT of the block;
   - the defining write is not a read-modify-write of R (an implicit
     read of the colliding value never moves);
   - the target F is an allocatable LREG untouched by every region node
     (as currently composed), not live in or out of the block, and not
     fixed.
   The web = node I's definition plus every following reader of R, and
   THROUGH every read-modify-write redefinition of R (an in-place
   operation both consumes the renamed value and continues it --
   SFPMULI's destination IS its source -- so all its R occurrences
   move), ending exclusive before the next FRESH (non-reading) writer
   of R, which starts an unrelated value; reaching the region end
   instead needs R dead out of the block.  Every rename is recorded so
   a scheduling refusal restores the original registers EXACTLY (each
   web gets a fresh untouched F, so the inverse replacement F -> R over
   the region is unambiguous).  Value soundness under lane predication
   follows the region invariant the head comment establishes: a region
   executes under ONE CC state, every region write is masked by the
   same lane-enable set, and the region's outputs on disabled lanes
   come from pre-region register content, which the rename never
   touches (R keeps its pre-region content; F was dead).  */

/* Refresh every node's cached register sets from its (possibly just
   rewritten) pattern, so dependence tests see the renamed registers.  */

void
ls_refresh_node_regs (std::vector<ls_node> &nodes)
{
  for (ls_node &nd : nodes)
    {
      collect_sfpu_regs (nd.insn, &nd.regs);
      nd.raw_defs = nd.regs.defs;
    }
}

/* Rename the colliding definition webs of the region NODES in BB to
   provably untouched LREGs, per the contract in the comment above.
   START_ALLOWED, when given, restricts which nodes may root a web (the
   cross-row pairing's ambient all-lanes rule); SCAN_ORDER reorders
   only the root search -- web extents, collision detection, and member
   rewrites stay in stream index order; *NO_FREE_LREG, when given, is
   set once the free-register search comes up empty.  Every committed
   rename is appended to *RECORD for exact undo.  Returns true when at
   least one web was renamed.  */

bool
ls_cyclic_rename_collisions (basic_block bb, std::vector<ls_node> &nodes,
			     std::vector<ls_rename> *record,
			     const std::vector<bool> *start_allowed,
			     const std::vector<unsigned> *scan_order,
			     bool *no_free_lreg)
{
  unsigned n = nodes.size ();
  bool any = false;
  /* SCAN_ORDER reorders only the ROOT search (which fresh definitions
     are offered the free registers first); web extents, collision
     detection and member rewrites stay in stream index order.  The
     cross-row pairing's stall-words extension passes the copy half
     first: the row-B webs are the ones whose serialization the pairing
     exists to break, so they must not be starved of free LREGs by an
     intra-row false-recurrence rename that buys far less.  */
  for (unsigned ii = 0; ii != n; ++ii)
    {
    unsigned i = scan_order ? (*scan_order)[ii] : ii;
    for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
      {
	if (!TEST_HARD_REG_BIT (nodes[i].raw_defs, r))
	  continue;
	if (start_allowed && !(*start_allowed)[i])
	  {
	    /* Caller-scoped lane-domain restriction (cross-row pairing):
	       a fresh definition inside a CC atom executes lane-predicated,
	       so renaming its web to a dead LREG would leave stale disabled-
	       lane bits in the new register where the original register
	       carried the pre-atom value -- a later all-lanes consumer or
	       store could expose them.  Only webs rooted in the proven
	       ambient all-lanes state may rename (they write every lane at
	       the root, so the fresh register never exposes dead bits).  */
	    rvtt_refuse (RVTT_REF_CROSSROW_PAIRING_RENAME_CC_DOMAIN, dump_file,
			 "List-schedule rename refused: "
			 "crossrow-pairing-rename-cc-domain reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	bool earlier = false;
	for (unsigned j = 0; j != i && !earlier; ++j)
	  earlier = TEST_HARD_REG_BIT (nodes[j].raw_defs, r);
	if (!earlier)
	  continue;
	if (TEST_HARD_REG_BIT (nodes[i].regs.uses, r))
	  {
	    /* An RMW definition reads the COLLIDING value: the chain
	       start must be a fresh value.  */
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_RMW_DEF, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-rmw-def reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	if (REGNO_REG_SET_P (df_get_live_in (bb), r))
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_LIVE_IN, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-live-in reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	/* Web extent: from the fresh definition at I, forward through
	   every reader of R and THROUGH every read-modify-write
	   redefinition of R (an in-place operation both consumes the
	   renamed value and continues it -- SFPMULI's dest IS its
	   source -- so its R occurrences all move), ending exclusive
	   before the next FRESH (non-reading) writer of R, which
	   starts an unrelated value.  Reaching the region end without
	   such a writer needs R dead out of the block.  */
	unsigned extent_end = n;	/* exclusive */
	bool fresh_terminator = false;
	for (unsigned k = i + 1; k != n; ++k)
	  if (TEST_HARD_REG_BIT (nodes[k].raw_defs, r)
	      && !TEST_HARD_REG_BIT (nodes[k].regs.uses, r))
	    {
	      extent_end = k;
	      fresh_terminator = true;
	      break;
	    }
	if (!fresh_terminator && REGNO_REG_SET_P (df_get_live_out (bb), r))
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_LIVE_OUT, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-live-out reg %u uid=%d\n",
			 r, INSN_UID (nodes[i].insn));
	    continue;
	  }

	int f = -1;
	for (unsigned c = SFPU_REG_FIRST; c <= SFPU_REG_LAST && f < 0; ++c)
	  {
	    if (fixed_regs[c])
	      continue;
	    bool touched = false;
	    for (unsigned j = 0; j != n && !touched; ++j)
	      touched = TEST_HARD_REG_BIT (nodes[j].regs.uses, c)
			|| TEST_HARD_REG_BIT (nodes[j].raw_defs, c);
	    if (touched
		|| REGNO_REG_SET_P (df_get_live_in (bb), c)
		|| REGNO_REG_SET_P (df_get_live_out (bb), c))
	      continue;
	    f = (int) c;
	  }
	if (f < 0)
	  {
	    if (no_free_lreg)
	      *no_free_lreg = true;
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_NO_FREE_LREG,
			 dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-no-free-lreg reg %u "
			 "uid=%d\n", r, INSN_UID (nodes[i].insn));
	    return any;
	  }

	ls_rename rn;
	rn.oldr = r;
	rn.newr = (unsigned) f;
	ls_queue_reg_replacements (nodes[i].insn, &PATTERN (nodes[i].insn),
				   r, (unsigned) f);
	rn.insns.push_back (nodes[i].insn);
	for (unsigned k = i + 1; k != extent_end; ++k)
	  if (TEST_HARD_REG_BIT (nodes[k].regs.uses, r)
	      || TEST_HARD_REG_BIT (nodes[k].raw_defs, r))
	    {
	      ls_queue_reg_replacements (nodes[k].insn,
					 &PATTERN (nodes[k].insn),
					 r, (unsigned) f);
	      rn.insns.push_back (nodes[k].insn);
	    }
	if (!apply_change_group ())
	  {
	    rvtt_refuse (RVTT_REF_ROUND_INTERLEAVE_RENAME_CONSTRAINT, dump_file,
			 "List-schedule rename refused: "
			 "round-interleave-rename-constraint reg %u "
			 "uid=%d\n", r, INSN_UID (nodes[i].insn));
	    continue;
	  }
	for (rtx_insn *ins : rn.insns)
	  df_insn_rescan (ins);
	if (dump_file)
	  fprintf (dump_file, "List-schedule rename: reg %u -> %u web at "
		   "uid=%d (%zu insns) in bb %d\n",
		   r, (unsigned) f, INSN_UID (nodes[i].insn),
		   rn.insns.size (), bb->index);
	record->push_back (std::move (rn));
	ls_refresh_node_regs (nodes);
	any = true;
      }
    }
  return any;
}

/* Undo every recorded rename exactly (each web's F was untouched
   before, so replacing F back with R over the web is unambiguous).  */

void
ls_undo_renames (std::vector<ls_rename> &record)
{
  for (unsigned i = record.size (); i--;)
    {
      ls_rename &rn = record[i];
      for (rtx_insn *ins : rn.insns)
	ls_queue_reg_replacements (ins, &PATTERN (ins), rn.newr, rn.oldr);
      bool ok = apply_change_group ();
      gcc_assert (ok);
      for (rtx_insn *ins : rn.insns)
	df_insn_rescan (ins);
    }
  record.clear ();
}

/* Steady-state initiation interval of NODES issued repeatedly in ORDER:
   the wrapped (cyclic) issue model of a self-loop row.  Entry pins do
   not apply (the seam is the model); dependences reach across copies
   through the same ls_dependence vocabulary.  */

int
ls_cyclic_ii (const std::vector<ls_node> &nodes,
	      const std::vector<int> &order)
{
  return rvtt_timing::cyclic_ii (ls_timing_seq (nodes), order);
}

/* ---- Rau iterative modulo scheduling (default off) ----

   -mtt-tensix-optimize-ims adds an IMS-generated candidate order to
   the established cyclic paths (the one-region wrapped row below, and
   the interior regions of ls_schedule_cyclic_interior).  The engine
   side lives in rvtt-timing.h's modulo tier: MII = max(ResMII,
   RecMII-exact) over the dependence-distance graph marshalled from
   the SAME seq/dep vocabulary the acceptance simulator consumes
   (make_mod_prob -- one marshaller, so the MRT and the acceptance
   model cannot drift), then Rau's budgeted-eviction placement against
   a single-issue modulo reservation table of II columns.

   The committed transform is a within-region permutation exactly like
   the list order's (sort the placement by issue slot, original index
   on ties): the ACCEPTANCE authority is unchanged -- strict whole-row
   steady-state II decrease under ls_cyclic_ii, plus the pad-site and
   entry-pad-flip guards -- so the IMS can never book a modeled
   regression, and bit-exactness holds by the established argument
   (barrier words and the backedge are never crossed).

   Modulo variable expansion is priced, never assumed: a placement
   whose value lifetimes exceed the II owes kmin = ceil(maxlife/II)
   kernel copies (Lam, PLDI 1988).  The kmin > 1 case commits only its
   flat order (judged by the same acceptance); the rename half is
   bounded fail-closed -- when the steady-state live-copy demand does
   not fit the register file net of loop-live invariants (capacity
   through the pressure engine's one constant, rvtt_pressure_capacity), or the
   region rename search already exhausted the free LREGs (the 8-LREG
   wall), the IMS candidate refuses `mve-rename-exhausted'.  The
   kernel-unroll realization of kmin > 1 placements is the item's
   staged follow-up (the crp-parity ceremony's territory), not this
   flag.

   Refusals by name (existing schedule kept byte-identically):
     ims-unaudited-latency             a result-bearing or acceptance-
					stall row word without an audited
					in-window latency (IMS places
					under no floored fact)
     ims-budget-exhausted              Rau eviction budget ran out at
					every II below the bound
     ims-no-ii-decrease                MII at or above the current II,
					or no accepted candidate
     ims-order-hazard                  legality-belt reversal (cannot
					fire by construction; belts stay)
     ims-dependence-distance-unproven  a cross-iteration edge deeper
					than distance 1 (outside the
					marshalled vocabulary)
     mve-rename-exhausted              kmin > 1 with unfittable rename
					demand (see above)  */

namespace {

struct ls_ims_candidate
{
  std::vector<int> order;
  int resmii = 0;
  int recmii = 0;
  int mii = 0;
  int place_ii = 0;
  int kmin = 1;
  unsigned demand = 0;
};

} /* anonymous namespace */

/* Generate the IMS candidate order for the region NODES (admitted
   members, audited 0/1-slot latencies).  MAX_II is the acceptance
   bound: only a placement that could prove II strictly below it is
   worth offering.  RENAME_EXHAUSTED carries the region renamer's
   no-free-lreg signal into the MVE register bound.  Returns false with
   the refusal named; never mutates NODES.  */

static bool
ls_ims_order (basic_block bb, const std::vector<ls_node> &nodes,
	      int max_ii, bool rename_exhausted, ls_ims_candidate *out)
{
  const unsigned n = nodes.size ();
  rvtt_timing::mod_prob prob
    = rvtt_timing::make_mod_prob (ls_timing_seq (nodes));

  /* Distance vocabulary belt: the marshaller emits distances 0 and 1
     only; anything deeper is outside the proven dependence vocabulary
     and refuses by name (fail-closed against future callers).  */
  for (unsigned k = 0; k != prob.edges.size (); ++k)
    if (prob.edges[k].omega > 1)
      {
	rvtt_refuse (RVTT_REF_IMS_DEPENDENCE_DISTANCE_UNPROVEN, dump_file,
		     "List-schedule (ims) refused: "
		     "ims-dependence-distance-unproven at uid=%d in bb %d\n",
		     INSN_UID (nodes[0].insn), bb->index);
	return false;
      }

  out->resmii = rvtt_timing::resmii (prob);
  out->recmii = rvtt_timing::recmii (prob);
  out->mii = out->resmii > out->recmii ? out->resmii : out->recmii;
  if (dump_file)
    fprintf (dump_file, "List-schedule (ims) region: bb %d nodes=%u "
	     "ResMII=%d RecMII=%d MII=%d\n",
	     bb->index, n, out->resmii, out->recmii, out->mii);
  if (out->recmii < 0 || out->mii >= max_ii)
    {
      rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
		   "List-schedule (ims) refused: ims-no-ii-decrease at "
		   "uid=%d in bb %d (MII %d >= II %d)\n",
		   INSN_UID (nodes[0].insn), bb->index, out->mii, max_ii);
      return false;
    }

  int budget = riscv_tt_ims_budget > 0 ? (int) riscv_tt_ims_budget
					: 8 * (int) n;
  rvtt_timing::mod_placement pl
    = rvtt_timing::ims_schedule (prob, out->mii, max_ii - 1, budget);
  if (!pl.scheduled)
    {
      rvtt_refuse (RVTT_REF_IMS_BUDGET_EXHAUSTED, dump_file,
		   "List-schedule (ims) refused: ims-budget-exhausted at "
		   "uid=%d in bb %d (MII %d, bound %d, budget %d)\n",
		   INSN_UID (nodes[0].insn), bb->index, out->mii, max_ii,
		   budget);
      return false;
    }
  out->place_ii = pl.ii;
  out->kmin = rvtt_timing::mve_kmin (prob, pl);
  out->demand = rvtt_timing::mve_live_demand (prob, pl);

  if (out->kmin > 1)
    {
      /* The placement's value lifetimes exceed the II: realizing its
	 overlap needs kmin kernel copies (modulo variable expansion) --
	 the item's staged follow-up, never performed here.  The
	 EXPANSION is adjudicated by name now: its steady state carries
	 DEMAND simultaneously-live value copies, which must fit the
	 file net of the loop-live invariants (live into the row,
	 defined by no region node), and the region rename search must
	 not already have exhausted the free LREGs.  Either way the
	 FLAT order below is still offered -- it realizes only what the
	 wrapped acceptance model proves, so it can never ride the
	 unrealized overlap.  */
      unsigned invariants = 0;
      for (unsigned r = SFPU_REG_FIRST; r <= SFPU_REG_LAST; ++r)
	{
	  if (!REGNO_REG_SET_P (df_get_live_in (bb), r))
	    continue;
	  bool defined = false;
	  for (unsigned i = 0; i != n && !defined; ++i)
	    defined = TEST_HARD_REG_BIT (nodes[i].raw_defs, r);
	  if (!defined)
	    ++invariants;
	}
      unsigned capacity = rvtt_pressure_capacity ();
      unsigned net = capacity > invariants ? capacity - invariants : 0;
      if (rename_exhausted || out->demand > net)
	rvtt_refuse (RVTT_REF_MVE_RENAME_EXHAUSTED, dump_file,
		     "List-schedule (ims) refused: mve-rename-exhausted "
		     "at uid=%d in bb %d (kmin=%d demand=%u capacity=%u "
		     "invariants=%u%s; flat order still offered)\n",
		     INSN_UID (nodes[0].insn), bb->index, out->kmin,
		     out->demand, capacity, invariants,
		     rename_exhausted ? " rename-search-exhausted" : "");
      else if (riscv_tt_opt_mve_expand)
	/* Stage 2 is live: the owed expansion is adjudicated by name.
	   This region path cannot perform it -- the realization
	   authority is the counted-kernel seam in the cross-row
	   pairing (which runs before these paths and owns the
	   replay-formable unroll); a region of a barrier-chopped or
	   otherwise non-counted row has no sound kernel copy.  The
	   flat order still competes exactly as before.  */
	rvtt_refuse (RVTT_REF_MVE_EXPAND_ROW_NOT_COUNTED_KERNEL, dump_file,
		     "List-schedule (ims) refused: "
		     "mve-expand-row-not-counted-kernel at uid=%d in bb %d "
		     "(kmin=%d demand=%u invariants=%u fits; flat order "
		     "still offered)\n",
		     INSN_UID (nodes[0].insn), bb->index, out->kmin,
		     out->demand, invariants);
      else if (dump_file)
	fprintf (dump_file, "List-schedule (ims) MVE owed: bb %d kmin=%d "
		 "demand=%u invariants=%u (fits; kernel unroll is the "
		 "staged follow-up, flat order offered)\n",
		 bb->index, out->kmin, out->demand, invariants);
    }

  /* Committed order: placement slots ascending, original index on
     ties.  Intra-iteration edges force strict slot increase, so the
     sort respects every original-order dependence by construction;
     the belt below re-verifies anyway (fail-closed).  */
  out->order.clear ();
  for (unsigned i = 0; i != n; ++i)
    out->order.push_back ((int) i);
  for (unsigned i = 1; i < n; ++i)
    {
      int v = out->order[i];
      unsigned j = i;
      while (j > 0 && (pl.sigma[out->order[j - 1]] > pl.sigma[v]
		       || (pl.sigma[out->order[j - 1]] == pl.sigma[v]
			   && out->order[j - 1] > v)))
	{
	  out->order[j] = out->order[j - 1];
	  --j;
	}
      out->order[j] = v;
    }
  std::vector<int> pos (n, 0);
  for (unsigned k = 0; k != n; ++k)
    pos[out->order[k]] = (int) k;
  for (unsigned i = 0; i != n; ++i)
    for (unsigned j = i + 1; j != n; ++j)
      if (ls_dependence (nodes[i], nodes[j]) && pos[i] > pos[j])
	{
	  rvtt_refuse (RVTT_REF_IMS_ORDER_HAZARD, dump_file,
		       "List-schedule (ims) refused: ims-order-hazard at "
		       "uid=%d/uid=%d in bb %d\n",
		       INSN_UID (nodes[i].insn), INSN_UID (nodes[j].insn),
		       bb->index);
	  return false;
	}
  if (dump_file)
    fprintf (dump_file, "List-schedule (ims) placed: bb %d place-II=%d "
	     "kmin=%d demand=%u\n",
	     bb->index, out->place_ii, out->kmin, out->demand);
  return true;
}

/* Under -mtt-tensix-optimize-ims a self-loop row whose issued words
   include a result-bearing (or acceptance-stall) word WITHOUT an
   audited in-window result latency refuses IMS treatment wholesale, by
   name: IMS is a placement authority and places under no floored fact
   (the legacy cyclic paths keep their identically-floored acceptance
   model and are untouched).  Defless words (stores, pure CC writers)
   carry no result latency the model would consult -- their lat never
   weights an edge -- so they do not refuse.  */

static bool
ls_ims_row_audited_p (basic_block bb)
{
  bool ok = true;
  for (rtx_insn *insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn) || GET_CODE (insn) != INSN
	  || recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	continue;
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	continue;	/* the row-level opaque refusals own this */
      if (!e.lreg_write && !e.next_slot_stall)
	continue;
      int lat = audited_latency (insn);
      if (lat < 0 || lat > 1)
	{
	  rvtt_refuse (RVTT_REF_IMS_UNAUDITED_LATENCY, dump_file,
		       "List-schedule (ims) refused: ims-unaudited-latency "
		       "uid=%d in bb %d\n", INSN_UID (insn), bb->index);
	  ok = false;
	}
    }
  return ok;
}

/* Cyclic scheduling of the single region of a self-loop row.
   Transactional exactly like ls_schedule_region; acceptance = strict
   steady-state II decrease; the same pad-site commit guard applies
   (the nop inserter's probe is the WH correctness carrier and must not
   grow).  */

static bool
ls_schedule_region_cyclic (basic_block bb, std::vector<ls_node> &nodes,
			   rtx_insn *anchor,
			   std::vector<basic_block> &visited)
{
  unsigned n = nodes.size ();
  for (unsigned i = 0; i != n; ++i)
    {
      nodes[i].entry_pin = 0;
      nodes[i].pin_to_baseline = false;
    }

  /* Guard metric and baseline on the ORIGINAL code, before any
     rename.  */
  unsigned pads_before = ls_pad_sites (visited, bb, nodes);
  std::vector<int> base_order (n);
  for (unsigned i = 0; i != n; ++i)
    base_order[i] = i;
  int base_ii = ls_cyclic_ii (nodes, base_order);

  /* Break storage-induced false recurrences before judging the
     interleave; every rename is undone on refusal.  */
  std::vector<ls_rename> renames;
  bool rename_exhausted = false;
  ls_cyclic_rename_collisions (bb, nodes, &renames, nullptr, nullptr,
			       &rename_exhausted);

  /* Candidate orders, one per enabled mechanism, all judged by the one
     wrapped acceptance model: the deterministic list order
     (round-interleave), and the IMS placement order.  */
  std::vector<int> order;
  int cand_ii = INT_MAX;
  bool used_ims = false;
  if (riscv_tt_opt_round_interleave)
    {
      order = ls_list_order (nodes);
      cand_ii = ls_cyclic_ii (nodes, order);
    }
  if (riscv_tt_opt_ims)
    {
      ls_ims_candidate ims;
      if (ls_ims_order (bb, nodes, base_ii, rename_exhausted, &ims))
	{
	  int ims_ii = ls_cyclic_ii (nodes, ims.order);
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (ims) candidate: bb %d "
		     "modeled II %d (place-II %d MII %d)\n",
		     bb->index, ims_ii, ims.place_ii, ims.mii);
	  if (ims_ii < cand_ii)
	    {
	      order = ims.order;
	      cand_ii = ims_ii;
	      used_ims = true;
	    }
	}
    }

  if (order.empty () || cand_ii >= base_ii)
    {
      ls_undo_renames (renames);
      if (riscv_tt_opt_ims && !order.empty ())
	rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
		     "List-schedule (ims) refused: ims-no-ii-decrease at "
		     "uid=%d in bb %d (%d -> %d)\n",
		     INSN_UID (nodes[0].insn), bb->index, base_ii, cand_ii);
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no modeled "
		 "steady-state II decrease in bb %d cyclic region at "
		 "uid=%d (%d -> %d)\n",
		 bb->index, INSN_UID (nodes[0].insn), base_ii,
		 cand_ii == INT_MAX ? base_ii : cand_ii);
      return false;
    }

  /* Exact-restore record, debug insns included.  */
  std::vector<rtx_insn *> chain;
  for (rtx_insn *w = NEXT_INSN (anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain.push_back (w);
      if (w == nodes[n - 1].insn)
	break;
    }

  rtx_insn *after = anchor;
  for (unsigned k = 0; k != n; ++k)
    {
      rtx_insn *insn = nodes[order[k]].insn;
      if (PREV_INSN (insn) != after)
	reorder_insns (insn, insn, after);
      after = insn;
    }

  unsigned pads_after = ls_pad_sites (visited, bb, nodes);
  if (pads_after > pads_before)
    {
      after = anchor;
      for (rtx_insn *insn : chain)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      ls_undo_renames (renames);
      rvtt_refuse (RVTT_REF_PAD_SITE, dump_file,
		   "List-schedule refused: pad-site increase, "
		   "restored bb %d cyclic region at uid=%d\n",
		   bb->index, INSN_UID (nodes[0].insn));
      return false;
    }

  if (dump_file)
    {
      fprintf (dump_file, "List-schedule (%s cyclic): "
	       "bb %d nodes=%u II %d -> %d renames=%zu target=%s\n",
	       used_ims ? "ims" : "round-interleave",
	       bb->index, n, base_ii, cand_ii, renames.size (),
	       TARGET_XTT_TENSIX_WH ? "wh" : "bh");
      for (unsigned k = 0; k != n; ++k)
	fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		 k, INSN_UID (nodes[order[k]].insn));
    }
  return true;
}

/* The entry producer of a region starting at FIRST: the nearest
   preceding instruction the DYNAMIC pad probe (find_next_insn) would
   treat as word-adjacent to the region's first member.  The probe
   SKIPS non-Tensix insns, USE/CLOBBER markers, and non-dependent
   zero-length ghosts, so a scalar RISC insn in the gap (a loop
   counter, an address materialization) does NOT break the adjacency:
   the walk here skips exactly the probe's vocabulary, or the entry
   pin, latency floor, and pad-flip guard would all silently bypass
   across a one-scalar gap.  */

static rtx_insn *
ls_entry_producer (basic_block bb, rtx_insn *first)
{
  for (rtx_insn *w = PREV_INSN (first); w && w != PREV_INSN (BB_HEAD (bb));
       w = PREV_INSN (w))
    {
      if (!NONDEBUG_INSN_P (w))
	continue;
      if (GET_CODE (w) != INSN)
	/* Jump/call boundary.  KNOWN DIVERGENCE from the dynamic pad
	   probe (DU-S8(a), still open): find_next_insn walks THROUGH a
	   call while this walk stops, so a region entered right after a
	   call sees no entry producer and keeps the conservative
	   latency floor -- refusal-direction only (a fill opportunity
	   is missed, never a hazard admitted).  */
	return nullptr;
      rtx pat = PATTERN (w);
      if (GET_CODE (pat) == USE || GET_CODE (pat) == CLOBBER)
	continue;
      if (recog_memoized (w) < 0)
	return nullptr;
      if (get_attr_type (w) != TYPE_TENSIX)
	continue;		/* scalar: probe-transparent */
      if (!get_attr_length (w))
	continue;		/* zero-length marker: no word, no event */
      return w;
    }
  return nullptr;
}

/* One collected candidate region of a block.  */

struct ls_region
{
  std::vector<ls_node> nodes;
  rtx_insn *anchor;
  rtx_insn *entry_producer;
  HARD_REG_SET unaudited_defs;	/* entry producer's defs when its
				   latency is out of the audited window */
  std::vector<int> signature;	/* insn codes, for the repeat deferral */
};

/* Isomorphic-pair scheduling (round-interleave extension): apply R1's
   chosen permutation to R2, keeping the copies textually isomorphic
   for the replay/MOP re-roll.  Legality of the shared permutation
   rests on positional dependence-matrix equality, proven before any
   motion; each region is judged by its own boundary model; the commit
   is transactional across the PAIR (a second-region refusal restores
   the first exactly).  */

static void
ls_schedule_iso_pair (basic_block bb, ls_region &r1, ls_region &r2,
		      std::vector<basic_block> &visited)
{
  unsigned n = r1.nodes.size ();
  if (r2.nodes.size () != n)
    return;	/* signatures equal implies equal sizes; belt only.  */

  for (unsigned i = 0; i != n; ++i)
    for (unsigned j = i + 1; j != n; ++j)
      if (ls_dependence (r1.nodes[i], r1.nodes[j])
	  != ls_dependence (r2.nodes[i], r2.nodes[j]))
	{
	  rvtt_refuse (RVTT_REF_COPIES_NOT_DATAFLOW_ISOMORPHIC, dump_file,
		       "List-schedule refused: "
		       "copies-not-dataflow-isomorphic at uid=%d/uid=%d "
		       "in bb %d (repeated-row pair keeps its deferral)\n",
		       INSN_UID (r1.nodes[0].insn),
		       INSN_UID (r2.nodes[0].insn), bb->index);
	  return;
	}

  /* First region's exact-restore record, captured BEFORE its commit so
     a second-region refusal can undo the pair.  */
  std::vector<rtx_insn *> chain1;
  for (rtx_insn *w = NEXT_INSN (r1.anchor);; w = NEXT_INSN (w))
    {
      if (INSN_P (w))
	chain1.push_back (w);
      if (w == r1.nodes[n - 1].insn)
	break;
    }

  std::vector<int> order;
  if (!ls_schedule_region (bb, r1.nodes, r1.anchor, r1.entry_producer,
			   next_issued_insn (bb, r1.nodes.back ().insn),
			   r1.unaudited_defs, visited, nullptr, &order))
    return;	/* first copy refused; nothing moved.  */

  if (!ls_schedule_region (bb, r2.nodes, r2.anchor, r2.entry_producer,
			   next_issued_insn (bb, r2.nodes.back ().insn),
			   r2.unaudited_defs, visited, &order, nullptr))
    {
      /* Restore the first region exactly: pair-transactional.  */
      rtx_insn *after = r1.anchor;
      for (rtx_insn *insn : chain1)
	{
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}
      rvtt_refuse (RVTT_REF_ISO_PAIR, dump_file,
		   "List-schedule refused: iso-pair sibling at "
		   "uid=%d would not improve, restored pair in bb %d\n",
		   INSN_UID (r2.nodes[0].insn), bb->index);
      return;
    }

  if (dump_file)
    fprintf (dump_file, "List-schedule (round-interleave iso-pair): "
	     "bb %d regions at uid=%d/uid=%d share one permutation "
	     "(isomorphism preserved)\n",
	     bb->index, INSN_UID (r1.nodes[0].insn),
	     INSN_UID (r2.nodes[0].insn));
}

/* ---- Cyclic-interior region scheduling (default off) ----

   -mtt-tensix-optimize-cyclic-region-schedule lifts the self-loop
   deferral for the MULTI-REGION row shape the one-region cyclic
   extension refuses (round-interleave-seam-barrier-word /
   -row-not-one-region): a row chopped by issued barrier words (CC
   writes, Dst traffic, config accesses) into several straight-line
   regions.  Every barrier word keeps its position; each INTERIOR
   region is re-list-scheduled under the established region vocabulary
   (admission, entry pins, deterministic list order), and the candidate
   commits only on a STRICT decrease of the WHOLE ROW's modeled
   steady-state initiation interval -- the wrapped cyclic issue model
   (ls_cyclic_ii) over EVERY issued word of the block, with unaudited
   latencies floored at ZERO identically in baseline and candidate (a
   modeled lower bound, never a claimed cycle count: only the strict
   decrease is acted on, and a floored latency can only hide a stall
   both orders share).  The linear boundary model that motivated the
   self-loop deferral is never consulted for acceptance.

   SOUNDNESS (why a within-region reorder is cyclically bit-exact):
   region members move only relative to each other; barrier words and
   region boundaries are fixed.  A dependence between two ITERATIONS
   either involves a fixed word, or connects iteration i's instance of
   the region to iteration i+1's instance -- and in the concatenated
   stream every word of the earlier instance precedes every word of
   the later one regardless of the interior permutation.  Dependences
   WITHIN one iteration are the region DAG's, honored by the list
   order exactly as in the straight-line case (the same fail-closed
   ls_dependence vocabulary; predicated RMW uses include defs).

   Refusals by name (original order kept byte-identically):
     cyclic-interior-opaque-word     raw asm / opaque effects in the row
     cyclic-interior-backedge-seam   region contains the row's first or
				     last issued word (the boundary the
				     deferral exists for)
     cyclic-interior-repeated-shape  region signature repeats in the row
				     (replay/MOP re-roll owns copy
				     isomorphism)
     cyclic-interior-no-ii-decrease  candidate II >= current II
   plus the pad-site / entry-pad-flip commit guards of the
   straight-line scheduler (the WH correctness carrier).  The row-level
   replay-owner and call refusals are the caller's.  */

static void
ls_schedule_cyclic_interior (basic_block bb,
			     std::vector<ls_region> &regions,
			     std::vector<basic_block> &visited)
{
  /* Whole-row model: every issued Tensix word, in order.  */
  std::vector<ls_node> body;
  for (rtx_insn *insn = BB_HEAD (bb); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (GET_CODE (insn) == INSN && PATTERN (insn)
	  && asm_noperands (PATTERN (insn)) >= 0)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_OPAQUE_WORD, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-opaque-word uid=%d in bb %d\n",
		       INSN_UID (insn), bb->index);
	  return;
	}
      if (GET_CODE (insn) != INSN || recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	continue;		/* scalar control / ghost: no word */
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (e.opaque)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_OPAQUE_WORD, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-opaque-word uid=%d in bb %d\n",
		       INSN_UID (insn), bb->index);
	  return;
	}
      ls_node nd;
      nd.insn = insn;
      nd.lat = audited_latency (insn);
      if (nd.lat < 0 || nd.lat > 1)
	nd.lat = 0;		/* model floor, charged both sides */
      nd.words = get_attr_length (insn) / 4;
      if (e.next_slot_stall)
	/* The architectural acceptance stall is an issue fact: one
	   extra slot per occurrence (the crossrow pairing's priced
	   rule), identical in baseline and candidate.  */
	nd.words += 1;
      if (!collect_sfpu_regs (insn, &nd.regs))
	/* Defless CC/store words: keep their real LREG uses for RAW
	   ordering (position fixed anyway -- they are never region
	   members).  */
	sfpu_reg_refs (insn, &nd.regs);
      nd.raw_defs = nd.regs.defs;
      nd.orig = (int) body.size ();
      nd.cp = 0;
      nd.ready = 0;
      nd.entry_pin = 0;
      nd.pin_to_baseline = false;
      body.push_back (nd);
    }
  if (body.empty ())
    return;

  const unsigned bn = body.size ();
  std::vector<int> body_order (bn);
  for (unsigned i = 0; i != bn; ++i)
    body_order[i] = i;
  int cur_ii = ls_cyclic_ii (body, body_order);

  /* Item #5: IMS treats no floored latency fact as placement input --
     a result-bearing word without an audited in-window latency refuses
     the whole row by name (the legacy path's identically-floored
     acceptance is untouched).  The row-level ResMII/RecMII line is the
     exact-tier floor artifact (the DT/EI chain-bound cross-check
     anchor).  */
  bool ims_row_ok = false;
  if (riscv_tt_opt_ims)
    {
      ims_row_ok = ls_ims_row_audited_p (bb);
      if (ims_row_ok && dump_file)
	{
	  rvtt_timing::mod_prob rowp
	    = rvtt_timing::make_mod_prob (ls_timing_seq (body));
	  fprintf (dump_file, "List-schedule (ims) row: bb %d words=%u "
		   "ResMII=%d RecMII=%d row-II=%d\n",
		   bb->index, bn, rvtt_timing::resmii (rowp),
		   rvtt_timing::recmii (rowp), cur_ii);
	}
    }

  for (unsigned ri = 0; ri != regions.size (); ++ri)
    {
      ls_region &r = regions[ri];
      const unsigned n = r.nodes.size ();

      /* Replay/MOP re-roll isomorphism: repeated shapes defer.  */
      bool repeated = false;
      for (unsigned rj = 0; rj != regions.size (); ++rj)
	if (rj != ri && regions[rj].signature == r.signature)
	  repeated = true;
      if (repeated)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_REPEATED_SHAPE, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-repeated-shape at uid=%d in bb %d\n",
		       INSN_UID (r.nodes[0].insn), bb->index);
	  continue;
	}

      /* Locate the region inside the body model: admitted nodes are
	 consecutive issued words (only barrier words separate
	 regions), so the run is contiguous.  */
      unsigned first = bn;
      for (unsigned i = 0; i != bn; ++i)
	if (body[i].insn == r.nodes[0].insn)
	  {
	    first = i;
	    break;
	  }
      gcc_assert (first != bn && first + n <= bn);
      for (unsigned k = 0; k != n; ++k)
	gcc_assert (body[first + k].insn == r.nodes[k].insn);

      /* Interior only: a region containing the row's first or last
	 issued word sits on the backedge seam this pass never
	 models.  */
      if (first == 0 || first + n == bn)
	{
	  rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_BACKEDGE_SEAM, dump_file,
		       "List-schedule (cyclic-interior) refused: "
		       "cyclic-interior-backedge-seam at uid=%d in bb %d\n",
		       INSN_UID (r.nodes[0].insn), bb->index);
	  continue;
	}

      /* R1 (-mtt-tensix-optimize-rename-temporal): break the region's
	 storage-collision chains through the du-chain rename service
	 BEFORE generating candidates.  The service carries the
	 complete legality proof (typed-effect veto, span/CC rule,
	 death proof, temporal-target admission, post-commit belt);
	 this consumer only selects collision roots -- a region
	 member's fresh LREG definition another issued row word also
	 defines -- and prices the composition through the unchanged
	 whole-row acceptance below, undoing every requested rename
	 exactly when no candidate commits.  Each root is attempted at
	 most once (a committed rename must never be re-rooted -- the
	 rename ping-pong guard).  The pristine pad census guards the
	 rename half exactly as the one-region path's original-code
	 baseline discipline does.  */
      std::vector<rvtt_lreg_rename_web> region_webs;
      auto refresh_nodes = [] (std::vector<ls_node> &nodes)
	{
	  /* The body model's own collection form: defless CC/store
	     words keep their real LREG uses via the reference scan
	     (they are never renamed, but their reads order the row).  */
	  for (ls_node &nd : nodes)
	    {
	      if (!collect_sfpu_regs (nd.insn, &nd.regs))
		sfpu_reg_refs (nd.insn, &nd.regs);
	      nd.raw_defs = nd.regs.defs;
	    }
	};
      auto undo_region_renames = [&] ()
	{
	  if (region_webs.empty ())
	    return;
	  for (unsigned i = region_webs.size (); i--;)
	    rvtt_lreg_rename_web_undo (region_webs[i]);
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (interior-rename): undid "
		     "%zu chain rename(s) in bb %d region at uid=%d\n",
		     region_webs.size (), bb->index,
		     INSN_UID (r.nodes[0].insn));
	  region_webs.clear ();
	  refresh_nodes (r.nodes);
	  refresh_nodes (body);
	};
      if (riscv_tt_opt_rename_temporal
	  && (riscv_tt_opt_cyclic_region_schedule
	      || (riscv_tt_opt_ims && ims_row_ok)))
	{
	  /* Earlier regions' committed renames may have moved webs.  */
	  refresh_nodes (r.nodes);
	  unsigned pads_pristine = ls_pad_sites (visited, bb, r.nodes);
	  for (unsigned k = 0; k != n; ++k)
	    for (unsigned reg = SFPU_REG_FIRST; reg <= SFPU_REG_LAST; ++reg)
	      {
		if (!TEST_HARD_REG_BIT (r.nodes[k].raw_defs, reg)
		    || TEST_HARD_REG_BIT (r.nodes[k].regs.uses, reg))
		  continue;	/* not a fresh-definition chain root */
		bool collision = false;
		for (unsigned j = 0; j != bn && !collision; ++j)
		  if (j != first + k
		      && TEST_HARD_REG_BIT (body[j].raw_defs, reg))
		    collision = true;
		if (!collision)
		  continue;
		rvtt_lreg_rename_web web;
		if (!rvtt_lreg_rename_chain (bb, r.nodes[k].insn, -1, &web))
		  break;	/* refused by name in the service */
		region_webs.push_back (web);
		refresh_nodes (r.nodes);
		refresh_nodes (body);
		if (dump_file)
		  fprintf (dump_file, "List-schedule (interior-rename): "
			   "chain L%d -> L%d at uid=%d in bb %d region at "
			   "uid=%d\n", web.old_l, web.new_l,
			   INSN_UID (r.nodes[k].insn), bb->index,
			   INSN_UID (r.nodes[0].insn));
		break;		/* one rename attempt per root insn */
	      }
	  if (!region_webs.empty ()
	      && ls_pad_sites (visited, bb, r.nodes) > pads_pristine)
	    {
	      undo_region_renames ();
	      rvtt_refuse (RVTT_REF_PAD_SITE, dump_file,
			   "List-schedule (interior-rename) refused: "
			   "pad-site increase, undid renames in bb %d "
			   "region at uid=%d\n",
			   bb->index, INSN_UID (r.nodes[0].insn));
	    }
	}

      /* Entry pins: the straight-line scheduler's own discipline over
	 the region's acyclic baseline (candidate construction only;
	 acceptance is the cyclic model below).  */
      insn_regs ep_regs;
      CLEAR_HARD_REG_SET (ep_regs.uses);
      CLEAR_HARD_REG_SET (ep_regs.defs);
      int ep_lat = 0;
      if (r.entry_producer)
	{
	  sfpu_reg_refs (r.entry_producer, &ep_regs);
	  ep_lat = audited_latency (r.entry_producer);
	  if (ep_lat < 0 || ep_lat > 1)
	    ep_lat = 0;
	}
      for (unsigned i = 0; i != n; ++i)
	{
	  r.nodes[i].entry_pin = 0;
	  r.nodes[i].pin_to_baseline
	    = hard_reg_set_intersect_p (r.unaudited_defs,
					r.nodes[i].regs.uses)
	      || hard_reg_set_intersect_p (r.unaudited_defs,
					   r.nodes[i].raw_defs);
	  if (r.entry_producer
	      && (hard_reg_set_intersect_p (ep_regs.defs,
					    r.nodes[i].regs.uses)
		  || hard_reg_set_intersect_p (ep_regs.defs,
					       r.nodes[i].raw_defs))
	      && ep_lat > r.nodes[i].entry_pin)
	    r.nodes[i].entry_pin = ep_lat;
	}
      rtx_insn *exit_consumer
	= next_issued_insn (bb, r.nodes[n - 1].insn);
      std::vector<bool> exit_shadow (n, false);
      if (exit_consumer)
	{
	  insn_regs xc;
	  sfpu_reg_refs (exit_consumer, &xc);
	  HARD_REG_SET wanted = xc.uses;
	  wanted |= xc.defs;
	  for (unsigned i = 0; i != n; ++i)
	    exit_shadow[i]
	      = hard_reg_set_intersect_p (r.nodes[i].raw_defs, wanted);
	}
      else
	for (unsigned i = 0; i != n; ++i)
	  exit_shadow[i] = true;
      std::vector<int> base_order (n);
      for (unsigned i = 0; i != n; ++i)
	base_order[i] = i;
      std::vector<int> base_issue (n, 0);
      ls_simulate (r.nodes, base_order, &base_issue, exit_shadow);
      for (unsigned i = 0; i != n; ++i)
	if (r.nodes[i].pin_to_baseline)
	  r.nodes[i].entry_pin = base_issue[i];

      /* Candidates, one per enabled mechanism, all judged on the WHOLE
	 row's steady-state II: the deterministic list order (the
	 cyclic-interior flag), and the IMS placement order.  */
      std::vector<int> order;
      int cand_ii = INT_MAX;
      bool used_ims = false;
      if (riscv_tt_opt_cyclic_region_schedule)
	{
	  order = ls_list_order (r.nodes);
	  std::vector<int> cb (body_order);
	  for (unsigned k = 0; k != n; ++k)
	    cb[first + k] = (int) (first + order[k]);
	  cand_ii = ls_cyclic_ii (body, cb);
	}
      if (riscv_tt_opt_ims && ims_row_ok)
	{
	  ls_ims_candidate ims;
	  if (ls_ims_order (bb, r.nodes, cur_ii, false, &ims))
	    {
	      std::vector<int> cb (body_order);
	      for (unsigned k = 0; k != n; ++k)
		cb[first + k] = (int) (first + ims.order[k]);
	      int ims_ii = ls_cyclic_ii (body, cb);
	      if (dump_file)
		fprintf (dump_file, "List-schedule (ims) candidate: bb %d "
			 "region at uid=%d modeled row II %d (place-II %d "
			 "MII %d)\n",
			 bb->index, INSN_UID (r.nodes[0].insn), ims_ii,
			 ims.place_ii, ims.mii);
	      if (ims_ii < cand_ii)
		{
		  order = ims.order;
		  cand_ii = ims_ii;
		  used_ims = true;
		}
	    }
	}
      if (!region_webs.empty ())
	{
	  /* The committed renames themselves are a candidate: the
	     region's CURRENT order over the renamed webs (a pure
	     rename commit when it wins), judged by the identical
	     whole-row acceptance.  */
	  int ident_ii = ls_cyclic_ii (body, body_order);
	  if (ident_ii < cand_ii)
	    {
	      order.resize (n);
	      for (unsigned k = 0; k != n; ++k)
		order[k] = (int) k;
	      cand_ii = ident_ii;
	      used_ims = false;
	    }
	}
      if (order.empty ())
	{
	  undo_region_renames ();
	  continue;	/* every candidate already refused by name */
	}
      if (cand_ii >= cur_ii)
	{
	  undo_region_renames ();
	  if (riscv_tt_opt_cyclic_region_schedule)
	    rvtt_refuse (RVTT_REF_CYCLIC_INTERIOR_NO_II_DECREASE, dump_file,
			 "List-schedule (cyclic-interior) refused: "
			 "cyclic-interior-no-ii-decrease at uid=%d in bb %d "
			 "(%d -> %d)\n",
			 INSN_UID (r.nodes[0].insn), bb->index, cur_ii,
			 cand_ii);
	  else
	    rvtt_refuse (RVTT_REF_IMS_NO_II_DECREASE, dump_file,
			 "List-schedule (ims) refused: ims-no-ii-decrease "
			 "at uid=%d in bb %d (%d -> %d)\n",
			 INSN_UID (r.nodes[0].insn), bb->index, cur_ii,
			 cand_ii);
	  continue;
	}

      /* Commit guards: the nop inserter's pad-site probe (the WH
	 correctness carrier) and the entry producer's pad state, as
	 in the straight-line scheduler.  */
      unsigned pads_before = ls_pad_sites (visited, bb, r.nodes);
      bool ep_dynamic
	= r.entry_producer
	  && get_attr_xtt_delay (r.entry_producer) == XTT_DELAY_DYNAMIC;
      bool ep_needed_before
	= ep_dynamic
	  && delay_nop_needed_p (visited, bb, r.entry_producer,
				 XTT_DELAY_DYNAMIC);

      /* Exact-restore record, debug insns included.  */
      std::vector<rtx_insn *> chain;
      for (rtx_insn *w = NEXT_INSN (r.anchor);; w = NEXT_INSN (w))
	{
	  if (INSN_P (w))
	    chain.push_back (w);
	  if (w == r.nodes[n - 1].insn)
	    break;
	}

      rtx_insn *after = r.anchor;
      for (unsigned k = 0; k != n; ++k)
	{
	  rtx_insn *insn = r.nodes[order[k]].insn;
	  if (PREV_INSN (insn) != after)
	    reorder_insns (insn, insn, after);
	  after = insn;
	}

      unsigned pads_after = ls_pad_sites (visited, bb, r.nodes);
      bool ep_flipped
	= ep_dynamic && !ep_needed_before
	  && delay_nop_needed_p (visited, bb, r.entry_producer,
				 XTT_DELAY_DYNAMIC);
      if (pads_after > pads_before || ep_flipped)
	{
	  after = r.anchor;
	  for (rtx_insn *insn : chain)
	    {
	      if (PREV_INSN (insn) != after)
		reorder_insns (insn, insn, after);
	      after = insn;
	    }
	  undo_region_renames ();
	  if (dump_file)
	    fprintf (dump_file, "List-schedule (cyclic-interior) refused: "
		     "%s, restored bb %d region at uid=%d\n",
		     ep_flipped ? "entry-producer pad flip"
				: "pad-site increase",
		     bb->index, INSN_UID (r.nodes[0].insn));
	  continue;
	}

      if (dump_file)
	{
	  if (!region_webs.empty ())
	    fprintf (dump_file, "List-schedule (interior-rename): committed "
		     "%zu chain rename(s) in bb %d region at uid=%d\n",
		     region_webs.size (), bb->index,
		     INSN_UID (r.nodes[0].insn));
	  fprintf (dump_file, "List-schedule (%s): bb %d "
		   "region at uid=%d nodes=%u row II %d -> %d "
		   "target=%s\n",
		   used_ims ? "ims-interior" : "cyclic-interior",
		   bb->index, INSN_UID (r.nodes[0].insn), n, cur_ii,
		   cand_ii, TARGET_XTT_TENSIX_WH ? "wh" : "bh");
	  for (unsigned k = 0; k != n; ++k)
	    fprintf (dump_file, "List-schedule slot-order=%u uid=%d\n",
		     k, INSN_UID (r.nodes[order[k]].insn));
	}
      for (unsigned k = 0; k != n; ++k)
	body_order[first + k] = (int) (first + order[k]);
      cur_ii = cand_ii;
    }
}

/* Region-scheduler driver over FN.  Refuses targets without audited
   latency facts.  Per block: collects candidate regions of admissible
   nodes between named barriers (an explicit replay owner ends the
   block's eligibility); a self-loop block dispatches to the cyclic
   paths -- the one-region wrapped-row model when the row is a single
   region with no seam hazards, else the cyclic-interior path -- under
   their flags, deferring by name otherwise; in straight-line blocks,
   exactly two isomorphic region copies schedule as a pair under one
   permutation, larger families defer to replay formation, and single
   regions list-schedule under the list-schedule flag.  */

void
list_schedule_regions (function *fn)
{
  if (!TARGET_XTT_TENSIX_BH && !TARGET_XTT_TENSIX_WH)
    {
      if (dump_file)
	fprintf (dump_file, "List-schedule refused: no audited latency "
		 "facts for this target\n");
      return;
    }

  df_analyze ();

  std::vector<basic_block> visited;
  visited.reserve (n_basic_blocks_for_fn (fn));

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      /* A self-loop row executes back-to-back across the backedge (and,
	 captured, across every playback): the row is a cycle, and this
	 scheduler's linear boundary model mispredicts the seam.  The
	 cyclic adjacency is capture rotation's audited territory.  */
      bool self_loop = false;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	if (e->dest == bb)
	  self_loop = true;
      if (self_loop && !riscv_tt_opt_round_interleave
	  && !riscv_tt_opt_cyclic_region_schedule
	  && !riscv_tt_opt_ims)
	{
	  if (dump_file)
	    fprintf (dump_file, "List-schedule deferred: cyclic row "
		     "adjacency in bb %d (capture rotation owns the "
		     "backedge seam)\n", bb->index);
	  continue;
	}

      /* Phase 1: collect the block's candidate regions.  */
      std::vector<ls_region> regions;
      std::vector<ls_node> nodes;
      rtx_insn *anchor = nullptr;
      rtx_insn *entry_producer = nullptr;
      HARD_REG_SET region_unaudited;
      CLEAR_HARD_REG_SET (region_unaudited);
      bool stop_block = false;
      unsigned tensix_barriers = 0;	/* issued Tensix words outside
					   any region (seam hazards for
					   the cyclic extension) */
      bool bb_has_call = false;

      auto flush = [&] ()
      {
	/* Interleaving needs a third participant: a two-node region is
	   either order-forced (dependent) or model-symmetric under the
	   interior objective, so regions below three nodes are skipped
	   by name rather than scheduled.  */
	if (nodes.size () == 2 && dump_file)
	  rvtt_refuse (RVTT_REF_TWO_NODE, dump_file,
		       "List-schedule skipped: two-node region at "
		       "uid=%d in bb %d (below the interleave minimum)\n",
		       INSN_UID (nodes[0].insn), bb->index);
	if (nodes.size () >= 3)
	  {
	    ls_region r;
	    r.nodes = std::move (nodes);
	    r.anchor = anchor;
	    r.entry_producer = entry_producer;
	    r.unaudited_defs = region_unaudited;
	    for (const ls_node &nd : r.nodes)
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

	  ls_node node;
	  const char *why = nullptr;
	  if (ls_admissible_p (insn, &node, &why))
	    {
	      if (nodes.empty ())
		{
		  anchor = PREV_INSN (insn);
		  entry_producer = ls_entry_producer (bb, insn);
		  /* The audited entry horizon is exactly ONE issued
		     instruction deep: every admitted latency is <= 1,
		     so a producer two issue slots back has an expired
		     shadow.  An entry producer whose latency is
		     unaudited (or beyond the window) contributes its
		     defs as the pin hazard instead of a modeled floor.
		     Unknown-latency producers deeper than the entry
		     adjacency are unmodeled in baseline and candidate
		     alike -- the same exposure the fill phases carry
		     when a filler moves toward them.  Zero-length
		     interface markers are not producers: they deliver
		     no word and stage no hardware event.  */
		  CLEAR_HARD_REG_SET (region_unaudited);
		  if (entry_producer)
		    {
		      int ep_lat = audited_latency (entry_producer);
		      if (ep_lat < 0 || ep_lat > 1)
			{
			  insn_regs epr;
			  sfpu_reg_refs (entry_producer, &epr);
			  region_unaudited = epr.defs;
			}
		    }
		}
	      node.orig = (int) nodes.size ();
	      nodes.push_back (node);
	      continue;
	    }

	  /* Barrier.  */
	  if (GET_CODE (insn) == INSN
	      && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_length (insn))
	    {
	      ++tensix_barriers;
	      if (dump_file)
		fprintf (dump_file, "List-schedule barrier: %s uid=%d\n",
			 why, INSN_UID (insn));
	    }
	  else if (GET_CODE (insn) == INSN && PATTERN (insn)
		   && asm_noperands (PATTERN (insn)) >= 0)
	    /* Raw assembly may deliver Tensix words the effect
	       vocabulary cannot see: a seam hazard for the cyclic
	       extension (the straight-line phases already never move
	       anything across it).  */
	    ++tensix_barriers;
	  if (CALL_P (insn))
	    bb_has_call = true;
	  flush ();
	  if (GET_CODE (insn) == INSN && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX
	      && get_attr_xtt_replay (insn) == XTT_REPLAY_OWNER)
	    stop_block = true;	/* established capture discipline */
	}
      if (!stop_block)
	flush ();

      /* Round-interleave cyclic extension: a self-loop row whose
	 admitted nodes are its ONE region and whose block carries no
	 other issued/foreign Tensix word, no replay owner, and no call
	 schedules under the wrapped steady-state II model; every other
	 self-loop shape keeps the deferral, by name.  */
      if (self_loop)
	{
	  const char *why_c = nullptr;
	  if (stop_block)
	    why_c = "round-interleave-replay-owner-in-row";
	  else if (bb_has_call)
	    why_c = "round-interleave-call-in-row";
	  else if (tensix_barriers)
	    why_c = "round-interleave-seam-barrier-word";
	  else if (regions.size () != 1)
	    why_c = "round-interleave-row-not-one-region";
	  if (!why_c && (riscv_tt_opt_round_interleave || riscv_tt_opt_ims))
	    {
	      ls_schedule_region_cyclic (bb, regions[0].nodes,
					 regions[0].anchor, visited);
	      continue;
	    }
	  /* Cyclic-interior extension: the multi-region self-loop
	     shapes the one-region path refuses -- and, when the
	     round-interleave flag is off, every self-loop shape --
	     schedule INTERIOR regions under the whole-row cyclic II
	     acceptance.  The replay-owner and call refusals stand
	     (an owner's capture discipline and a call's foreign words
	     are outside the row model).  */
	  if ((riscv_tt_opt_cyclic_region_schedule || riscv_tt_opt_ims)
	      && !stop_block && !bb_has_call)
	    {
	      ls_schedule_cyclic_interior (bb, regions, visited);
	      continue;
	    }
	  if (dump_file)
	    fprintf (dump_file, "List-schedule deferred: cyclic row "
		     "adjacency in bb %d (%s)\n", bb->index,
		     why_c ? why_c : "round-interleave-flag-off");
	  continue;
	}

      /* Phase 2: repeated region shapes defer by name -- unrolled row
	 copies must stay textually isomorphic for the replay former's
	 re-roll and the MOP re-roll, and boundary-context differences
	 would schedule sibling copies differently.  Under the
	 round-interleave flag, EXACTLY TWO isomorphic copies schedule
	 as a pair under one shared permutation (isomorphism preserved;
	 see ls_schedule_iso_pair); larger families keep the deferral.  */
      std::vector<bool> pair_done (regions.size (), false);
      for (unsigned i = 0; i != regions.size (); ++i)
	{
	  if (pair_done[i])
	    continue;
	  unsigned nmatch = 0;
	  unsigned mate = 0;
	  for (unsigned j = 0; j != regions.size (); ++j)
	    if (j != i && regions[j].signature == regions[i].signature)
	      {
		if (!nmatch)
		  mate = j;
		++nmatch;
	      }
	  if (nmatch)
	    {
	      if (riscv_tt_opt_round_interleave && nmatch == 1
		  && mate > i)
		{
		  pair_done[mate] = true;
		  ls_schedule_iso_pair (bb, regions[i], regions[mate],
					visited);
		  continue;
		}
	      rvtt_refuse (RVTT_REF_REPEATED_ROW, dump_file,
			   "List-schedule deferred: repeated-row "
			   "shape at uid=%d in bb %d (replay capture "
			   "formation owns row isomorphism)\n",
			   INSN_UID (regions[i].nodes[0].insn), bb->index);
	      continue;
	    }
	  if (!riscv_tt_opt_list_schedule)
	    continue;	/* round-interleave alone owns no single region */
	  ls_schedule_region (bb, regions[i].nodes, regions[i].anchor,
			      regions[i].entry_producer,
			      next_issued_insn
				(bb, regions[i].nodes.back ().insn),
			      regions[i].unaudited_defs, visited);
	}
    }
}
