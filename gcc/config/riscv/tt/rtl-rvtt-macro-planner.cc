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
   configuration state, and then re-launched by a single instruction.
   Collapsing a repeated row into a macro removes words from the
   instruction stream even when it removes no work, and RISC-pushed words
   cost more to deliver than replayed ones (rvtt-cost.md).

   Rather than recognising a fixed set of known sequences, this pass
   derives a descriptor from the region's own dataflow and schedules it
   against a reservation table of per-unit, per-slot occupancy (in the
   style of Rau and Glaeser, MICRO-14, 1981).  The work is layered:

     ownership   (rvtt-macro-ownership)  prove the region owns the
                 configuration state it programs, function-wide or
                 for the loop-body region
     region      (rvtt-macro-region)     find candidate regions and rows
     desc        (rvtt-macro-desc)       synthesise the descriptor program
     sched       (rvtt-macro-sched)      place rows against unit/slot
                                         capacity
     epoch       (rvtt-macro-epoch)      configuration epochs across tiles
     verify      (rvtt-macro-verify)     re-derive the descriptor and
                                         compare before emitting

   Every layer may decline with a named refusal.  A verification mismatch
   is a refusal too, so a region is formed only if the re-derived
   descriptor matches the planned one.  The descriptor program lives in
   configuration state that outlives the region, which is why ownership
   is checked first.  The SequenceBits, adjacency and Misc field rules
   are from SFPLOADMACRO.md; the per-target capability tables are in
   rvtt-macro-tables-{bh,wh}.def (QSR has none).

   Under -mtt-tensix-macro-planner-analyze the pass only dumps the regions
   it finds and its refusals; under -mtt-tensix-macro-planner it forms
   them.  It runs after register allocation (hard LREGs are final) and
   before the hazard scheduler and replay formation.  */

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
#include "rvtt-rtl-lane-state.h"
#include "rvtt-delivery-cost.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"
#include "rvtt-macro-epoch.h"
#include "rvtt-raw-boundary.h"

namespace {

/* Form REGION in FN with SCHEDULE and DESC after the formation checks:
   configuration ownership, dead planned LREGs, the all-lanes entry proof,
   profitability and stride absorption.  RESID is the descriptor residency
   state; SOLE_REGION says REGION is the function's only macro region.
   Return true if the function was changed.  */

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

  /* Configuration ownership: the function-wide proof or, for a loop-body
     region, the preheader+body window.  */
  basic_block scoped_preheader = nullptr;
  if (!planner_config_ownership_ok (fn, c))
    {
      bool scoped_ok = false;

      /* With a loop preheader, ownership need only cover the preheader tail
	 and the body: configuration before the insertion point is overwritten
	 by the complete prefix.  */
      if (region.loop_body)
	{
	  scoped_preheader = loop_region_preheader (fn, region, nullptr);
	  scoped_ok = scoped_preheader && planner_config_window_ok (region);
	  if (scoped_ok && dump)
	    fprintf (dump,
		     "Macro-planner config-ownership: loop-scoped window\n");
	}

      /* CC-template programs may also use the region-scoped proof, which
	 covers straight-line regions without a loop preheader.  */
      if (!scoped_ok && desc.cc.active)
	{
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

  /* A loop-body region puts its configuration in the loop preheader.  */
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

  /* Configuration writes and row payloads are lane-gated.  Moving an ENCC
     cannot repair ROW_MASK, and moving configuration before its reset is
     invalid even when a row later executes with all lanes enabled.  */
  rvtt_rtl_lane_facts lanes (fn);
  rtx_insn *config_at = region.rows[0].enable
    ? region.rows[0].enable : region.rows[0].insns[0];
  unsigned config_lanes = config_preheader
    ? lanes.block_out (config_preheader) : lanes.before (config_at);
  if (!(config_lanes & RVTT_RTL_ROW_MASK_CLEAR))
    {
      rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
		   "Macro-planner formation-refusal:"
		   " cc-enable-unproved (ROW_MASK at configuration)\n");
      return false;
    }

  /* Every enable the formation consumes (each row's local enable, all deleted
     with one re-emitted in the prefix, and the preheader's trailing enable)
     must provably enable all lanes; the store and misc programs do not support
     a partial lane mask.  Region discovery already checks this.  */
  for (const macro_row &row : region.rows)
    if (row.enable && !cc_enable_all_lanes_proved_p (row.enable))
      {
	rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
		     "Macro-planner formation-refusal:"
		     " cc-enable-unproved\n");
	return false;
      }

  /* A lane-predicated schedule needs all lanes enabled on entry: either the
     first row's local enable (region members cannot write CC, so the state
     holds across every row) or, for a loop-body region, the preheader's
     trailing enable.  */
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
	      /* The trailing CC write is not provably all-lanes.  */
	      rvtt_refuse (RVTT_REF_CC_ENABLE_UNPROVED, dump,
			   "Macro-planner formation-refusal:"
			   " cc-enable-unproved\n");
	      return false;
	    }
	  enable_src = nullptr;	/* already in place; no copy */
	}
      else
	{
	  /* No enable precedes the region (LLK kernels set the lane state in
	     init code the IR cannot see).  A CC-template row's own all-lanes
	     restore can serve instead: copy it to the head of the
	     configuration prefix.  This relies on the convention, already used
	     by the outermost POPC -> ENCC rewrite in gimple-rvtt-cc.cc, that
	     the outermost lane state is all-lanes, so the copy rewrites a
	     state that already holds; each later row runs after the launched
	     restore template.  */
	  rtx_insn *proof_restore = nullptr;
	  if (desc.cc.active && region.rows.length () > 1)
	    for (rtx_insn *member : region.rows[0].insns)
	      {
		xtt_effect_set e = rvtt_macro_insn_effects (member);
		if (e.cc_write && !e.cc_read && !e.lreg_read
		    && !e.lreg_write && e.cc_write_all_lanes)
		  proof_restore = member;
	      }
	  if (proof_restore
	      && entry_ambient_all_lanes_p
		   (config_preheader ? config_preheader : region.bb,
		    config_preheader ? nullptr : config_at, dump))
	    {
	      enable_src = proof_restore;
	      materialized_enable = true;
	    }
	  /* Otherwise derive the lane state: entry_ambient_all_lanes_p walks
	     backwards from the configuration point and requires every path to
	     reach the function entry (all lanes enabled by the structured-CC
	     convention) or an all-lanes SFPENCC before any other CC-affecting
	     or opaque instruction.  On success emit the canonical all-lanes
	     SFPENCC at the head of the prefix; it rewrites the proven state
	     and costs one pushed word.  */
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
			 " all-lanes derived (flags and ROW_MASK proved);"
			 " canonical enable synthesized\n");
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

  /* Descriptor residency (rvtt-macro-desc.cc): if an identical descriptor
     program is already programmed at a dominating point and the owned state is
     unchanged since, omit the descriptor words.  This only computes the
     decision, early enough for the init-hoist pricing below.  */
  bool resident_elide
    = rvtt_macro_residency_lookup (fn, region, desc, c, resid, dump);

  /* Evaluate the cross-call init hoist without committing, so that
     profitability can price it.  At stage 2 the configuration prefix moves to
     the caller and is paid once per caller-loop entry rather than per run.
     The hoist is committed below, after every other refusal point.  */
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
	/* The prefix's enable must be a proven all-lanes SFPENCC, either in
	   the source or synthesized from the entry proof; a copied in-row
	   restore does not carry across the call.  */
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
  /* Price the caller-loop amortization only at stage 2 with a usable profile
     weight; otherwise price the prefix per run.  */
  int ih_pricing_stage
    = init_hoist_stage >= 2 && init_prog.caller_weight_ok
      ? init_hoist_stage : 0;

  /* Profitability.  In straight-line code each run pays for the full
     configuration prefix, unless the full init hoist moves it to the caller
     loop.  In a loop body the prefix is paid once in the preheader and weighed
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

  /* Emission deletes each row's Dst separator, which is valid only if the
     schedule absorbed the stride or the program keeps the separator
     (CC-template programs re-emit it as the restore-visibility slot).  */
  for (const macro_row &row : region.rows)
    if (row.separator && !schedule.absorbed_stride && !desc.keep_separator)
      {
	rvtt_refuse (RVTT_REF_STRIDE_NOT_ABSORBED, dump,
		     "Macro-planner formation-refusal:"
		     " stride-not-absorbed\n");
	return false;
      }

  /* Immediate-delta regions need the absorbed schedule, cannot keep the
     separator, and need every Dst access of every offset row to be rewritable
     to rows[0]'s base address.  Check that before changing anything.  */
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
	      xtt_effect_set e = rvtt_macro_insn_effects (insn);
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

  /* Compact CC schedule: the absorbing explicit load's address-mode operand
     must be rewritable.  */
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

  /* Cross-tile prefix hoist for CC-template programs.  If the configuration
     preheader is inside an enclosing loop (the tile loop) and the epoch proof
     shows nothing else writes the planner's SFPCONFIG destinations in that
     loop, hoist the descriptor words to the enclosing loop's preheader.  The
     enable and the owned SETC16 program stay per trip.  The hoisted block
     needs its own all-lanes enable (the LREG materialization must not run
     masked), so copy the proven one.  */
  /* A resident program makes the cross-tile hoist unnecessary.  */
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
	      /* Try to move the placement further out through enclosing loops
		 (rvtt-macro-desc.cc).  */
	      unsigned resid_levels = 0;
	      rvtt_macro_residency_extend (fn, region, desc, c, resid,
					   &hoist_preheader, &hoist_edge,
					   &resid_levels, dump);

	      /* All checks have passed; split the entry edge of a guarded
		 enclosing loop.  The new block runs exactly when the loop is
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

  /* Cross-call init hoist: when this straight-line region is the function's
     only macro content and its init prefix is call-invariant, move the prefix
     to the single caller's loop preheader, so it runs once per loop instead of
     once per call.  This is the last refusal point, so the caller-side
     insertion and the callee-side suppression happen together.  The commit
     re-runs the proof evaluated above; if the result differs, refuse, since
     profitability assumed the hoist.  */
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

  /* Drain placement (-mtt-tensix-optimize-drain-schedule, proofs in
     rtl-rvtt-schedule.cc): decide every boundary within the region before
     changing anything.  The final run's drain, which keeps in-flight events
     from reaching the following code, is never removed.  */
  /* Inter-row drain.  Rows within a run are emitted back-to-back, which is
     valid only if consecutive rows alternate VDs (rvtt-macro-sched.cc): a
     hosted event reads the launch VD up to drain_slots after the launch.  When
     synthesis fixes a value carrier's VD, every row targets the same register,
     and the next launch would overwrite it while the previous row's events are
     pending.  Then place the full drain between rows, matching the per-row
     schedule that was proven.  Alternating VDs, store-only VDs and the
     CC-template model (whose inter-row constraints macro_cc_model checks) need
     no inter-row drain.  */
  bool interrow_drain = false;
  if (desc.drain_slots > 0 && region.rows.length () > 1 && !desc.cc.active)
    for (const macro_launch_spec &l : desc.launches)
      if (!l.vd_alternates && !l.is_store_only)
	interrow_drain = true;
  if (interrow_drain && dump)
    fprintf (dump, "Macro-planner drain-interrow: drain=%d rows=%u"
	     " (fixed-vd value carrier)\n", desc.drain_slots,
	     region.rows.length ());
  /* Window pairing: replace the full inter-row drain with the minimal spacing
     the pending-event model proves (rvtt_macro_interrow_drain_tuned,
     rtl-rvtt-schedule.cc).  */
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

  /* Backedge drain: a loop-body region's final run ends at the loop latch, so
     its drain runs every trip although it is only needed on loop exit.  If the
     code after the backedge is safe (rvtt_macro_drain_backedge_elidable,
     rtl-rvtt-schedule.cc) and the loop has a single exit successor, remove the
     in-body drain and emit the drain on the exit path instead.  */
  bool backedge_elide = false;
  edge drain_exit_edge = nullptr;
  int exit_drain = desc.drain_slots;
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
	  int tail_exit_drain = desc.drain_slots;
	  backedge_elide = rvtt_macro_drain_backedge_elidable
	    (region, schedule, desc, first_run_end, dump, &tail_exit_drain);
	  if (backedge_elide)
	    {
	      drain_exit_edge = exit_e;
	      if (riscv_tt_opt_drain_exit)
		{
		  if (dump)
		    fprintf (dump, "Macro-planner drain-exit: compensation"
			     " %d -> %d SFPNOPs (loop tail covers the rest"
			     " of the decoded horizon)\n",
			     desc.drain_slots, tail_exit_drain);
		  exit_drain = tail_exit_drain;
		}
	    }
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
  if (backedge_elide && exit_drain > 0)
    {
      /* Emit the drain on the loop's exit path so that events from the final
	 trip complete; under -mtt-tensix-optimize-drain-exit, only the part
	 the loop tail does not already cover.  Place it at the head of the
	 exit destination if this loop is its only predecessor, else on a new
	 block split from the exit edge.  */
      basic_block dest = drain_exit_edge->dest;
      if (dest == EXIT_BLOCK_PTR_FOR_FN (fn) || !single_pred_p (dest)
	  || !bb_note (dest))
	dest = split_edge (drain_exit_edge);
      start_sequence ();
      for (int d = 0; d != exit_drain; ++d)
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
		 exit_drain, dest->index);
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

/* Try the schedule candidates for REGION in order and pass the first whose
   descriptor synthesizes and verifies to form_region.  Return true if a
   candidate was proven, whether or not it was formed; set *CHANGED if the code
   changed.  SOLE_REGION, RESID and DUMP are as for form_region.  */

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
	  /* A verification mismatch makes the candidate unproven.  */
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
/* Upward carrier former (-mtt-tensix-macro-ims-carrier).  The IMS repair
   search (Rau, MICRO-27, 1994) shrinks the set of hosted events; this searches
   the other way: duplicate one of the row's Dst loads into a free LREG (a new
   value carrier), clone the in-place operations that follow the load onto it,
   and rename one consumer web onto the new carrier so its events can be hosted
   there.  The change is applied to every row and then the whole pipeline
   (discovery, scheduling, synthesis, verification and the formation checks)
   runs again on the changed region.  The variant is kept only if it is proven
   with a strictly smaller initiation interval than before; otherwise the
   change is reverted.  A region-admitted load uses the no-increment address
   mode, so duplicating it has no effect beyond writing the new register.  */

/* Maximum number of variants tried per region.  */
static const unsigned UPWARD_CARRIER_BUDGET = 24; /* variants per region */

/* Refusal names used in dumps.  */
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

/* Undo log for a variant.  Renamed insns get a copied pattern, so revert
   restores the original pattern objects.  */

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

/* Replace, in place, every hard LREG in *LOC whose number is in LREG_MASK by
   NEWREG.  Every occurrence in the insn is replaced, which keeps tied operands
   tied; upward_compute_renames only produces masks for which that is
   correct.  */

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

/* The carrier register.  The launch word's VDLo field encodes only VD 0..3
   (VDHi shares the address LSB), so the carrier must be one of L0..L3.  If
   they are all in use, a low-register web used only inside the rows may first
   be moved to a free high register.  Registers are tried highest first to
   avoid L0, which also collides with the cast class's VC:=VD encoding (see
   rvtt-macro-derive-core.h).  */

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
  /* Otherwise find a free high register and a low register used only by this
     region's row members.  */
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

/* One variant: duplicate the SEED load, clone the in-place operations that
   follow it, and move the TARGETS chain onto the new carrier.  Indices are
   positions in row 0; each row recomputes the structure from its own
   registers, and any difference refuses.  */

struct upward_variant
{
  unsigned seed_ix;
  /* The chain, in program order: the first member reads the seed's value after
     the in-place operations; each later member reads the previous member's
     result.  Each member's definition is renamed onto the new carrier.  */
  unsigned targets[4];
  unsigned n_targets;
  /* Place the duplicate load directly after the seed or, when nothing needs
     cloning, at the row head, where the earlier launch slot gives the hosted
     chain more room.  Both positions read the same Dst word, since row members
     do not change the RWC counters.  */
  bool reload_at_head;
};

/* Compute, for ROW and variant V, the registers to rename in each insn (MASKS)
   and the in-place operations to clone, in program order (PREFIX).  Does not
   change ROW.  On failure set *REFUSAL and return false.

   Why the result is equivalent:
   - the seed uses the no-increment address mode (any other mode has an unknown
     RWC effect, which discovery refuses), so the duplicate only writes the new
     register;
   - the duplicate and clones go directly after the seed, and nothing between
     the seed and a cloned operation redefines the clone's other sources, so
     the new register gets the same value;
   - each chain member writes a single fresh register, so renaming the members'
     definitions onto the new carrier makes it hold each chain value in turn.
     Every use reached by a renamed definition is renamed too, and a use of a
     value after the next member's definition refuses.  After the chain tail,
     tied in-place followers move with the register and a fresh redefinition
     ends the renamed range.  */

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

  /* The seed must be a plain (not live-value merging) Dst load writing one of
     L0..L7.  */
  xtt_effect_set se = rvtt_macro_insn_effects (row.insns[v.seed_ix]);
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

  /* Chain members are at increasing positions after the seed; the first reads
     the seed register and each later one the previous member's result.  */
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
      xtt_effect_set te = rvtt_macro_insn_effects (row.insns[tix]);
      uint32_t need = k == 0 ? dmask : chain_w[k - 1];
      if (te.opaque || te.dst_mem_read || te.dst_mem_write
	  || !(te.lreg_read & need))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* A member either defines one fresh register or updates the incoming
	 register in place.  */
      uint32_t w = te.lreg_write;
      bool in_place = w == need && (te.lreg_read & w) != 0;
      if (!w || (w & (w - 1)) != 0 || (!in_place && (te.lreg_read & w)))
	{
	  *refusal = upward_refusal_legality;
	  return false;
	}
      /* A later member's read of the seed register is not renamed (the mask
	 below adds DMASK only for the head), so it still reads the original
	 value.  */
      chain_w[k] = w;
    }

  /* Every writer of the seed register between the seed and the chain head must
     update it in place; each is cloned onto the new carrier.  A fresh
     redefinition means the head does not read the loaded value.  */
  for (unsigned ix = v.seed_ix + 1; ix < v.targets[0]; ++ix)
    {
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
      if (e.opaque)
	continue;
      if (e.lreg_write & dmask)
	{
	  /* Do not clone an instruction that advances the PRNG.  */
	  if (e.prng_advance)
	    {
	      *refusal = upward_refusal_legality;
	      return false;
	    }
	  if (e.lreg_write != dmask || !(e.lreg_read & dmask)
	      || e.dst_mem_read || e.dst_mem_write)
	    {
	      *refusal = upward_refusal_legality;
	      return false;
	    }
	  /* The clone's other sources must not be redefined between the seed
	     and this operation.  */
	  uint32_t other = e.lreg_read & ~dmask;
	  for (unsigned jx = v.seed_ix + 1; jx < ix; ++jx)
	    if (rvtt_macro_insn_effects (row.insns[jx]).lreg_write & other)
	      {
		*refusal = upward_refusal_web;
		return false;
	      }
	  prefix->safe_push (ix);
	}
    }

  /* Rename the head's read of the seed register and each later member's read
     of the previous member, plus every member's definition.  */
  (*masks)[v.targets[0]] = dmask | chain_w[0];
  for (unsigned k = 1; k != v.n_targets; ++k)
    (*masks)[v.targets[k]] = chain_w[k - 1] | chain_w[k];

  /* Between member K and the next member, uses of member K's value are
     renamed; another redefinition of its register refuses.  */
  for (unsigned k = 0; k + 1 < v.n_targets; ++k)
    {
      uint32_t w = chain_w[k];
      unsigned from = v.targets[k] + 1;
      unsigned upto = v.targets[k + 1];
      for (unsigned ix = from; ix < upto; ++ix)
	{
	  xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
	  if (e.opaque)
	    {
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_write & w)
	    {
	      /* Redefined before the next chain member.  */
	      *refusal = upward_refusal_web;
	      return false;
	    }
	  if (e.lreg_read & w)
	    (*masks)[ix] |= w;
	}
      /* After the next member, a read of member K's register that is not
	 preceded by a fresh redefinition would see the renamed value: refuse.
	 If the next member updates the register in place, later reads belong
	 to it and are checked with it.  */
      if (chain_w[k + 1] == w)
	continue;
      for (unsigned ix = upto + 1; ix < n; ++ix)
	{
	  xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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

  /* Propagate the tail's rename through the rest of the row.  */
  uint32_t wmask = chain_w[v.n_targets - 1];
  bool active = true;
  for (unsigned ix = v.targets[v.n_targets - 1] + 1; ix < n && active; ++ix)
    {
      xtt_effect_set e = rvtt_macro_insn_effects (row.insns[ix]);
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

/* Apply variant V to every row of REGION, recording the changes in JOURNAL.
   REGS is the carrier register choice.  If a row differs from row 0's
   structure, revert, set *REFUSAL and return false.  */

static bool
upward_apply (macro_region &region, const upward_variant &v,
	      const upward_carrier_choice &regs,
	      const auto_vec<uint32_t> &masks0,
	      const auto_vec<unsigned> &prefix0, upward_journal *journal,
	      const char **refusal)
{
  int newreg_lreg = regs.vd;
  rtx newreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + newreg_lreg);
  /* First move the row-internal web off the chosen low register.  */
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
      /* Each row must have the same clone set and rename positions as row
	 0.  */
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

      /* Renames first, on copied patterns.  */
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

      /* Then the duplicate load and the clones.  */
      uint32_t dmask
	= rvtt_macro_insn_effects (row.insns[v.seed_ix]).lreg_write;
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

/* Find the first proven schedule candidate of REGION without changing
   anything; store its initiation interval in *II_OUT and its index in
   *CANDIDATE_OUT.  */

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

/* Try upward-carrier variants on REGION in FN.  Return true if one was kept
   and formed; otherwise FN is unchanged.  */

static bool
upward_carrier_try (function *fn, macro_region &region,
		    macro_residency_state *resid, FILE *dump, bool *changed)
{
  if (region.rows.is_empty ())
    return false;

  /* Leave rows that define predicates to the CC select programs.  */
  for (rtx_insn *insn : region.rows[0].insns)
    {
      xtt_effect_set e = rvtt_macro_insn_effects (insn);
      if (!(e.dst_mem_read || e.dst_mem_write) && e.cc_write
	  && e.lreg_read != 0 && !e.lreg_write)
	return false;
    }

  /* The normal search result is the baseline to improve on.  If it proves
     nothing, a variant may still make the region formable, and form_region's
     profitability checks compare it with the explicit code.  */
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

  /* Find the explicit value events of the baseline schedule; for an unproven
     region use the first (maximal sharing) candidate, which always exists.  */
  macro_schedule est;
  if (!rvtt_macro_schedule_region (region, &est, nullptr,
				   est_proven ? est_candidate : 0))
    return false;
  const macro_row &row0 = region.rows[0];
  auto_vec<upward_variant> variants;
  /* A chain member is a non-memory value event of a hostable sub-unit class
     writing a single register.  */
  auto chain_member_p = [&] (unsigned ix, uint32_t need) -> bool
    {
      /* With a proven baseline only explicit events can move; otherwise any
	 value event can.  */
      if (est_proven
	  && est.events[ix].realization != macro_event::EXPLICIT_INSN)
	return false;
      if (!est_proven
	  && est.events[ix].realization == macro_event::CC_COALESCED)
	return false;
      xtt_effect_set te = rvtt_macro_insn_effects (row0.insns[ix]);
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
      xtt_effect_set se = rvtt_macro_insn_effects (row0.insns[seed_ix]);
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
	  /* Extend the chain greedily with the event that reads the tail's
	     result; upward_compute_renames checks the result.  */
	  upward_variant v;
	  v.seed_ix = seed_ix;
	  v.targets[0] = tix;
	  v.n_targets = 1;
	  v.reload_at_head = false;
	  while (v.n_targets < 4)
	    {
	      uint32_t w = rvtt_insn_effects
		(row0.insns[v.targets[v.n_targets - 1]]).lreg_write;
	      /* The next member is the next definition of the tail's register
		 if it updates it in place, else the last reader before that
		 definition.  */
	      int next = -1;
	      for (unsigned jx = v.targets[v.n_targets - 1] + 1;
		   jx != row0.insns.length (); ++jx)
		{
		  xtt_effect_set je = rvtt_macro_insn_effects (row0.insns[jx]);
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
	  /* With nothing to clone, also try the row-head position, first.  */
	  bool prefix_free = true;
	  for (unsigned jx = seed_ix + 1; jx < tix; ++jx)
	    {
	      xtt_effect_set je = rvtt_macro_insn_effects (row0.insns[jx]);
	      if (!je.opaque && (je.lreg_write & dmask))
		prefix_free = false;
	    }
	  /* Try the longest chain first, then each shorter prefix.  */
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

      /* Rediscover regions; the changed region must reappear with the same
	 number of rows.  */
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
      /* Dump the probe in full, so the dump shows why a variant succeeds or
	 fails.  */
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
	  /* This region is not known to be the only one, so the cross-call
	     init hoist is not used.  */
	  bool proven = planner_process_region (fn, *sel, /*sole_region=*/false,
						resid, dump, &local_changed);
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
    /* Formed calendars preserve the original lane state at every
       unmodified source point, but their hidden CC programs are opaque
       to generic RTL effect decoding.  Keep the pre-formation proof for
       later source regions; never infer a fact for newly emitted UIDs.  */
    rvtt_rtl_lane_facts original_lanes (fn);
    rvtt_rtl_lane_snapshot_scope lane_scope (original_lanes);
    /* Clear the previous function's launch effect records
       (rvtt-effects.h).  */
    rvtt_planner_launch_effects_reset ();
    /* Per-function record of programmed descriptors and emitted insns
       (rvtt-macro-desc.cc).  Regions are processed in program order, so the
       first formed program is the one later regions can reuse.  */
    macro_residency_state resid;
    auto_vec<macro_region> regions;
    rvtt_macro_regions_discover (fn, dump_file, &regions);
    for (macro_region &region : regions)
      {
	/* If an upward-carrier variant is kept, the region is already formed;
	   otherwise fall through to the normal search.  */
	if (riscv_tt_macro_planner && riscv_tt_macro_ims_carrier
	    && upward_carrier_try (fn, region, &resid, dump_file, &changed))
	  {
	    rvtt_macro_region_release (&region);
	    continue;
	  }
	/* Normal search, from maximal sharing down.  */
	planner_process_region (fn, region, regions.length () == 1,
				&resid, dump_file, &changed);
	rvtt_macro_region_release (&region);
      }
    return changed ? TODO_df_finish : 0;
  }
};

} /* anonymous namespace */

/* Create the macro-planner pass.  It is enabled by -mtt-tensix-macro-planner
   or -mtt-tensix-macro-planner-analyze.  */

rtl_opt_pass *
make_pass_rvtt_macro_planner (gcc::context *ctxt)
{
  return new pass_rvtt_macro_planner (ctxt);
}
