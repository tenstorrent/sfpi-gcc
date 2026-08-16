/* Generic SFPLOADMACRO macro planner (analysis skeleton).
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
#include "tree-pass.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "df.h"
#include "tm_p.h"
#include "rvtt-protos.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"
#include "rvtt-macro-sched.h"
#include "rvtt-macro-desc.h"

/* The macro planner replaces every exact-calendar SFPLOADMACRO
   recognizer with regions, schedules, and descriptors derived from typed
   effects, dataflow proofs, and capability tables.  This pass is the
   planner's spine; at this stage it is analysis-only: under
   -mtt-tensix-macro-planner-analyze it reports discovered regions and
   named refusals to its dump and never mutates the function.  It runs
   after IRA/reload (hard LREGs final) and before the quarantined
   exact-calendar pass, the hazard scheduler, and replay formation.  */

namespace {

/* ---------------- Formation (WP7): emission from the descriptor ------ */

/* Function-global configuration-ownership proof, typed: the planner owns
   the macro configuration destinations and address-modifier slots for
   the whole function under the formation contract, so any call, any
   asm, and any typed config access touching an owned destination (or a
   statically unknown one) refuses.  Path-sensitive refinement through
   rvtt-macro-ownership is a documented later widening (fresh tests
   required); this matches the frozen contract byte for byte.  */

static bool
planner_config_ownership_ok (function *fn, const rvtt_macro::caps *c)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;
      FOR_BB_INSNS (bb, insn)
	{
	  if (!NONDEBUG_INSN_P (insn))
	    continue;
	  if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	    return false;
	  xtt_effect_set e = rvtt_insn_effects (insn);
	  if (!e.opaque
	      && ((e.config_dests_written | e.config_dests_read)
		  & c->owned_config_dests))
	    return false;
	}
    }
  return true;
}

/* Prove that hard register VALUE has no use after START before an
   all-lane definition kills it (the frozen pass's proof idiom).  */

static bool
planned_value_dead_after_p (rtx value, rtx_insn *start)
{
  basic_block bb = BLOCK_FOR_INSN (start);
  for (rtx_insn *insn = NEXT_INSN (start);
       insn && BLOCK_FOR_INSN (insn) == bb; insn = NEXT_INSN (insn))
    if (NONDEBUG_INSN_P (insn))
      {
	if (reg_referenced_p (value, PATTERN (insn)))
	  return false;
	if (reg_set_p (value, insn))
	  return true;
      }
  return !bitmap_bit_p (df_get_live_out (bb), REGNO (value));
}

/* Issue cost of materializing one 32-bit configuration word through an
   LREG: the SFPLOADI half count mirrors rvtt_emit_sfpxloadi's forms.  */

static unsigned
config_word_loadi_issues (uint32_t w)
{
  if (w <= 0x7fff || w >= 0xffff8000u || w <= 0xffff || !(w & 0xffff))
    return 1;
  if (!(w & 0x1fff))
    {
      unsigned exp = (w >> 23) & 0xff;
      if (exp < 127 + 16 && exp >= 127 - 14)
	return 1;
    }
  return 2;
}

/* Layer-6 profitability, derived from configuration and drain costs --
   no row thresholds anywhere.  Every run must independently amortize
   the full configuration prefix (the frozen conservative-per-run
   discipline): rows*ii + drain + config < rows * explicit-row issues.  */

static bool
run_profitable_p (const macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc, unsigned run_rows)
{
  const macro_row &row = region.rows[0];
  unsigned explicit_row = row.insns.length ()
    + (row.enable ? 1 : 0) + (row.separator ? 1 : 0);

  unsigned config_cost = 1;	/* all-lanes enable */
  config_cost += desc.n_setc16;
  for (unsigned t = 0; t != desc.n_templates; ++t)
    config_cost += config_word_loadi_issues (desc.templ[t]) + 1;
  for (unsigned m = 0; m != desc.n_seq; ++m)
    config_cost += config_word_loadi_issues (desc.seq[m]) + 1;
  if (desc.has_misc)
    config_cost += config_word_loadi_issues (desc.misc) + 1;

  unsigned macro_cost = config_cost + run_rows * schedule.ii
    + desc.drain_slots;
  return macro_cost < run_rows * explicit_row;
}

/* Emit one run: the configuration prefix (first run only), the per-row
   issue calendar from the descriptor, and the drain; then delete the
   explicit rows.  Everything emitted is descriptor data.  */

static void
emit_planner_run (macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc,
		  const rvtt_macro::caps *c,
		  unsigned begin, unsigned end, bool emit_config)
{
  const macro_row &first = region.rows[begin];
  rtx_insn *anchor = first.enable ? first.enable : first.insns[0];

  if (emit_config)
    {
      start_sequence ();
      emit_insn (copy_rtx (PATTERN (first.enable)));
      for (unsigned s = 0; s != desc.n_setc16; ++s)
	emit_insn (gen_rvtt_owned_setc16
		   (GEN_INT (desc.setc16[s].config_reg),
		    GEN_INT (desc.setc16[s].value)));
      rtx config_lreg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST);
      auto config_word = [&] (uint32_t word, unsigned dest)
	{
	  rvtt_emit_sfpxloadi (config_lreg, rvtt_gen_rtx_noval (XTT32SImode),
			       GEN_INT (word));
	  emit_insn (gen_rvtt_sfpwriteconfig_v (config_lreg,
						GEN_INT (dest)));
	};
      for (unsigned t = 0; t != desc.n_templates; ++t)
	config_word (desc.templ[t], t);
      for (unsigned m = 0; m != desc.n_seq; ++m)
	config_word (desc.seq[m], 4 + m);
      if (desc.has_misc)
	config_word (desc.misc, 8);
      rtx_insn *prefix = get_insns ();
      end_sequence ();
      emit_insn_before (prefix, anchor);
    }

  /* Per-macro carried memory operands and hidden template writes.  */
  rtx carrier_load_mem[4] = {}, carrier_store_mem[4] = {};
  uint32_t carrier_hidden[4] = {};
  const macro_row &row0 = region.rows[0];
  for (unsigned ix = 0; ix != row0.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row0.insns[ix]);
      rtx address, mode, addr_mode;
      if (ev.is_carrier && e.dst_mem_read
	  && rvtt_dst_access_operands (row0.insns[ix], e, &address, &mode,
				       &addr_mode))
	{
	  extract_insn (row0.insns[ix]);
	  carrier_load_mem[ev.macro_index] = recog_data.operand[1];
	}
      if (ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT
	  && ev.is_store)
	{
	  extract_insn (row0.insns[ix]);
	  carrier_store_mem[ev.macro_index] = recog_data.operand[0];
	}
      if (ev.realization == macro_event::LAUNCHED_TEMPLATE_SLOT
	  && !ev.is_store && ev.template_id < desc.n_templates)
	carrier_hidden[ev.macro_index]
	  |= rvtt_macro::template_hidden_lreg_writes
	       (c, desc.templ[ev.template_id]);
    }

  /* Planned destination of each explicit reload: the src field of the
     template consuming its value (decoded from the descriptor).  */
  unsigned explicit_planned[8] = {};
  for (unsigned ix = 0; ix != row0.insns.length (); ++ix)
    {
      const macro_event &ev = schedule.events[ix];
      xtt_effect_set e = rvtt_insn_effects (row0.insns[ix]);
      if (ev.realization != macro_event::EXPLICIT_INSN || !e.dst_mem_read
	  || ev.is_carrier || ix >= 8)
	continue;
      explicit_planned[ix] = 0;
      for (unsigned jx = 0; jx != row0.insns.length (); ++jx)
	{
	  const macro_event &cons = schedule.events[jx];
	  if (cons.realization != macro_event::LAUNCHED_TEMPLATE_SLOT
	      || cons.is_store || cons.template_id >= desc.n_templates)
	    continue;
	  xtt_effect_set ce = rvtt_insn_effects (row0.insns[jx]);
	  if (!(ce.lreg_read & e.lreg_write))
	    continue;
	  rvtt_macro::template_spec spec;
	  if (rvtt_macro::decode_template (desc.templ[cons.template_id],
					   &spec)
	      && spec.src_c)
	    explicit_planned[ix] = spec.src_c;
	}
    }

  start_sequence ();
  for (unsigned r = begin; r != end; ++r)
    {
      const macro_row &row = region.rows[r];
      unsigned parity = (r - begin) & 1;
      /* Issue order follows the schedule slots: carriers and explicit
	 reloads interleave exactly as derived.  */
      for (int slot = 0; slot != schedule.ii; ++slot)
	for (unsigned ix = 0; ix != row.insns.length (); ++ix)
	  {
	    const macro_event &ev = schedule.events[ix];
	    if (!ev.issues_word || ev.slot != slot)
	      continue;
	    if (ev.is_carrier)
	      {
		const macro_launch_spec *launch = nullptr;
		for (const macro_launch_spec &ls : desc.launches)
		  if (ls.macro_index == ev.macro_index)
		    launch = &ls;
		if (!launch)
		  continue;
		uint32_t word = launch->vd_alternates && parity
		  ? launch->word_alt : launch->word;
		unsigned vd = launch->vd_alternates && parity
		  ? launch->vd ^ 1 : launch->vd;
		rtx vd_reg = gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + vd);
		rtx lmem = carrier_load_mem[ev.macro_index];
		rtx smem = carrier_store_mem[ev.macro_index];
		rtx mem1 = lmem ? lmem : smem;
		rtx mem2 = smem ? smem : const0_rtx;
		if (!smem)
		  mem2 = const0_rtx;
		uint32_t hidden = carrier_hidden[ev.macro_index];
		if (hidden)
		  {
		    unsigned hreg = ctz_hwi (hidden);
		    emit_insn (gen_rvtt_sfploadmacro_hidden_int
			       (vd_reg, mem1, mem2,
				GEN_INT (launch->address),
				GEN_INT (launch->mode),
				GEN_INT (launch->addr_mode),
				GEN_INT (word),
				gen_rtx_REG (XTT32SImode,
					     SFPU_REG_FIRST + hreg)));
		  }
		else
		  emit_insn (gen_rvtt_sfploadmacro_int
			     (vd_reg, mem1, mem2 == const0_rtx && smem
			      ? smem : mem2,
			      GEN_INT (launch->address),
			      GEN_INT (launch->mode),
			      GEN_INT (launch->addr_mode),
			      GEN_INT (word)));
	      }
	    else
	      {
		/* Explicit reload retargeted to its planned register.  */
		rtx pat = copy_rtx (PATTERN (region.rows[r].insns[ix]));
		if (ix < 8 && explicit_planned[ix])
		  {
		    rtx set = GET_CODE (pat) == PARALLEL
		      ? XVECEXP (pat, 0, 0) : pat;
		    if (GET_CODE (set) == SET)
		      SET_DEST (set)
			= gen_rtx_REG (XTT32SImode,
				       SFPU_REG_FIRST + explicit_planned[ix]);
		  }
		emit_insn (pat);
	      }
	  }
    }
  for (int d = 0; d != desc.drain_slots; ++d)
    emit_insn (gen_rvtt_sfpnop ());
  rtx_insn *replacement = get_insns ();
  end_sequence ();
  emit_insn_before (replacement, anchor);

  for (unsigned r = begin; r != end; ++r)
    {
      const macro_row &row = region.rows[r];
      if (row.enable)
	delete_insn (row.enable);
      for (rtx_insn *insn : row.insns)
	delete_insn (insn);
      if (row.separator)
	delete_insn (row.separator);
    }
}

/* Form REGION when every proof holds; returns true when code changed.
   Refusal paths never mutate.  */

static bool
form_region (function *fn, macro_region &region,
	     const macro_schedule &schedule, const macro_descriptor &desc,
	     FILE *dump)
{
  rvtt_macro::cpu_t cpu = TARGET_XTT_TENSIX_BH ? rvtt_macro::CPU_BH
    : TARGET_XTT_TENSIX_WH ? rvtt_macro::CPU_WH : rvtt_macro::CPU_QSR;
  const rvtt_macro::caps *c = rvtt_macro_caps_for_cpu (cpu);
  if (!c || desc.refusal || desc.drain_slots < 0
      || desc.launches.is_empty ())
    return false;

  /* Every row needs its ambient all-lanes enable when the calendar is
     lane-predicated, and the launch VDs must be encodable.  */
  if (desc.needs_all_lanes_prefix)
    for (const macro_row &row : region.rows)
      if (!row.enable)
	return false;

  if (!planner_config_ownership_ok (fn, c))
    {
      if (dump)
	fprintf (dump, "Macro-planner formation-refusal:"
		 " config-ownership-unproven\n");
      return false;
    }

  /* Every planner-owned physical LREG must be dead after the region.  */
  rtx_insn *region_end = region.rows.last ().separator
    ? region.rows.last ().separator : region.rows.last ().insns.last ();
  for (unsigned reg = 0; reg != 17; ++reg)
    if ((desc.planned_lregs >> reg) & 1)
      if (!planned_value_dead_after_p
	    (gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + reg), region_end))
	{
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " planned-lreg-live\n");
	  return false;
	}

  /* Per-run profitability (conservative full-config accounting).  */
  auto_vec<unsigned> run_begins;
  for (unsigned r = 0; r != region.rows.length (); ++r)
    if (region.rows[r].starts_run)
      run_begins.safe_push (r);
  if (run_begins.is_empty ())
    run_begins.safe_push (0);
  for (unsigned b = 0; b != run_begins.length (); ++b)
    {
      unsigned begin = run_begins[b];
      unsigned end = b + 1 == run_begins.length ()
	? region.rows.length () : run_begins[b + 1];
      if (!run_profitable_p (region, schedule, desc, end - begin))
	{
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " unprofitable\n");
	  return false;
	}
    }

  for (unsigned b = 0; b != run_begins.length (); ++b)
    {
      unsigned begin = run_begins[b];
      unsigned end = b + 1 == run_begins.length ()
	? region.rows.length () : run_begins[b + 1];
      emit_planner_run (region, schedule, desc, c, begin, end, b == 0);
    }
  if (dump)
    fprintf (dump, "Macro-planner formed: rows=%u runs=%u\n",
	     region.rows.length (), run_begins.length ());
  return true;
}

const pass_data pass_data_rvtt_macro_planner =
{
  RTL_PASS,
  "rvtt_macro_planner",
  OPTGROUP_NONE,
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
    auto_vec<macro_region> regions;
    rvtt_macro_regions_discover (fn, dump_file, &regions);
    for (macro_region &region : regions)
      {
	/* Deterministic carrier-grouping search: candidates ascend from
	   maximal sharing; the first whose descriptor synthesis proves
	   is committed.  When every candidate refuses, the region
	   refuses byte-identically.  */
	for (unsigned candidate = 0; ; ++candidate)
	  {
	    macro_schedule schedule;
	    if (!rvtt_macro_schedule_region (region, &schedule, dump_file,
					     candidate))
	      break;		/* search exhausted (or no table)  */
	    bool proven = false;
	    macro_descriptor descriptor;
	    if (rvtt_macro_synthesize (region, schedule, &descriptor,
				       dump_file))
	      {
		if (riscv_tt_macro_planner_verify || flag_checking)
		  rvtt_macro_verify_descriptor (region, schedule,
						descriptor, dump_file);
		proven = !descriptor.refusal;
		if (proven && riscv_tt_macro_planner)
		  changed |= form_region (fn, region, schedule,
					  descriptor, dump_file);
		rvtt_macro_descriptor_release (&descriptor);
	      }
	    rvtt_macro_schedule_release (&schedule);
	    if (proven)
	      break;
	  }
	rvtt_macro_region_release (&region);
      }
    return changed ? TODO_df_finish : 0;
  }
};

} // anonymous namespace

rtl_opt_pass *
make_pass_rvtt_macro_planner (gcc::context *ctxt)
{
  return new pass_rvtt_macro_planner (ctxt);
}
