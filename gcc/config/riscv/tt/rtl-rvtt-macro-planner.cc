/* Generic SFPLOADMACRO macro planner.
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

/* SFPLOADMACRO lets a short instruction sequence be described once, in
   configuration state, and then re-launched by a single instruction.  The
   win is delivery: a RISC-pushed operation costs roughly 1.23x a replayed
   slot on the audited model, so collapsing a repeated row into a macro
   removes words from the instruction stream even when it removes no work.

   The predecessor pass recognised a fixed calendar of known sequences.  That
   is exactly the shape this backend is forbidden to use (see README section
   1): it cannot generalise, and it silently does nothing on a body that is
   one instruction away from a pattern it knows.  This pass instead DERIVES a
   descriptor from the region's own dataflow, proves the derivation, and
   schedules the result against a resource reservation table.  The old pass
   was deleted only after byte-parity oracles minted from it confirmed the
   generic path reproduced its output.

   The work is layered, and the layers are separate files:

     ownership   (rvtt-macro-ownership)  prove the region owns the
                 configuration state it is about to program -- function-global
                 first, then refined to the loop-body region
     region      (rvtt-macro-region)     find candidate regions and their rows
     desc        (rvtt-macro-desc)       synthesise the descriptor program
     sched       (rvtt-macro-sched)      place rows against unit/slot capacity
     epoch       (rvtt-macro-epoch)      configuration epochs across tiles
     verify      (rvtt-macro-verify)     re-derive and compare before emitting

   Every layer may decline, and a decline is a registered refusal name rather
   than a silent fallthrough.  Verification failure is a refusal too: if the
   re-derived descriptor does not match what was planned, the region is not
   formed.

   Gated by TARGET_XTT_TENSIX and the -mtt-tensix-macro-planner family.

   LINEAGE.
     technique  B. R. Rau and C. D. Glaeser, "Some scheduling
                techniques and an easily schedulable horizontal
                architecture for high performance scientific
                computing", MICRO-14, 1981, pp. 183-198.
                Placing operations against a RESERVATION TABLE of
                per-unit, per-slot occupancy.  That is what the sched
                layer (rvtt-macro-sched) does with the macro
                sub-unit calendar: occupancy modulo the interval,
                delay ranges, port and hazard bounds.
     technique  B. R. Rau, "Iterative modulo scheduling: an
                algorithm for software pipelining loops", MICRO-27,
                1994, pp. 63-74.
                The backtracking half: when a maximal proposal
                cannot be proven, deterministically unplace and retry
                within a bounded budget instead of refusing the whole
                region (-mtt-tensix-macro-ims).  The repair driver
                below cites this work by eponym alone; this is its
                attribution.
                What is NOT taken: no loop is pipelined and no
                interval is minimized across iterations.  The
                "interval" here is slots per ROW inside one
                descriptor program, the search is refusal-biased
                (exhausting the budget leaves the region unformed,
                never admits an unproven variant), and a repair may
                only recover regions the established search already
                refused.
     admission  A. Pnueli, M. Siegel and E. Singerman, "Translation
                validation", Tools and Algorithms for the
                Construction and Analysis of Systems (TACAS), 1998,
                pp. 151-166.
                Layer 7 does not trust the planner: the descriptor is
                RE-DERIVED from the region and compared with what was
                planned, and a mismatch refuses formation rather than
                reporting a diagnostic.  That is per-instance
                validation of one translation instead of verification
                of the translator -- the discipline that also let the
                pattern-calendar predecessor be deleted against
                byte-parity oracles minted from it.
     modelled on  none.  GCC describes a FIXED machine's issue
                hazards (gcc/genautomata.cc and the DFA it builds);
                nothing in GCC derives a programmable instruction
                template from a region's own dataflow, and nothing
                in GCC proves that a region owns the configuration
                state such a template lives in.

   HARDWARE.  SFPLOADMACRO: a short sequence described once in
   configuration state -- the descriptor program -- and re-launched
   by a single instruction.  A RISC-pushed operation costs roughly
   1.23x a replayed slot on the audited model, so collapsing a
   repeated row into a macro removes words from the instruction
   stream even when it removes no work.  What is spent is
   configuration state that OUTLIVES the region, which is why
   ownership (function-global first, then refined to the loop body)
   is Layer 1 and not an afterthought; rows are then placed against
   the sub-unit calendar's slot capacity, and the values they carry
   occupy the 8 LREGs.
     - SFPLOADMACRO SequenceBits, per-sub-unit adjacency rule, Misc
       field layout        [SPEC] SFPLOADMACRO.md; [SIM] the
                           reference simulator's dispatch builder
                           and its 4-slot select calendar
     - per-target capability tables (QSR intentionally absent)
                           rvtt-macro-tables-bh.def /
                           rvtt-macro-tables-wh.def
     - push-vs-replayed-slot ratio and the delivery prices
                           rvtt-cost.md

   BIRTH KERNEL.  where/reduce-sdpa.  Ledger: FIRE-BREADTH.tsv flag
   macro-planner, birth_share 0.53 -- about half the measured
   benefit lies off the birth row, so the derived descriptor is
   doing real work beyond the row it was built on, which is the
   whole point of having deleted the pattern-calendar predecessor.
   The testsuite agrees on the family (macro-planner-where-form,
   -where-default, -where-refuse, plus macro-planner-cast-round-emit
   and the ims/ambient rows).  The satellite flags differ:
   macro-planner-residency is "where impl-1 (laneBN)" at 1.00
   (birth-row-bound) and macro-planner-replay is "where impl
   (planner)" with no share recorded ("-").
   */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "rtl.h"
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "cfghooks.h"
#include "df.h"
#include "rtl-iter.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rtl-rvtt-macro-planner-int.h"

using namespace rvtt_planner;
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-raw-boundary.h"

/* The macro planner replaces every exact-calendar SFPLOADMACRO
   recognizer with regions, schedules, and descriptors derived from typed
   effects, dataflow proofs, and capability tables.  This pass is the
   planner's spine; at this stage it is analysis-only: under
   -mtt-tensix-macro-planner-analyze it reports discovered regions and
   named refusals to its dump and never mutates the function.  It runs
   after IRA/reload (hard LREGs final) and before the quarantined
   exact-calendar pass, the hazard scheduler, and replay formation.  */

namespace {

static bool
form_region (function *fn, macro_region &region,
	     const macro_schedule &schedule, const macro_descriptor &desc,
	     macro_residency_state *resid, bool sole_region, FILE *dump)
{
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c || desc.refusal || desc.drain_slots < 0
      || desc.launches.is_empty ())
    return false;

  /* Configuration ownership: the function-global proof, or, for a
     loop-body region, the region-scoped preheader+body window (the
     preheader is computed quietly here; when the scoped proof also
     fails the refusal keeps its established name, and when only the
     scoped path can prove -- the real-kernel loop shape -- the window
     dump line names the sharing).  */
  basic_block scoped_preheader = nullptr;
  if (!planner_config_ownership_ok (fn, c))
    {
      /* WP9: proven CC-template programs fall back to the region-scoped
	 ownership proof (see planner_region_config_ownership_ok); every
	 other shape keeps the conservative function-global gate.  */
      bool scoped_ok = false;
      if (desc.cc.active)
	{
	  basic_block scoped_preheader = region.loop_body
	    ? loop_region_preheader (fn, region, nullptr) : nullptr;
	  if (!region.loop_body || scoped_preheader)
	    {
	      rtx_insn *anchor = region.rows[0].enable
		? region.rows[0].enable : region.rows[0].insns[0];
	      scoped_ok = planner_region_config_ownership_ok
		(region, scoped_preheader, anchor, c);
	    }
	}
      if (!scoped_ok)
	{
	  rvtt_refuse (RVTT_REF_CONFIG_OWNERSHIP_UNPROVEN, dump,
		       "Macro-planner formation-refusal:"
		       " config-ownership-unproven\n");
	  return false;
	}
    }

  /* Every planner-owned physical LREG must be dead after the region.  */
  rtx_insn *region_end = region.rows.last ().separator
    ? region.rows.last ().separator : region.rows.last ().insns.last ();
  for (unsigned reg = 0; reg != 17; ++reg)
    if ((desc.planned_lregs >> reg) & 1)
      if (!planned_value_dead_after_p
	    (gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + reg), region_end))
	{
	  rvtt_refuse (RVTT_REF_PLANNED_LREG_LIVE, dump,
		       "Macro-planner formation-refusal:"
		       " planned-lreg-live\n");
	  return false;
	}

  auto_vec<unsigned> run_begins;
  for (unsigned r = 0; r != region.rows.length (); ++r)
    if (region.rows[r].starts_run)
      run_begins.safe_push (r);
  if (run_begins.is_empty ())
    run_begins.safe_push (0);

  /* A loop-body region hoists its configuration to the structural
     preheader; that placement carries the zero-trip and whole-body
     ownership obligations.  */
  basic_block config_preheader = nullptr;
  gcov_type body_count = 1, preheader_count = 1;
  if (region.loop_body)
    {
      config_preheader = scoped_preheader
	? scoped_preheader : loop_region_preheader (fn, region, dump);
      if (!config_preheader)
	return false;
      if (!loop_trip_weight (region.bb, config_preheader, &body_count,
			     &preheader_count))
	{
	  /* No usable trip estimate: conservatively a single trip.  */
	  body_count = preheader_count = 1;
	}
    }

  /* Every ambient enable the formation consumes -- each row's local
     enable (all are deleted and one is re-emitted in the prefix) and
     the loop preheader's trailing enable -- must provably write the
     all-lanes state: the proven store/misc envelope covers no partial
     lane mask, and the deleted quarantined pass refused exactly here.
     Region discovery only admits proven enables; this re-check keeps
     the formation contract locally auditable and guards every future
     discovery widening.  */
  for (const macro_row &row : region.rows)
    if (row.enable && !cc_enable_all_lanes_proved_p (row.enable))
      {
	rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
		     "Macro-planner formation-refusal:"
		     " cc-enable-unproved\n");
	return false;
      }

  /* A lane-predicated calendar needs the ambient all-lanes proof: the
     region's first row's local enable (relaxed from every-row:
     region members cannot write CC -- such rows refuse
     cc-template-unsupported at discovery -- so the entry lane state
     holds across every row), or, for a loop-body region whose enable
     was written once outside the loop, the proven preheader's trailing
     enable (whole-body ownership keeps it live across every trip).  */
  rtx_insn *enable_src = region.rows[0].enable;
  bool materialized_enable = false;
  bool ambient_enable = false;
  if (desc.needs_all_lanes_prefix && !region.rows[0].enable)
    {
      rtx_insn *trailing = config_preheader
	? preheader_trailing_enable (config_preheader) : nullptr;
      if (trailing)
	{
	  if (!cc_enable_all_lanes_proved_p (trailing))
	    {
	      /* A trailing pure CC write exists but its written lane
		 state is not provably the all-lanes pattern (lanes-off,
		 partial mask, complement, ...): name the unproved
		 enable.  */
	      rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
			   "Macro-planner formation-refusal:"
			   " cc-enable-unproved\n");
	      return false;
	    }
	  enable_src = nullptr;	/* already in place; no copy */
	}
      else
	{
	  /* Materialized enable (superseding the first-row
	     peel): when no typed ambient enable exists -- the real LLK
	     kernels establish the lane state through opaque init the
	     typed IR cannot see -- a CC-template row's OWN all-lanes
	     restore is the proof source, and the formation MATERIALIZES
	     that proven word (a pattern copy, all-lanes word-exact
	     through the P0 sfpencc derivation) at the head of the
	     configuration prefix instead of executing the whole first
	     row explicitly.  The license is the compiler's own
	     established outermost-CC-depth contract: the row's
	     SETCC/.../ENCC combine is produced by rvtt_cc's
	     outermost-depth transform (gimple-rvtt-cc.cc), which
	     already replaces the outermost POPC (restore the pushed
	     state) with ENCC (enable all lanes) -- sound exactly
	     because the architectural kernel convention pins the
	     outermost lane state to all-lanes.  The materialized word
	     re-writes the state that contract already guarantees, so
	     the first row -- like every later row, inductively through
	     the launched restore template -- executes under the
	     all-lanes entry state.  Rows without an in-row proven
	     restore keep the named refusal.  */
	  rtx_insn *proof_restore = nullptr;
	  if (desc.cc.active && region.rows.length () > 1)
	    for (rtx_insn *member : region.rows[0].insns)
	      {
		xtt_effect_set e = rvtt_insn_effects (member);
		if (e.cc_write && !e.cc_read && !e.lreg_read
		    && !e.lreg_write && e.cc_write_all_lanes)
		  proof_restore = member;
	      }
	  if (proof_restore)
	    {
	      enable_src = proof_restore;
	      materialized_enable = true;
	    }
	  /* Entry-ambient derivation (owner-ratified honesty
	     fix, 2026-08-29): when no typed enable exists anywhere --
	     the source carries no marker instruction -- the compiler
	     derives the lane state itself.  The license is the SAME
	     architectural contract every established consumer already
	     stands on (rvtt_cc's outermost POPC -> ENCC rewrite, the
	     enable materialization above, crossrow-pairing's loop-entry
	     walk, prgm-const's pre-peel walk): the structured-CC
	     lowering pins the function-entry and outermost lane state
	     to the architectural all-lanes state.  The derivation is a
	     kill-aware backwards CFG walk from the configuration
	     placement point: every path must reach the function entry
	     or a word-exact all-lanes SFPENCC before any other
	     CC-affecting, unaudited, or opaque instruction.  Raw asm is
	     seen THROUGH, never trusted:
	     decoded `.ttinsn' words classify against the audited
	     lane-enable table and the rest leans on the TU-wide
	     CC/lane-enable audit; calls and anything undecodable stay
	     DIRTY fail-closed (entry_ambient_all_lanes_p header
	     comment).  On success the formation
	     SYNTHESIZES the canonical all-lanes enable
	     (rvtt_sfpencc_all_lanes, the capability-table word, the
	     same word the crosscall init hoist already synthesizes
	     caller-side) at the head of the configuration prefix:
	     re-writing the state the derivation just proved -- a
	     machine-state no-op, priced as the one pushed word it
	     is.  An unproven walk keeps the named refusal.  */
	  else if (entry_ambient_all_lanes_p
		     (config_preheader
		      ? config_preheader
		      : BLOCK_FOR_INSN (region.rows[0].insns[0]),
		      config_preheader
		      ? nullptr : region.rows[0].insns[0], dump))
	    {
	      start_sequence ();
	      emit_insn (gen_rvtt_sfpencc_all_lanes ());
	      rtx_insn *synth = get_insns ();
	      end_sequence ();
	      gcc_assert (synth && !NEXT_INSN (synth)
			  && cc_enable_all_lanes_proved_p (synth));
	      enable_src = synth;
	      materialized_enable = true;
	      ambient_enable = true;
	      if (dump)
		fprintf (dump, "Macro-planner formation: entry-ambient"
			 " all-lanes derived (structured-CC fn-entry"
			 " contract); canonical enable synthesized\n");
	    }
	  else
	    {
	      rvtt_refuse (RVTT_REF_ALL_LANES_PROOF_MISSING, dump,
			   "Macro-planner formation-refusal:"
			   " all-lanes-proof-missing"
			   " (ambient-entry-unproven)\n");
	      return false;
	    }
	}
    }

  /* Residency de-duplication (rvtt-macro-desc.cc): when a
     bit-identical descriptor program is already resident at a proven
     dominating placement under function-wide owned-state invariance,
     this region elides its descriptor-word programming entirely.
     Proof-only; computed here so the init-hoist pricing
     pre-run below sees the same eligibility the commit site keeps.
     Refusals keep today's emission byte-identically.  */
  bool resident_elide
    = rvtt_macro_residency_lookup (fn, region, desc, c, resid, dump);

  /* Init-hoist pricing pre-run: the crosscall
     init hoist's proof chain, evaluated PROOF-ONLY ahead of the
     profitability gate under exactly the guards the committing site
     (below, still last among the refusal points) applies.  A proven
     stage-2 contract means the configuration prefix will leave the
     callee entirely -- the init contract words price ONCE per proven
     caller-loop entry, not per run (rvtt-cost.md caller-loop prefix
     amortization).  Nothing is inserted here; the commit only happens
     after every later refusal point has passed, and a refusal
     anywhere keeps caller and callee byte-identical.  */
  int init_hoist_stage = 0;
  rvtt_init_hoist_program init_prog = {};
  const char *init_refusal = nullptr;
  rtx_insn *init_refusal_insn = nullptr;
  if (riscv_tt_opt_init_hoist && !region.loop_body && !resident_elide)
    {
      if (!sole_region)
	init_refusal = "drain-init-callee-unproven";
      else if (!enable_src || (materialized_enable && !ambient_enable)
	       || !cc_enable_all_lanes_proved_p (enable_src)
	       || recog_memoized (enable_src) != CODE_FOR_rvtt_sfpencc)
	/* v1: the prefix's lane proof must be the typed proven
	   all-lanes SFPENCC (the minmax-class ambient enable); the
	   in-row-restore materialization license is not carried
	   cross-call.  The entry-ambient SYNTHESIZED enable (F1 honest
	   fix) is admitted: it is the same canonical word the hoist's
	   caller-side emission synthesizes itself
	   (rvtt_sfpencc_all_lanes), licensed by the derived fn-entry
	   ambient fact rather than a row-local restore.  */
	init_refusal = "drain-init-idempotence-unproven";
      else if (desc.n_setc16 > 8
	       || desc.n_templates + desc.n_seq + (desc.has_misc ? 1 : 0)
		  > 16)
	init_refusal = "drain-init-idempotence-unproven";
      else
	init_refusal = init_hoist_callee_scan (fn, region,
					       &init_refusal_insn);
      if (!init_refusal)
	{
	  init_prog.n_setc16 = desc.n_setc16;
	  for (unsigned i = 0; i != desc.n_setc16; ++i)
	    {
	      init_prog.setc16[i].reg = desc.setc16[i].config_reg;
	      init_prog.setc16[i].value = desc.setc16[i].value;
	    }
	  init_prog.n_words = 0;
	  for (unsigned t = 0; t != desc.n_templates; ++t)
	    {
	      init_prog.words[init_prog.n_words].word = desc.templ[t];
	      init_prog.words[init_prog.n_words++].dest = t;
	    }
	  for (unsigned m = 0; m != desc.n_seq; ++m)
	    {
	      init_prog.words[init_prog.n_words].word = desc.seq[m];
	      init_prog.words[init_prog.n_words++].dest = 4 + m;
	    }
	  if (desc.has_misc)
	    {
	      init_prog.words[init_prog.n_words].word = desc.misc;
	      init_prog.words[init_prog.n_words++].dest = 8;
	    }
	  init_refusal = rvtt_crosscall_init_hoist (fn, &init_prog,
						    /*commit=*/false);
	  if (!init_refusal)
	    {
	      init_hoist_stage = init_prog.stage;
	      if (dump)
		fprintf (dump, "Macro-planner init-hoist pricing"
			 " pre-run: stage=%d proven (weight=%lld/%lld"
			 " usable=%d)\n", init_hoist_stage,
			 (long long) init_prog.caller_body_count,
			 (long long) init_prog.caller_entry_count,
			 (int) init_prog.caller_weight_ok);
	    }
	}
      if (init_refusal && dump)
	{
	  rvtt_refuse_by_name (init_refusal, dump,
			       "Macro-planner init-hoist-refusal: %s",
			       init_refusal);
	  if (init_refusal_insn)
	    fprintf (dump, " (insn %d)", INSN_UID (init_refusal_insn));
	  fprintf (dump, "\n");
	}
    }
  /* The pricing sees the amortization only under the full (stage-2)
     contract with a usable profile weight; anything less keeps the
     frozen conservative-per-run discipline.  */
  int ih_pricing_stage
    = init_hoist_stage >= 2 && init_prog.caller_weight_ok
      ? init_hoist_stage : 0;

  /* Profitability.  Straight-line: every run independently amortizes
     the full configuration prefix (the frozen conservative-per-run
     discipline), except under the proven full init-hoist contract,
     where the prefix amortizes over the caller loop (above).  Loop
     body: the prefix is paid once in the preheader and weighted
     against the profile trip estimate.  */
  if (region.loop_body)
    {
      if (!loop_profitable_p (region, schedule, desc, body_count,
			      preheader_count, run_begins.length ()))
	{
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " unprofitable (trip-weight=%ld/%ld)\n",
		     (long) body_count, (long) preheader_count);
	  return false;
	}
      if (!ims_arbitrate_loop (region, schedule, desc, body_count,
			       preheader_count, run_begins.length (), dump))
	{
	  rvtt_refuse (RVTT_REF_REPLAY_DELIVERY_PREFERRED, dump,
		       "Macro-planner formation-refusal:"
		       " replay-delivery-preferred\n");
	  return false;
	}
    }
  else
    for (unsigned b = 0; b != run_begins.length (); ++b)
      {
	unsigned begin = run_begins[b];
	unsigned end = b + 1 == run_begins.length ()
	  ? region.rows.length () : run_begins[b + 1];
	if (!run_profitable_p (region, schedule, desc, end - begin,
			       ih_pricing_stage,
			       init_prog.caller_entry_count,
			       init_prog.caller_body_count, dump))
	  {
	    if (dump)
	      fprintf (dump, "Macro-planner formation-refusal:"
		       " unprofitable\n");
	    return false;
	  }
	if (!ims_arbitrate_run (region, schedule, desc, end - begin,
				ih_pricing_stage,
				init_prog.caller_entry_count,
				init_prog.caller_body_count, dump))
	  {
	    rvtt_refuse (RVTT_REF_REPLAY_DELIVERY_PREFERRED, dump,
			 "Macro-planner formation-refusal:"
			 " replay-delivery-preferred\n");
	    return false;
	  }
      }

  /* Emission deletes each row's typed Dst separator; that is only
     sound when the launch calendar absorbed the stride, or when the
     proven program keeps the separator in place (the CC-template
     programs re-emit it verbatim as the restore-visibility slot).  */
  for (const macro_row &row : region.rows)
    if (row.separator && !schedule.absorbed_stride && !desc.keep_separator)
      {
	rvtt_refuse (RVTT_REF_STRIDE_NOT_ABSORBED, dump,
		     "Macro-planner formation-refusal:"
		     " stride-not-absorbed\n");
	return false;
      }

  /* Immediate-delta regions (honesty fix): only the
     absorbed calendar expresses them (the schedule already refuses
     imm-stride-not-absorbed; this re-check keeps the contract locally
     auditable), the kept-separator program never carries them, and
     every Dst access of every offset row must be address-rewritable
     back to rows[0]'s base -- proven here as a dry run BEFORE any
     mutation.  */
  if (region.imm_stride)
    {
      if (schedule.absorbed_stride != region.imm_stride
	  || desc.keep_separator)
	{
	  rvtt_refuse (RVTT_REF_IMM_STRIDE_NOT_ABSORBED, dump,
		       "Macro-planner formation-refusal:"
		       " imm-stride-not-absorbed\n");
	  return false;
	}
      for (const macro_row &row : region.rows)
	{
	  if (!row.imm_delta)
	    continue;
	  for (rtx_insn *insn : row.insns)
	    {
	      xtt_effect_set e = rvtt_insn_effects (insn);
	      if (!e.dst_mem_read && !e.dst_mem_write)
		continue;
	      rtx address, mode, addr_mode;
	      if (!rvtt_dst_access_operands (insn, e, &address, &mode,
					     &addr_mode)
		  || !CONST_INT_P (address)
		  || !planner_rewrite_dst_address (insn, PATTERN (insn),
						   INTVAL (address)))
		{
		  rvtt_refuse (RVTT_REF_IMM_STRIDE_REWRITE_UNPROVEN, dump,
			       "Macro-planner formation-refusal:"
			       " imm-stride-rewrite-unproven\n");
		  return false;
		}
	    }
	}
    }

  /* Compact CC calendar: the absorbing explicit load's address
     mode operand must be rewritable (the one admitted load pattern);
     proven here as a dry run -- refusal paths never mutate.  */
  if (schedule.absorb_into_explicit)
    for (unsigned ix = 0; ix != region.rows[0].insns.length (); ++ix)
      if (schedule.events[ix].absorbs_stride
	  && !planner_rewrite_load_addr_mode
	       (region.rows[0].insns[ix],
		PATTERN (region.rows[0].insns[ix]),
		c->auto_increment_dst2_addr_mode))
	{
	  rvtt_refuse (RVTT_REF_STRIDE_NOT_ABSORBED, dump,
		       "Macro-planner formation-refusal:"
		       " stride-not-absorbed\n");
	  return false;
	}

  /* Cross-tile prefix elision for formed CC calendars.  When the
     configuration preheader itself sits inside an enclosing issue loop
     (the tile loop) and the configuration-epoch proof shows no
     intervening owner of the planner's SFPCONFIG destinations across
     that loop, the descriptor words are hoisted to the enclosing
     loop's structural preheader and elided from every later trip; the
     ambient enable and the owned SETC16 program stay per trip.  The
     hoisted block needs its own copyable proven all-lanes enable (the
     lane-predicated LREG materialization must not run masked), so a
     region relying purely on an in-place trailing enable copies that
     proven word.  Every refusal keeps today's per-trip prefix
     byte-identically, under a stable name.  */
  /* Residency de-duplication: RESIDENT_ELIDE was computed above
     (proof-only ahead of profitability, decision-identical --
     the lookup is proof-only and profitability reads no residency
     state); a resident program makes the cross-tile hoist moot.  */
  basic_block hoist_preheader = nullptr;
  edge hoist_edge = nullptr;
  rtx_insn *hoist_enable_src = nullptr;
  if (!resident_elide && desc.cc.active && config_preheader)
    {
      hoist_enable_src = enable_src
	? enable_src : preheader_trailing_enable (config_preheader);
      if (hoist_enable_src
	  && !cc_enable_all_lanes_proved_p (hoist_enable_src))
	hoist_enable_src = nullptr;
      if (hoist_enable_src)
	{
	  const char *epoch_refusal = nullptr;
	  rtx_insn *epoch_refusal_insn = nullptr;
	  if (rvtt_macro_prefix_epoch_hoist (fn, region, config_preheader,
					     c, &hoist_preheader,
					     &hoist_edge, &epoch_refusal,
					     &epoch_refusal_insn))
	    {
	      /* Residency outward extension (rvtt-macro-desc.cc):
		 proof-only iteration of the epoch discipline through
		 further enclosing loops; on any refusal the placement
		 stays the cross-tile hoist's, byte-identically.  */
	      unsigned resid_levels = 0;
	      rvtt_macro_residency_extend (fn, region, desc, c, resid,
					   &hoist_preheader, &hoist_edge,
					   &resid_levels, dump);

	      /* Commit-time edge split (guarded enclosing loop): every
		 proof has passed, so this is no longer a refusal path.
		 The split block executes exactly when the loop is
		 entered.  */
	      if (!hoist_preheader)
		hoist_preheader = split_edge (hoist_edge);
	      if (dump)
		{
		  unsigned words = 0;
		  for (unsigned t = 0; t != desc.n_templates; ++t)
		    words += config_word_loadi_issues (desc.templ[t]) + 1;
		  for (unsigned m = 0; m != desc.n_seq; ++m)
		    words += config_word_loadi_issues (desc.seq[m]) + 1;
		  if (desc.has_misc)
		    words += config_word_loadi_issues (desc.misc) + 1;
		  fprintf (dump, "Macro-planner prefix-epoch: cross-tile"
			   " config invariance proven (owned SFPCONFIG"
			   " dests epoch-clean across the enclosing loop;"
			   " %u descriptor words hoisted to the outer"
			   " preheader; enable+setc16 retained per"
			   " trip)\n", words);
		}
	    }
	  else if (epoch_refusal && dump)
	    {
	      rvtt_refuse_by_name (epoch_refusal, dump,
				   "Macro-planner prefix-epoch-refusal: %s",
				   epoch_refusal);
	      if (epoch_refusal_insn)
		fprintf (dump, " (insn %d)", INSN_UID (epoch_refusal_insn));
	      fprintf (dump, "\n");
	    }
	}
    }

  /* Lane CA cross-call init hoist (D2): when this straight-line region
     is the function's whole macro content and its idempotent init
     prefix is call-invariant descriptor data, prove the (single)
     caller's loop epoch and move the prefix to the caller's loop
     preheader -- once per loop instead of once per call.  The proof
     chain ran PROOF-ONLY ahead of profitability (pricing
     pre-run); the COMMIT stays here, LAST among the refusal points (a
     committed caller-side insertion and the callee-side suppression
     stand together).  The committing call re-evaluates the identical
     deterministic proof chain; a divergence would mean the function
     changed between the two calls -- impossible on the refusal-free
     path -- and refuses fail-closed (pricing that assumed the hoist
     must never form without it).  Every unproven link keeps today's
     per-call prefix byte-identically.  */
  if (init_hoist_stage > 0
      && !hoist_preheader && !config_preheader && !resident_elide)
    {
      const char *commit_refusal
	= rvtt_crosscall_init_hoist (fn, &init_prog, /*commit=*/true);
      if (commit_refusal || init_prog.stage != init_hoist_stage)
	{
	  rvtt_refuse (RVTT_REF_INIT_HOIST_COMMIT_DIVERGED, dump,
		       "Macro-planner formation-refusal:"
		       " init-hoist-commit-diverged (%s)\n",
		       commit_refusal ? commit_refusal : "stage-mismatch");
	  return false;
	}
      if (dump)
	fprintf (dump, "Macro-planner init-hoist: stage=%d"
		 " init contract hoisted to caller loop preheader"
		 " (%u descriptor words, %u setc16, enable %s)\n",
		 init_hoist_stage, init_prog.n_words, init_prog.n_setc16,
		 init_hoist_stage >= 2 ? "hoisted" : "retained");
    }
  else
    init_hoist_stage = 0;

  /* Drain-aware boundary placement (default-off; proofs and derivation
     in rtl-rvtt-schedule.cc): decide every intra-region boundary BEFORE any
     mutation.  The final run's drain -- the region's exit contract (no
     events in flight may reach the invisible follower stream) -- is
     never elided.  */
  /* Inter-row drain (a wrong-code adjudication).
     Within a run, rows are emitted back-to-back.  The conservative VD
     policy's own stated rule (rvtt-macro-sched.cc: a hosted launched
     event consumes the launch VD; without a proven consumption slot
     before the next row's launch, consecutive rows must alternate VDs)
     makes that sound ONLY under VD alternation.  When descriptor
     synthesis pins a VALUE carrier's VD (name-encoded consumers;
     the frozen whole-word programs' fixed_vd), every row re-targets the
     SAME register while the previous row's hosted consumers still pend
     up to drain_slots past its launch -- back-to-back rows race the
     next launch's VD write against the pending events.  Adjudicated on
     the replay-loop-unroll signbit shape (device correctness failure,
     reproduced by the reference simulator; the simulator trace
     shows three launches' events in flight on one LReg and the shift
     event consuming overwritten data).  Placement: the FULL derived
     drain between consecutive rows, reproducing exactly the proven
     rolled per-row calendar (launch + drain), whose issue-stream
     spacing is the proven envelope.  The established alternating
     envelope, store-only sacrificial VDs (written, never read), and
     the CC-template model (its macro_cc_model next-row obligations --
     store-before-next-def, restore-visibility <= row interval -- are
     the proven inter-row contract, hardware-proven multi-row on the
     unified where kernel) keep today's bytes.  */
  bool interrow_drain = false;
  if (desc.drain_slots > 0 && region.rows.length () > 1 && !desc.cc.active)
    for (const macro_launch_spec &l : desc.launches)
      if (!l.vd_alternates && !l.is_store_only)
	interrow_drain = true;
  if (interrow_drain && dump)
    fprintf (dump, "Macro-planner drain-interrow: drain=%d rows=%u"
	     " (fixed-vd value carrier)\n", desc.drain_slots,
	     region.rows.length ());
  /* Lane FT window-pairing: replace the shape rule's full inter-row
     drain with the minimal spacing the exact pending-event model proves
     (rvtt_macro_interrow_drain_tuned, rtl-rvtt-schedule.cc).  Refusals
     keep the lane-EV placement byte-identically.  */
  int interrow_drain_slots = interrow_drain ? desc.drain_slots : 0;
  if (interrow_drain && riscv_tt_opt_window_pairing)
    {
      const char *bound = nullptr;
      int tuned = rvtt_macro_interrow_drain_tuned (fn, region, schedule,
						   desc, dump, &bound);
      gcc_assert (tuned >= 0 && tuned <= desc.drain_slots);
      if (tuned < interrow_drain_slots && dump)
	fprintf (dump, "Macro-planner window-pairing: interrow-drain"
		 " %d -> %d rows=%u%s%s\n", desc.drain_slots, tuned,
		 region.rows.length (), bound ? " bound=" : "",
		 bound ? bound : "");
      interrow_drain_slots = tuned;
    }

  auto_vec<bool> drain_elide;
  drain_elide.safe_grow_cleared (run_begins.length ());
  unsigned drains_elided = 0;
  if (riscv_tt_opt_drain_schedule && desc.drain_slots > 0)
    for (unsigned b = 0; b + 1 < run_begins.length (); ++b)
      {
	unsigned rend = run_begins[b + 1];
	unsigned rnext_end = b + 2 < run_begins.length ()
	  ? run_begins[b + 2] : region.rows.length ();
	drain_elide[b] = rvtt_macro_drain_boundary_elidable
	  (region, schedule, desc, run_begins[b], rend, rnext_end, dump);
	if (drain_elide[b])
	  ++drains_elided;
      }

  /* Loop-backedge drain elision: a loop-body region's FINAL
     run ends at the loop latch, so its drain executes once per trip
     where the architecture requires it once per loop exit.  When the
     backedge follower stream proves (rvtt_macro_drain_backedge_elidable,
     rtl-rvtt-schedule.cc) AND a sound exit placement exists (the sole
     non-self successor, entered only from this loop), the in-body drain
     is elided and the FULL derived drain is emitted at the exit block's
     head instead -- the exit contract is preserved, only its placement
     moves.  Any unprovable piece keeps today's in-body drain
     byte-identically, under a stable name.  */
  bool backedge_elide = false;
  edge drain_exit_edge = nullptr;
  if (riscv_tt_opt_drain_schedule && desc.drain_slots > 0
      && region.loop_body && region.bb)
    {
      edge exit_e = nullptr;
      bool shape_ok = EDGE_COUNT (region.bb->succs) == 2;
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, region.bb->succs)
	if (e->dest != region.bb)
	  {
	    if (exit_e)
	      shape_ok = false;
	    exit_e = e;
	  }
      if (!shape_ok || !exit_e
	  || (exit_e->flags & (EDGE_ABNORMAL | EDGE_EH | EDGE_COMPLEX)))
	{
	  rvtt_refuse (RVTT_REF_DRAIN_EXIT_SHARED, dump,
		       "Macro-planner drain-refusal:"
		       " drain-exit-shared\n");
	}
      else
	{
	  unsigned first_run_end = run_begins.length () > 1
	    ? run_begins[1] : region.rows.length ();
	  backedge_elide = rvtt_macro_drain_backedge_elidable
	    (region, schedule, desc, first_run_end, dump);
	  if (backedge_elide)
	    drain_exit_edge = exit_e;
	}
    }

  basic_block config_placement = nullptr;
  for (unsigned b = 0; b != run_begins.length (); ++b)
    {
      unsigned begin = run_begins[b];
      unsigned end = b + 1 == run_begins.length ()
	? region.rows.length () : run_begins[b + 1];
      bool last_run = b + 1 == run_begins.length ();
      emit_planner_run (region, schedule, desc, c, begin, end, b == 0,
			config_preheader, enable_src,
			hoist_preheader, hoist_enable_src,
			last_run ? !backedge_elide : !drain_elide[b],
			interrow_drain_slots,
			resident_elide, resid, init_hoist_stage,
			b == 0 ? &config_placement : nullptr);
    }
  if (backedge_elide)
    {
      /* Exit compensation: the full derived drain on the loop's exit
	 path -- events from the final trip never reach the invisible
	 follower stream.  Placement: the exit destination's head when
	 this loop is its only predecessor, else a commit-time edge
	 split (every proof has passed; the split block executes
	 exactly when the loop exits) -- the same discipline as the
	 cross-tile hoist's guarded-enclosing-loop split.  */
      basic_block dest = drain_exit_edge->dest;
      if (dest == EXIT_BLOCK_PTR_FOR_FN (fn) || !single_pred_p (dest)
	  || !bb_note (dest))
	dest = split_edge (drain_exit_edge);
      start_sequence ();
      for (int d = 0; d != desc.drain_slots; ++d)
	emit_insn (gen_rvtt_sfpnop ());
      rtx_insn *nops = get_insns ();
      end_sequence ();
      if (resid)
	for (rtx_insn *i = nops; i; i = NEXT_INSN (i))
	  resid->emitted.add (i);
      emit_insn_after (nops, bb_note (dest));
      if (dump)
	fprintf (dump, "Macro-planner drain-backedge: loop-carried drain"
		 " elided; exit compensation %d SFPNOPs (bb %d)\n",
		 desc.drain_slots, dest->index);
    }
  if (!resident_elide)
    rvtt_macro_residency_record (desc, config_placement, resid);
  if (dump)
    fprintf (dump, "Macro-planner formed: rows=%u runs=%u%s%s%s%s%s%s%s\n",
	     region.rows.length (), run_begins.length (),
	     config_preheader ? " config=preheader" : "",
	     materialized_enable ? " lane-proof=materialized-enable" : "",
	     hoist_preheader ? " prefix-epoch=hoisted" : "",
	     drains_elided ? " drain-elided" : "",
	     backedge_elide ? " drain-backedge" : "",
	     resident_elide ? " resident=elided" : "",
	     init_hoist_stage == 2 ? " init-hoist=full"
	     : init_hoist_stage == 1 ? " init-hoist=descriptor" : "");
  return true;
}

/* Drive one region through the established candidate search exactly as
   the pass spine always has: schedule candidates ascend, the first whose
   descriptor synthesis (and Layer-7 verification) proves is committed
   through form_region.  Returns true when a candidate PROVED (the search
   stops there whether or not formation committed); *CHANGED accumulates
   actual code mutation.  Shared verbatim by the spine and the
   upward-carrier commit path so both can never diverge.  */

static bool
planner_process_region (function *fn, macro_region &region,
			bool sole_region,
			macro_residency_state *resid, FILE *dump,
			bool *changed)
{
  for (unsigned candidate = 0; ; ++candidate)
    {
      macro_schedule schedule;
      if (!rvtt_macro_schedule_region (region, &schedule, dump, candidate))
	return false;		/* search exhausted (or no table)  */
      bool proven = false;
      macro_descriptor descriptor;
      if (rvtt_macro_synthesize (region, schedule, &descriptor, dump))
	{
	  /* A Layer-7 verification mismatch is a descriptor refusal: the
	     candidate is unproven and must never reach form_region.  */
	  const char *verify_fail = nullptr;
	  if (riscv_tt_macro_planner_verify || flag_checking)
	    verify_fail = rvtt_macro_verify_descriptor (region, schedule,
							descriptor, dump);
	  proven = !descriptor.refusal && !verify_fail;
	  if (proven && riscv_tt_macro_planner)
	    *changed |= form_region (fn, region, schedule, descriptor,
				     resid, sole_region, dump);
	  rvtt_macro_descriptor_release (&descriptor);
	}
      rvtt_macro_schedule_release (&schedule);
      if (proven)
	return true;
    }
}

/* ---------------- Upward-IMS carrier former ------------------------- */
/* The upward half of the IMS mapping (-mtt-tensix-macro-ims-carrier,
   default off).  The repair driver searches DOWNWARD -- reduced
   hosted sets -- and provably conserves the initiation interval on rows
   whose maximal hosting already proves (docs/MACRO_PLANNER.md 2d).  The
   upward former searches the other direction, the handwritten kernels'
   re-load idiom: duplicate one of the row's Dst loads into a provably
   free LREG (a fresh value carrier), replicate the load's in-place
   cooking prefix onto it, and version-split-rename one explicit consumer
   web onto the new carrier so its events can host there.  Everything is
   applied as a REAL commit-or-revert mutation of every unrolled row
   copy: the mutated region re-runs the full established pipeline --
   discovery, scheduling (including IMS repair when enabled), descriptor
   synthesis, Layer-7 verification, and every formation gate -- which
   remains the only feasibility oracle.  A variant commits only when it
   re-proves at a STRICTLY smaller initiation interval than the
   established outcome; every other path reverts byte-identically under a
   stable refusal name.  Nothing here names an operation, matches an
   opcode calendar, or assembles a raw word: seeds, prefixes, and webs
   are typed-effect dataflow classes, and the duplication legality rides
   the effect vocabulary's own proofs (a region-admitted load is
   RWC-inert by the no-increment address-mode derivation).  */

/* Enumeration budget, NOT a cost-model constant: bounds the variant
   SEARCH per region (refusal-biased -- exhausting it leaves candidates
   unformed, never admits an unproven one).  No rvtt-cost.md derivation
   exists (FH audit FHP-5); widening or deriving it is the planner
   lane's follow-up.  */
static const unsigned UPWARD_CARRIER_BUDGET = 24; /* variants per region */

/* Stable refusal vocabulary (append-only dump API).  */
static const char *upward_refusal_legality
  = "ims-carrier-legality-unproven";
static const char *upward_refusal_lreg
  = "ims-carrier-lreg-unavailable";
static const char *upward_refusal_web
  = "ims-carrier-web-unsplittable";
static const char *upward_refusal_row_divergent
  = "ims-carrier-row-divergent";
static const char *upward_refusal_rederive
  = "ims-carrier-rederive-unproven";
static const char *upward_refusal_no_improvement
  = "ims-carrier-no-improvement";

/* Commit-or-revert journal.  Renamed insns keep their ORIGINAL pattern
   rtx (the mutation installs a copy), so revert restores the exact
   pre-mutation objects.  */

struct upward_journal
{
  auto_vec<rtx_insn *> inserted;
  auto_vec<rtx_insn *> renamed_insns;
  auto_vec<rtx> renamed_old_pats;
  auto_vec<int> renamed_old_codes;

  void revert ()
  {
    for (unsigned i = inserted.length (); i-- > 0;)
      delete_insn (inserted[i]);
    for (unsigned i = renamed_insns.length (); i-- > 0;)
      {
	PATTERN (renamed_insns[i]) = renamed_old_pats[i];
	INSN_CODE (renamed_insns[i]) = renamed_old_codes[i];
	df_insn_rescan (renamed_insns[i]);
      }
    drop ();
  }

  void drop ()
  {
    inserted.truncate (0);
    renamed_insns.truncate (0);
    renamed_old_pats.truncate (0);
    renamed_old_codes.truncate (0);
  }
};

/* Replace, in place, every hard-LREG occurrence in *LOC whose lane
   register number is in LREG_MASK by NEWREG.  Per-insn replacement is
   whole-register: within one instruction either every occurrence of a
   register renames (an in-place chain member: the post-RA tie holds
   because both sides move together) or the register only appears as
   reads (a stop-through consumer) -- the rename computation below only
   emits masks with that property.  */

static void
upward_replace_lregs (rtx *loc, uint32_t lreg_mask, rtx newreg)
{
  subrtx_ptr_iterator::array_type array;
  FOR_EACH_SUBRTX_PTR (iter, array, loc, NONCONST)
    {
      rtx *p = *iter;
      rtx x = *p;
      if (x && REG_P (x) && REGNO (x) >= SFPU_REG_FIRST
	  && REGNO (x) - SFPU_REG_FIRST < 32
	  && ((lreg_mask >> (REGNO (x) - SFPU_REG_FIRST)) & 1))
	*p = newreg;
    }
}

/* The carrier register choice.  The launch word's VDLo field encodes
   VD 0..3 only (VDHi is punned with the address LSB -- the capability
   tables' sacrificial-VD rule), so the carrier register must come from
   L0..L3.  When every low register is taken, a row-internal low WEB may
   be RELOCATED to a free high register first (the version-split-rename
   half of the pre-registered design): a pure whole-web physical rename
   is value-inert, and re-derivation re-proves every encoding that
   embedded the old name.  Highest-first choices keep away from L0,
   which additionally collides with the cast class's VC:=VD encoding
   (the derivation core's audited fact).  */

struct upward_carrier_choice
{
  int vd;			/* carrier register (0..3)	       */
  int relocate_to;		/* free high register, or -1	       */
};

/* Choose the carrier register for REGION per the rule above.  Prefer
   the highest free register in L0..L3 (free: unreferenced anywhere in
   the region's block and live neither in nor out); failing that, pick a
   low register whose block usage is entirely region-row-internal and
   pair it in OUT->RELOCATE_TO with a free high register its web will be
   relocated to.  Returns false when neither exists.  */

static bool
upward_pick_carrier_reg (const macro_region &region,
			 upward_carrier_choice *out)
{
  basic_block bb = region.bb;
  uint32_t used = 0;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    if (NONDEBUG_INSN_P (insn))
      {
	subrtx_iterator::array_type array;
	FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	  {
	    const_rtx x = *iter;
	    if (x && REG_P (x) && REGNO (x) >= SFPU_REG_FIRST
		&& REGNO (x) - SFPU_REG_FIRST < 32)
	      used |= 1u << (REGNO (x) - SFPU_REG_FIRST);
	  }
      }
  auto free_p = [&] (unsigned r) -> bool
    {
      return !((used >> r) & 1)
	&& !bitmap_bit_p (df_get_live_in (bb), SFPU_REG_FIRST + r)
	&& !bitmap_bit_p (df_get_live_out (bb), SFPU_REG_FIRST + r);
    };
  out->relocate_to = -1;
  for (unsigned r = 4; r-- > 0;)
    if (free_p (r))
      {
	out->vd = (int) r;
	return true;
      }
  /* Relocation: a free high register and a low register whose block
     usage is entirely this region's row members (row-internal web:
     defined and consumed inside the rows, never live across the block
     or referenced by foreign instructions).  */
  int high = -1;
  for (unsigned r = 8; r-- > 4;)
    if (free_p (r))
      {
	high = (int) r;
	break;
      }
  if (high < 0)
    return false;
  for (unsigned r = 4; r-- > 0;)
    {
      if (!((region.internal_lregs >> r) & 1)
	  || bitmap_bit_p (df_get_live_in (bb), SFPU_REG_FIRST + r)
	  || bitmap_bit_p (df_get_live_out (bb), SFPU_REG_FIRST + r))
	continue;
      bool foreign = false;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn) || foreign)
	    continue;
	  bool refs = false;
	  subrtx_iterator::array_type array;
	  FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	    {
	      const_rtx x = *iter;
	      if (x && REG_P (x)
		  && REGNO (x) == SFPU_REG_FIRST + r)
		refs = true;
	    }
	  if (!refs)
	    continue;
	  bool member = false;
	  for (const macro_row &row : region.rows)
	    for (rtx_insn *m : row.insns)
	      member |= m == insn;
	  foreign |= !member;
	}
      if (!foreign)
	{
	  out->vd = (int) r;
	  out->relocate_to = high;
	  return true;
	}
    }
  return false;
}

/* One enumerated variant: re-load the SEED load, replicate its in-place
   cooking prefix, and version-split the TARGET event's web onto the new
   carrier.  Indices are row0 positions; per-row application recomputes
   the same structure from each row's own registers (isomorphism makes
   the index sets agree; any divergence refuses).  */

struct upward_variant
{
  unsigned seed_ix;
  /* The version-split target CHAIN, program order: the first member
     reads the seed's cooked value; each later member reads the previous
     member's result.  Every member's fresh single-register definition
     web renames onto the new carrier -- the hand kernels' chained
     re-load idiom (a one-member chain is the plain split).  */
  unsigned targets[4];
  unsigned n_targets;
  /* Carrier placement: the re-load sits directly after the seed load,
     or -- when the cooking prefix is empty, so the copy has no in-row
     value dependences -- at the row head (an earlier launch slot gives
     the hosted chain earlier execution windows against its explicit
     consumers' deadlines).  Both positions read the same Dst word
     under the same counter state (the row's members are RWC-inert by
     admission).  */
  bool reload_at_head;
};

/* Derive the per-insn rename masks and the prefix-clone index set of one
   ROW for VARIANT.  Pure analysis (never mutates).  On success MASKS[i]
   holds the lane-register numbers to rewrite onto the new carrier in
   row insn i, and PREFIX holds the in-place cooking events to clone (in
   program order).  Refusals set *REFUSAL to a stable name.

   Soundness of the split, at the value level:
   - the seed is a region-admitted Dst load, so its address-mode operand
     is the derived no-increment slot (rvtt_insn_effects maps any other
     mode to an UNKNOWN RWC effect, which discovery refuses): executing
     the copy is architecturally inert beyond writing the new register;
   - the copy is inserted directly after the seed with the cooking
     clones following, and no member between the seed and a cloned
     event's original defines the clone's other sources, so every clone
     computes exactly the seed web's value into the new register;
   - the chain's first member reads the seed's cooked value (the new
     register holds that exact value after the clones) and each later
     member reads the previous member's result; every member writes a
     fresh single register, so renaming the member definitions onto the
     new carrier is a linear version split: the new register holds each
     chain value in turn, every use reached by a renamed definition is
     renamed with it, and a use of an earlier version positioned at or
     after the next version's definition refuses (the versions are only
     linear when their live ranges are).  After the chain tail the
     established single-web propagation continues: a tied in-place
     follower moves with the register (the post-RA tie holds because
     every occurrence renames together), a fresh redefinition ends the
     renamed range.  */

static bool
upward_compute_renames (const macro_row &row, const upward_variant &v,
			auto_vec<uint32_t> *masks,
			auto_vec<unsigned> *prefix, const char **refusal)
{
  unsigned n = row.insns.length ();
  masks->truncate (0);
  masks->safe_grow_cleared (n);
  prefix->truncate (0);

  if (v.n_targets == 0 || v.n_targets > 4)
    {
      *refusal = upward_refusal_legality;
      return false;
    }

  /* Seed legality: a plain (no live-value merge) Dst load writing one
     physical L0..L7 register.  The RWC-inert property is already the
     admission condition (see above).  */
  xtt_effect_set se = rvtt_insn_effects (row.insns[v.seed_ix]);
  if (se.opaque || !se.dst_mem_read
      || se.rwc.kind != xtt_rwc_effect_t::NONE)
    {
      *refusal = upward_refusal_legality;
      return false;
    }
  if (recog_memoized (row.insns[v.seed_ix]) != CODE_FOR_rvtt_sfpload_lv_int)
    {
      *refusal = upward_refusal_legality;
      return false;
    }
  extract_insn (row.insns[v.seed_ix]);
  if (recog_data.n_operands < 9
      || !noval_operand (recog_data.operand[6],
			 GET_MODE (recog_data.operand[6])))
    {
      /* A live-value merging load reads its destination's prior value;
	 duplicating it is not value-inert.  */
      *refusal = upward_refusal_legality;
      return false;
    }
  uint32_t dmask = se.lreg_write;
  if (!dmask || (dmask & (dmask - 1)) != 0 || (unsigned) ctz_hwi (dmask) > 7)
    {
      *refusal = upward_refusal_legality;
      return false;
    }

  /* Chain legality: ascending positions after the seed; the first
     member reads the seed register; each later member reads the
     previous member's result; every member is a non-memory value event
     writing one fresh (not self-read) register.  */
  uint32_t chain_w[4];
  for (unsigned k = 0; k != v.n_targets; ++k)
    {
      unsigned tix = v.targets[k];
      if (tix >= n || tix <= v.seed_ix
	  || (k && tix <= v.targets[k - 1]))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      xtt_effect_set te = rvtt_insn_effects (row.insns[tix]);
      uint32_t need = k == 0 ? dmask : chain_w[k - 1];
      if (te.opaque || te.dst_mem_read || te.dst_mem_write
	  || !(te.lreg_read & need))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* Two admitted member forms: a FRESH single-register definition
	 (version split), or an IN-PLACE continuation (the member reads
	 and writes the incoming version register -- the launch-VD chain
	 idiom itself); anything else is outside the split vocabulary.  */
      uint32_t w = te.lreg_write;
      bool in_place = w == need && (te.lreg_read & w) != 0;
      if (!w || (w & (w - 1)) != 0 || (!in_place && (te.lreg_read & w)))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* A later chain member must not read the SEED register: the new
	 carrier register no longer holds that value at its position.
	 (Reading it by its own name stays untouched and correct; only
	 a rename would be wrong, so nothing to rename means nothing to
	 refuse -- the mask below simply never adds dmask for k > 0.)  */
      chain_w[k] = w;
    }

  /* Cooking prefix: every writer of the seed register between the seed
     and the FIRST chain member must be an in-place event (reads and
     writes exactly that register); each is cloned onto the new
     carrier.  A fresh redefinition means the chain head does not
     consume the seed load's value at all.  */
  for (unsigned ix = v.seed_ix + 1; ix < v.targets[0]; ++ix)
    {
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.opaque)
	continue;
      if (e.lreg_write & dmask)
	{
	  if (e.lreg_write != dmask || !(e.lreg_read & dmask)
	      || e.dst_mem_read || e.dst_mem_write)
	    {
	      *refusal = upward_refusal_legality;
	      return false;
	    }
	  /* The clone sits directly after the re-load; its other
	     sources must still carry their original reaching values
	     there: no member between the seed and this event may
	     define them.  */
	  uint32_t other = e.lreg_read & ~dmask;
	  for (unsigned jx = v.seed_ix + 1; jx < ix; ++jx)
	    if (rvtt_insn_effects (row.insns[jx]).lreg_write & other)
	      {
		*refusal = upward_refusal_web;
		return false;
	      }
	  prefix->safe_push (ix);
	}
    }

  /* Chain member renames: the head's reads of the seed register plus
     its definition; every later member's read of the previous version
     plus its definition.  */
  (*masks)[v.targets[0]] = dmask | chain_w[0];
  for (unsigned k = 1; k != v.n_targets; ++k)
    (*masks)[v.targets[k]] = chain_w[k - 1] | chain_w[k];

  /* Version linearity between chain members: a use of version k
     between its definition and the next chain member reads the new
     register (renamed); a use at or after the next member's definition
     would read a later version and refuses; an interleaved foreign
     redefinition of the version register likewise refuses.  */
  for (unsigned k = 0; k + 1 < v.n_targets; ++k)
    {
      uint32_t w = chain_w[k];
      unsigned from = v.targets[k] + 1;
      unsigned upto = v.targets[k + 1];
      for (unsigned ix = from; ix < upto; ++ix)
	{
	  xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	  if (e.opaque)
	    {
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_write & w)
	    {
	      /* A redefinition of the version register before the next
		 chain member: the split is not linear here.  */
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_read & w)
	    (*masks)[ix] |= w;
	}
      /* Uses of a non-tail version after the next member's definition
	 read a later version: refuse.  (Reads of the OLD register name
	 past this window belong to other, unrenamed definitions only
	 when a fresh redefinition intervenes; without one such a read
	 consumed our renamed value and refuses.)  An in-place next
	 member carries the same register forward -- later reads bind to
	 the renamed continuation and its own windows judge them.  */
      if (chain_w[k + 1] == w)
	continue;
      for (unsigned ix = upto + 1; ix < n; ++ix)
	{
	  xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
	  if (e.opaque)
	    continue;
	  if ((e.lreg_write & w) && !(e.lreg_read & w))
	    break;		/* fresh redefinition: later reads foreign */
	  if (e.lreg_read & w)
	    {
	      *refusal = upward_refusal_web;
	      return false;
	    }
	}
    }

  /* Established single-web propagation past the chain tail.  */
  uint32_t wmask = chain_w[v.n_targets - 1];
  bool active = true;
  for (unsigned ix = v.targets[v.n_targets - 1] + 1; ix < n && active; ++ix)
    {
      xtt_effect_set e = rvtt_insn_effects (row.insns[ix]);
      if (e.opaque)
	{
	  /* An opaque member's register accesses are unknown; a live
	     renamed value across it cannot be proven to move.  */
	  *refusal = upward_refusal_web;
	  return false;
	}
      bool reads = (e.lreg_read & wmask) != 0;
      bool writes = (e.lreg_write & wmask) != 0;
      if (reads)
	{
	  (*masks)[ix] |= wmask;
	  if (writes && e.lreg_write != wmask)
	    {
	      /* Writes the renamed register AND another: outside the
		 single-result web vocabulary this split can prove.  */
	      *refusal = upward_refusal_web;
	      return false;
	    }
	}
      else if (writes)
	/* Fresh redefinition: later uses read it, unrenamed.  */
	active = false;
    }
  return true;
}

/* Apply VARIANT to every row of REGION (journal-recorded).  NEWREG_LREG
   is the free carrier register.  Returns false -- after reverting any
   partial application -- with *REFUSAL named when a row diverges from
   the row0 structure.  */

static bool
upward_apply (macro_region &region, const upward_variant &v,
	      const upward_carrier_choice &regs,
	      const auto_vec<uint32_t> &masks0,
	      const auto_vec<unsigned> &prefix0, upward_journal *journal,
	      const char **refusal)
{
  int newreg_lreg = regs.vd;
  rtx newreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + newreg_lreg);
  /* Web relocation first: the freed low register becomes the carrier.
     A pure whole-web physical rename of a proven row-internal web; the
     re-derivation re-proves every encoding embedding the name.  */
  if (regs.relocate_to >= 0)
    {
      rtx high = gen_rtx_REG (XTT32SImode,
			      SFPU_REG_FIRST + regs.relocate_to);
      for (macro_row &row : region.rows)
	for (rtx_insn *insn : row.insns)
	  {
	    bool refs = false;
	    subrtx_iterator::array_type array;
	    FOR_EACH_SUBRTX (iter, array, PATTERN (insn), NONCONST)
	      {
		const_rtx x = *iter;
		if (x && REG_P (x)
		    && REGNO (x) == SFPU_REG_FIRST + (unsigned) regs.vd)
		  refs = true;
	      }
	    if (!refs)
	      continue;
	    rtx old_pat = PATTERN (insn);
	    int old_code = INSN_CODE (insn);
	    rtx new_pat = copy_rtx (old_pat);
	    upward_replace_lregs (&new_pat, 1u << regs.vd, high);
	    PATTERN (insn) = new_pat;
	    INSN_CODE (insn) = -1;
	    journal->renamed_insns.safe_push (insn);
	    journal->renamed_old_pats.safe_push (old_pat);
	    journal->renamed_old_codes.safe_push (old_code);
	    if (recog_memoized (insn) < 0)
	      {
		*refusal = upward_refusal_web;
		journal->revert ();
		return false;
	      }
	    df_insn_rescan (insn);
	  }
    }
  for (macro_row &row : region.rows)
    {
      auto_vec<uint32_t> masks;
      auto_vec<unsigned> prefix;
      if (row.insns.length () != masks0.length ()
	  || !upward_compute_renames (row, v, &masks, &prefix, refusal))
	{
	  if (*refusal == nullptr)
	    *refusal = upward_refusal_row_divergent;
	  journal->revert ();
	  return false;
	}
      /* The per-row structure must agree with row0's: same clone set
	 and same rename positions (isomorphism should force this; any
	 divergence refuses rather than trusts).  */
      bool agrees = prefix.length () == prefix0.length ();
      for (unsigned i = 0; agrees && i != prefix.length (); ++i)
	agrees = prefix[i] == prefix0[i];
      for (unsigned i = 0; agrees && i != masks.length (); ++i)
	agrees = (masks[i] != 0) == (masks0[i] != 0);
      if (!agrees)
	{
	  *refusal = upward_refusal_row_divergent;
	  journal->revert ();
	  return false;
	}

      /* Renames first (patterns swap to mutated copies).  */
      for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	{
	  if (!masks[ix])
	    continue;
	  rtx_insn *insn = row.insns[ix];
	  rtx old_pat = PATTERN (insn);
	  int old_code = INSN_CODE (insn);
	  rtx new_pat = copy_rtx (old_pat);
	  upward_replace_lregs (&new_pat, masks[ix], newreg);
	  PATTERN (insn) = new_pat;
	  INSN_CODE (insn) = -1;
	  journal->renamed_insns.safe_push (insn);
	  journal->renamed_old_pats.safe_push (old_pat);
	  journal->renamed_old_codes.safe_push (old_code);
	  if (recog_memoized (insn) < 0)
	    {
	      *refusal = upward_refusal_web;
	      journal->revert ();
	      return false;
	    }
	  df_insn_rescan (insn);
	}

      /* The re-load and the cooking clones, directly after the seed
	 (or the re-load alone at the row head for a prefix-free
	 variant).  */
      uint32_t dmask
	= rvtt_insn_effects (row.insns[v.seed_ix]).lreg_write;
      rtx reload_pat = copy_rtx (PATTERN (row.insns[v.seed_ix]));
      upward_replace_lregs (&reload_pat, dmask, newreg);
      rtx_insn *reload;
      if (v.reload_at_head && prefix.is_empty ())
	reload = emit_insn_before (reload_pat, row.insns[0]);
      else
	reload = emit_insn_after (reload_pat, row.insns[v.seed_ix]);
      journal->inserted.safe_push (reload);
      rtx_insn *pos = reload;
      bool clones_ok = recog_memoized (reload) >= 0;
      if (clones_ok)
	df_insn_rescan (reload);
      for (unsigned i = 0; clones_ok && i != prefix.length (); ++i)
	{
	  rtx clone_pat = copy_rtx (PATTERN (row.insns[prefix[i]]));
	  upward_replace_lregs (&clone_pat, dmask, newreg);
	  rtx_insn *clone = emit_insn_after (clone_pat, pos);
	  journal->inserted.safe_push (clone);
	  pos = clone;
	  clones_ok = recog_memoized (clone) >= 0;
	  if (clones_ok)
	    df_insn_rescan (clone);
	}
      if (!clones_ok)
	{
	  *refusal = upward_refusal_legality;
	  journal->revert ();
	  return false;
	}
    }
  return true;
}

/* First proven candidate of REGION through the established pipeline
   (analysis only, no dumps, no mutation).  */

static bool
upward_probe_region (const macro_region &region, int *ii_out,
		     unsigned *candidate_out, FILE *dump = nullptr)
{
  for (unsigned candidate = 0; ; ++candidate)
    {
      macro_schedule schedule;
      if (!rvtt_macro_schedule_region (region, &schedule, dump,
				       candidate))
	return false;
      bool proven = false;
      macro_descriptor descriptor;
      if (rvtt_macro_synthesize (region, schedule, &descriptor, dump))
	{
	  const char *verify_fail = nullptr;
	  if (riscv_tt_macro_planner_verify || flag_checking)
	    verify_fail = rvtt_macro_verify_descriptor (region, schedule,
							descriptor, dump);
	  proven = !descriptor.refusal && !verify_fail;
	  rvtt_macro_descriptor_release (&descriptor);
	}
      int ii = schedule.ii;
      rvtt_macro_schedule_release (&schedule);
      if (proven)
	{
	  *ii_out = ii;
	  *candidate_out = candidate;
	  return true;
	}
    }
}

/* The upward-carrier driver: try variants on REGION; returns true
   when one committed (the mutated region formed).  On false the
   function is byte-identical to entry.  */

static bool
upward_carrier_try (function *fn, macro_region &region,
		    macro_residency_state *resid, FILE *dump, bool *changed)
{
  if (region.rows.is_empty ())
    return false;

  /* Predicate-definition rows keep the established candidate space:
     their hosting rules are the proven CC select programs' territory
     (the IMS-repair discipline).  */
  for (rtx_insn *insn : region.rows[0].insns)
    {
      xtt_effect_set e = rvtt_insn_effects (insn);
      if (!(e.dst_mem_read || e.dst_mem_write) && e.cc_write
	  && e.lreg_read != 0 && !e.lreg_write)
	return false;
    }

  /* The established outcome is the improvement baseline: the upward
     search only ever replaces a PROVEN formation by a strictly denser
     one.  When the established search proves NOTHING, the upward
     variants may still recover the region (the repair symmetry:
     the re-load can be exactly what makes a refusing hosted set
     realizable); the baseline is then no formation at all, and the
     established profitability and arbitration gates inside
     form_region price the variant against the explicit stream.  */
  int est_ii = INT_MAX;
  unsigned est_candidate = 0;
  bool est_proven = upward_probe_region (region, &est_ii, &est_candidate);

  upward_carrier_choice reg_choice;
  if (!upward_pick_carrier_reg (region, &reg_choice))
    {
      rvtt_refuse_by_name (upward_refusal_lreg, dump,
			   "Macro-planner upward-carrier-refusal: %s\n",
			   upward_refusal_lreg);
      return false;
    }
  int newreg = reg_choice.vd;

  /* Classify the established schedule's explicit value events (the
     hosting frontier the upward search can move).  For an unproven
     region the frontier comes from the first grouping proposal (the
     maximal-sharing candidate always exists for a discovered region).  */
  macro_schedule est;
  if (!rvtt_macro_schedule_region (region, &est, nullptr,
				   est_proven ? est_candidate : 0))
    return false;
  const macro_row &row0 = region.rows[0];
  auto_vec<upward_variant> variants;
  /* A hostable chain member: an explicit non-memory value event of a
     hostable sub-unit class writing one fresh single register.  */
  auto chain_member_p = [&] (unsigned ix, uint32_t need) -> bool
    {
      /* For a PROVEN baseline only explicit events are a frontier (a
	 hosted event already realizes); an unproven baseline's greedy
	 hosting is not a realization, so every value event is fair.  */
      if (est_proven
	  && est.events[ix].realization != macro_event::EXPLICIT_INSN)
	return false;
      if (!est_proven
	  && est.events[ix].realization == macro_event::CC_COALESCED)
	return false;
      xtt_effect_set te = rvtt_insn_effects (row0.insns[ix]);
      if (te.opaque || te.dst_mem_read || te.dst_mem_write
	  || !(te.lreg_read & need)
	  || (te.subunit != XTT_SU_SIMPLE && te.subunit != XTT_SU_ROUND
	      && te.subunit != XTT_SU_MAD))
	return false;
      uint32_t w = te.lreg_write;
      bool in_place = w == need && (te.lreg_read & w) != 0;
      return w && (w & (w - 1)) == 0
	&& (in_place || !(te.lreg_read & w));
    };
  for (unsigned seed_ix = 0; seed_ix != row0.insns.length (); ++seed_ix)
    {
      xtt_effect_set se = rvtt_insn_effects (row0.insns[seed_ix]);
      if (se.opaque || !se.dst_mem_read)
	continue;
      uint32_t dmask = se.lreg_write;
      if (!dmask || (dmask & (dmask - 1)) != 0)
	continue;
      for (unsigned tix = seed_ix + 1; tix != row0.insns.length (); ++tix)
	{
	  if (variants.length () >= UPWARD_CARRIER_BUDGET)
	    break;
	  if (!chain_member_p (tix, dmask))
	    continue;
	  /* Greedy deterministic chain: extend with the next explicit
	     event reading the current tail's result, as long as that
	     result has a UNIQUE reading successor position (linear
	     version ranges; upward_compute_renames re-proves).  */
	  upward_variant v;
	  v.seed_ix = seed_ix;
	  v.targets[0] = tix;
	  v.n_targets = 1;
	  v.reload_at_head = false;
	  while (v.n_targets < 4)
	    {
	      uint32_t w = rvtt_insn_effects
		(row0.insns[v.targets[v.n_targets - 1]]).lreg_write;
	      /* The tail version's live window ends at the next
		 definition of its register; the extension member is
		 that definition when it is an in-place continuation
		 (it reads the version), else the window's LAST reader
		 (earlier readers stay read-renamed uses of the same
		 version).  */
	      int next = -1;
	      for (unsigned jx = v.targets[v.n_targets - 1] + 1;
		   jx != row0.insns.length (); ++jx)
		{
		  xtt_effect_set je = rvtt_insn_effects (row0.insns[jx]);
		  if (je.opaque)
		    continue;
		  if (je.lreg_write & w)
		    {
		      if (je.lreg_read & w)
			next = (int) jx;   /* in-place continuation */
		      break;	/* any definition ends the window    */
		    }
		  if (je.lreg_read & w)
		    next = (int) jx;	   /* last reader so far      */
		}
	      if (next < 0 || !chain_member_p ((unsigned) next, w))
		break;
	      v.targets[v.n_targets++] = (unsigned) next;
	    }
	  /* Placement axis: a prefix-free variant (no cooking writer of
	     the seed register before the chain head) additionally tries
	     the row-head carrier slot FIRST -- the earlier launch slot
	     gives the hosted chain earlier execution windows against
	     its explicit consumers' deadlines.  */
	  bool prefix_free = true;
	  for (unsigned jx = seed_ix + 1; jx < tix; ++jx)
	    {
	      xtt_effect_set je = rvtt_insn_effects (row0.insns[jx]);
	      if (!je.opaque && (je.lreg_write & dmask))
		prefix_free = false;
	    }
	  /* The maximal chain first, then each shorter prefix (a deeper
	     rename can refuse where a shallower one proves).  */
	  for (unsigned len = v.n_targets; len > 0; --len)
	    {
	      if (variants.length () >= UPWARD_CARRIER_BUDGET)
		break;
	      upward_variant p = v;
	      p.n_targets = len;
	      if (prefix_free)
		{
		  p.reload_at_head = true;
		  variants.safe_push (p);
		  if (variants.length () >= UPWARD_CARRIER_BUDGET)
		    break;
		}
	      p.reload_at_head = false;
	      variants.safe_push (p);
	    }
	}
    }
  rvtt_macro_schedule_release (&est);

  for (const upward_variant &v : variants)
    {
      char chain_str[32];
      {
	int off = 0;
	for (unsigned k = 0; k != v.n_targets && off < 24; ++k)
	  off += snprintf (chain_str + off, sizeof (chain_str) - off,
			   "%s%u", k ? "," : "", v.targets[k]);
      }
      const char *refusal = nullptr;
      auto_vec<uint32_t> masks0;
      auto_vec<unsigned> prefix0;
      if (!upward_compute_renames (row0, v, &masks0, &prefix0, &refusal))
	{
	  rvtt_refuse_by_name (refusal, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n", refusal, v.seed_ix,
			       chain_str);
	  continue;
	}
      upward_journal journal;
      if (!upward_apply (region, v, reg_choice, masks0, prefix0, &journal,
			 &refusal))
	{
	  rvtt_refuse_by_name (refusal, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n", refusal, v.seed_ix,
			       chain_str);
	  continue;
	}

      /* Re-derive: the mutated block re-enters discovery; the variant's
	 region must reappear with the same row count (the whole
	 unrolled span re-proves or the variant refuses).  */
      auto_vec<macro_region> fresh;
      rvtt_macro_regions_discover (fn, nullptr, &fresh);
      macro_region *sel = nullptr;
      rtx_insn *row0_reload = journal.inserted[0];
      for (macro_region &fr : fresh)
	if (fr.bb == region.bb
	    && fr.rows.length () == region.rows.length () && !sel)
	  for (rtx_insn *i : fr.rows[0].insns)
	    if (i == row0_reload)
	      {
		sel = &fr;
		break;
	      }

      bool committed = false;
      int new_ii = 0;
      unsigned new_candidate = 0;
      /* The variant's re-derivation search is dumped in full (the
	 repair discipline): the probe's schedule and descriptor lines
	 are the reviewable record of why a variant proves or dies.  */
      if (dump)
	{
	  fprintf (dump, "Macro-planner upward-carrier: probing seed=%u"
		   " chain={%s} reload-vd=%d placement=%s", v.seed_ix,
		   chain_str, newreg, v.reload_at_head ? "head" : "after-seed");
	  if (reg_choice.relocate_to >= 0)
	    fprintf (dump, " web-relocated=L%d->L%d", reg_choice.vd,
		     reg_choice.relocate_to);
	  fprintf (dump, "\n");
	}
      if (!sel || !upward_probe_region (*sel, &new_ii, &new_candidate, dump))
	{
	  rvtt_refuse_by_name (upward_refusal_rederive, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s})\n",
			       upward_refusal_rederive, v.seed_ix, chain_str);
	}
      else if (new_ii >= est_ii)
	{
	  rvtt_refuse_by_name (upward_refusal_no_improvement, dump,
			       "Macro-planner upward-carrier-refusal: %s"
			       " (seed=%u chain={%s} est-ii=%d"
			       " variant-ii=%d)\n",
			       upward_refusal_no_improvement, v.seed_ix,
			       chain_str, est_ii, new_ii);
	}
      else
	{
	  if (dump)
	    fprintf (dump, "Macro-planner upward-carrier: seed=%u chain={%s}"
		     " reload-vd=%d prefix-clones=%u ii=%d->%d\n",
		     v.seed_ix, chain_str, newreg, prefix0.length (),
		     est_ii, new_ii);
	  bool local_changed = false;
	  bool proven = planner_process_region (fn, *sel, resid, dump,
						&local_changed);
	  if (proven && local_changed)
	    {
	      *changed = true;
	      committed = true;
	      if (dump)
		fprintf (dump, "Macro-planner upward-carrier: formed"
			 " (ii=%d, was %d)\n", new_ii, est_ii);
	    }
	  else if (dump)
	    rvtt_refuse_by_name (upward_refusal_rederive, dump,
				 "Macro-planner upward-carrier-refusal: %s"
				 " (seed=%u chain={%s} formation declined)\n",
				 upward_refusal_rederive, v.seed_ix, chain_str);
	}

      for (macro_region &fr : fresh)
	rvtt_macro_region_release (&fr);
      if (committed)
	{
	  journal.drop ();	/* mutation is the committed code */
	  return true;
	}
      journal.revert ();
    }
  return false;
}

const pass_data pass_data_rvtt_macro_planner =
{
  RTL_PASS,
  "rvtt_macro_planner",
  OPTGROUP_OTHER,
  TV_NONE,
  0,
  0,
  0,
  0,
  0
};

class pass_rvtt_macro_planner : public rtl_opt_pass
{
public:
  pass_rvtt_macro_planner (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_macro_planner, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
      && (riscv_tt_macro_planner_analyze || riscv_tt_macro_planner);
  }

  unsigned execute (function *fn) final override
  {
    bool changed = false;
    /* Planner emission records are per-function: clear the previous
       function's launch effect records before any formation
       (rvtt-effects.h contract; lookups additionally verify the
       function identity, so this reset is belt-and-braces).  */
    rvtt_planner_launch_effects_reset ();
    /* Descriptor residency: per-function store of programmed descriptor
       content and planner-emitted insns (rvtt-macro-desc.cc).  Regions
       are processed in discovery order = forward program order, the
       increment-1 first-formed-wins selection policy.  */
    macro_residency_state resid;
    auto_vec<macro_region> regions;
    rvtt_macro_regions_discover (fn, dump_file, &regions);
    for (macro_region &region : regions)
      {
	/* Upward-IMS carrier former (default off): when a variant
	   commits, the mutated region has already formed and the
	   established search is superseded for this region.  On any
	   refusal the function is byte-identical and the established
	   search below proceeds untouched.  */
	if (riscv_tt_macro_planner && riscv_tt_macro_ims_carrier
	    && upward_carrier_try (fn, region, &resid, dump_file, &changed))
	  {
	    rvtt_macro_region_release (&region);
	    continue;
	  }
	/* Deterministic carrier-grouping search: candidates ascend from
	   maximal sharing; the first whose descriptor synthesis proves
	   is committed.  When every candidate refuses, the region
	   refuses byte-identically.  */
	planner_process_region (fn, region, regions.length () == 1,
				&resid, dump_file, &changed);
	rvtt_macro_region_release (&region);
      }
    return changed ? TODO_df_finish : 0;
  }
};

} /* anonymous namespace */

/* Instantiate the macro-planner pass for CTXT; rvtt-passes.def places
   it before postreload, and it gates on the Tensix extension plus
   -mtt-tensix-macro-planner (or its analyze-only companion
   -mtt-tensix-macro-planner-analyze).  */

rtl_opt_pass *
make_pass_rvtt_macro_planner (gcc::context *ctxt)
{
  return new pass_rvtt_macro_planner (ctxt);
}
