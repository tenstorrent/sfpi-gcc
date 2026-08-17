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

/* Region-scoped configuration ownership for a loop-body region: the
   fallback when the function-global proof fails because the enclosing
   function carries foreign Tensix code (the real-kernel shape of the
   typecast blocker -- the four architectural faces sit in a loop inside
   a function full of opaque init/dataflow instructions, so the
   function-global scan can never prove ownership there).

   The configuration window is the proven structural preheader's TAIL
   (the compiler-owned insertion point after the last reachable foreign
   call, asm, or configuration access) plus the loop body.  Placement at
   the tail dominates every trip's launches, and the window proof shows
   no path from the materialization point to the final drain contains
   another owner: the preheader's unique successor is the body (proven
   by loop_region_preheader), the body's only edges are its self-loop
   and its exit, and this scan proves every body instruction is either a
   region-owned issue or provably inert scalar code -- no call, no asm,
   no Tensix issue, and no volatile memory reference (the shape of every
   raw MMIO instruction push or configuration access the typed effect
   vocabulary cannot see).  Foreign owners BEFORE the insertion point
   are simply overwritten by the prefix.  Code after the loop exit runs
   after the drain and is beyond the descriptor's lifetime, exactly as
   when the planner forms inside an out-of-line callee invoked from an
   opaque caller (the shipped straight-line contract).  */

static bool
planner_config_window_ok (const macro_region &region)
{
  basic_block body = region.bb;
  for (rtx_insn *insn = BB_HEAD (body); insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn))
	{
	  bool owned = false;
	  for (const macro_row &row : region.rows)
	    {
	      owned |= insn == row.enable || insn == row.separator;
	      for (rtx_insn *member : row.insns)
		owned |= insn == member;
	    }
	  for (rtx_insn *sep : region.run_separators)
	    owned |= insn == sep;
	  if (!owned)
	    {
	      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
		return false;
	      if (recog_memoized (insn) >= 0
		  && get_attr_type (insn) == TYPE_TENSIX)
		return false;
	      if (volatile_refs_p (PATTERN (insn)))
		return false;
	    }
	}
      if (insn == BB_END (body))
	break;
    }
  return true;
}

/* Region-scoped configuration ownership (WP9; the refinement the
   function-global proof documents).  Used ONLY for proven CC-template
   programs, and only as the ADDITIONAL fallback after the loop-scoped
   window proof above (which covers any loop-body region) has been
   tried: this path adds the straight-line CC-template shapes the
   window path does not reach.  Every other shape keeps the
   conservative function-global gate above, so their formation and
   refusal behavior is unchanged.

   Soundness bounds, matching the deleted quarantined pass's select
   contract ("no other Tensix issue may own config, CC, LREG, or
   calendar state between the materialization and a launch"):

   - The planner's configuration prefix rewrites EVERY owned destination
     the calendar consumes (templates, sequence words, misc; the CC
     programs absorb no stride, so no address-modifier slot is read), so
     foreign configuration writes BEFORE the prefix are dead.

   - Between the prefix placement point and the region end -- the
     preheader tail plus the loop body for a loop-body region, or the
     region span itself for a straight-line one -- there must be no
     call, no inline assembly, and no typed access to an owned
     configuration destination.  For a loop-body region the scope also
     covers the chain from the consumed trailing enable, so no opaque
     issue can sit between the lane-state proof and the loop.

   - Foreign code AFTER the region is tolerated: the LLK ownership
     convention (carried from the frozen pass and its silicon-proven
     integrations) is that every SFPLOADMACRO consumer programs its own
     descriptors before launching.  This is a documented accepted risk,
     mirrored in docs/MACRO_PLANNER.md.  */

static bool
planner_scope_insn_clean_p (rtx_insn *insn, const rvtt_macro::caps *c)
{
  if (!NONDEBUG_INSN_P (insn))
    return true;
  if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  if (!e.opaque
      && ((e.config_dests_written | e.config_dests_read)
	  & c->owned_config_dests))
    return false;
  return true;
}

static bool
planner_region_config_ownership_ok (const macro_region &region,
				    basic_block config_preheader,
				    rtx_insn *scope_begin,
				    const rvtt_macro::caps *c)
{
  /* The region's own basic block from the scope begin (the prefix
     anchor for straight-line regions; the block head for loop bodies,
     whose every trip re-executes under the preheader-materialized
     configuration).  */
  basic_block bb = region.bb;
  rtx_insn *from = config_preheader ? BB_HEAD (bb) : scope_begin;
  for (rtx_insn *insn = from; insn; insn = NEXT_INSN (insn))
    {
      if (!planner_scope_insn_clean_p (insn, c))
	return false;
      if (insn == BB_END (bb) || insn == region.last)
	break;
      if (!config_preheader && insn == region.last)
	break;
    }
  /* For a loop-body region: the preheader from the prefix insertion
     point to the loop entry.  The prefix is placed at the block end
     (before a trailing jump), so only the jump can follow it --
     verified here rather than assumed.  */
  if (config_preheader)
    {
      rtx_insn *tail = BB_END (config_preheader);
      if (tail && !JUMP_P (tail) && !planner_scope_insn_clean_p (tail, c))
	return false;
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

/* Issue cost of the full configuration prefix (all-lanes enable, owned
   SETC16 program, and every descriptor word's materialization).  */

static unsigned
config_prefix_cost (const macro_descriptor &desc)
{
  unsigned config_cost = 1;	/* all-lanes enable */
  config_cost += desc.n_setc16;
  for (unsigned t = 0; t != desc.n_templates; ++t)
    config_cost += config_word_loadi_issues (desc.templ[t]) + 1;
  for (unsigned m = 0; m != desc.n_seq; ++m)
    config_cost += config_word_loadi_issues (desc.seq[m]) + 1;
  if (desc.has_misc)
    config_cost += config_word_loadi_issues (desc.misc) + 1;
  return config_cost;
}

/* Issue cost of one explicit (unformed) row.  */

static unsigned
explicit_row_cost (const macro_region &region)
{
  const macro_row &row = region.rows[0];
  return row.insns.length ()
    + (row.enable ? 1 : 0) + (row.separator ? 1 : 0);
}

/* Layer-6 profitability, derived from configuration and drain costs --
   no row thresholds anywhere.  Every run must independently amortize
   the full configuration prefix (the frozen conservative-per-run
   discipline): rows*ii + drain + config < rows * explicit-row issues.  */

static bool
run_profitable_p (const macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc, unsigned run_rows)
{
  unsigned macro_cost = config_prefix_cost (desc) + run_rows * schedule.ii
    + desc.drain_slots;
  return macro_cost < run_rows * explicit_row_cost (region);
}

/* Loop trip weight (WP8): the profile-estimated body/preheader
   execution-count ratio of a loop-body region.  Purely a profitability
   weight -- never a correctness input -- exact where the profile is
   (constant-bound loops), the static estimate elsewhere.  The two
   outputs report the ratio as an unreduced fraction so profitability
   can weigh it without rounding.  Returns false when the profile gives
   no usable estimate.  */

static bool
loop_trip_weight (basic_block body, basic_block preheader,
		  gcov_type *body_count, gcov_type *preheader_count)
{
  profile_count bc = body->count, pc = preheader->count;
  if (!bc.initialized_p () || !pc.initialized_p () || !pc.nonzero_p ())
    return false;
  gcov_type b = bc.to_gcov_type (), p = pc.to_gcov_type ();
  if (p <= 0 || b < p)
    return false;
  /* Keep the products of profitability inside 64 bits.  */
  while (b > (gcov_type) 1 << 48)
    {
      b >>= 8;
      p = p >> 8 ? p >> 8 : 1;
    }
  *body_count = b;
  *preheader_count = p;
  return true;
}

/* Loop-body profitability: the configuration prefix sits in the
   preheader and is paid once per loop entry, while every trip pays each
   run's launch calendar and drain against the explicit rows it
   replaces.  Weighted by the profile ratio without rounding:
   config * preheader_count + per_trip_macro * body_count
     < per_trip_explicit * body_count.  */

static bool
loop_profitable_p (const macro_region &region, const macro_schedule &schedule,
		   const macro_descriptor &desc, gcov_type body_count,
		   gcov_type preheader_count, unsigned n_runs)
{
  unsigned total_rows = region.rows.length ();
  unsigned per_trip_macro = total_rows * schedule.ii
    + n_runs * desc.drain_slots;
  unsigned per_trip_explicit = total_rows * explicit_row_cost (region);
  uint64_t macro_cost = (uint64_t) config_prefix_cost (desc) * preheader_count
    + (uint64_t) per_trip_macro * body_count;
  return macro_cost < (uint64_t) per_trip_explicit * body_count;
}

/* An ambient lane-enable shape: only a CC write, no other
   architectural effect (the region scanner's pure-CC-write class).
   This classifies the SHAPE only; whether the written value provably
   enables all lanes is the separate cc_write_all_lanes proof, checked
   at every consumption site (an unproved value is a named
   cc-enable-unproved refusal, never an ambient enable).  */

static bool
pure_cc_write_insn_p (rtx_insn *insn)
{
  if (!NONDEBUG_INSN_P (insn))
    return false;
  xtt_effect_set e = rvtt_insn_effects (insn);
  return !e.opaque && e.cc_write && !e.cc_read
    && !e.lreg_read && !e.lreg_write
    && !e.config_dests_written && !e.addr_mod_slot_write
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* The consumer-side all-lanes proof of an ambient enable: the written
   value must be word-exact against the capability table's
   architectural all-lanes SFPENCC encoding (rvtt_insn_effects derives
   the bit through the one shared derivation, so this proof can never
   drift from the encoding the hardware sees).  */

static bool
cc_enable_all_lanes_proved_p (rtx_insn *insn)
{
  return rvtt_insn_effects (insn).cc_write_all_lanes;
}

/* The trailing ambient enable of a proven loop preheader: the LAST
   Tensix issue in PREHEADER when it is a pure CC write.  With
   whole-body ownership (no CC writer inside the loop) this locates the
   instruction that decides the lane state at every trip's region
   entry, replacing the first row's local enable for regions whose
   enable was written once outside the loop.  This finds the SHAPE
   only; the caller must still prove the written value is the
   all-lanes pattern (cc_enable_all_lanes_proved_p) before consuming
   it.  */

static rtx_insn *
preheader_trailing_enable (basic_block preheader)
{
  /* Walk the unique-predecessor chain from the preheader upward until a
     Tensix issue is found: only scalar code may sit between the enable
     and the loop entry, so the dominating chain's LAST Tensix issue
     decides the proof.  */
  basic_block bb = preheader;
  for (unsigned depth = 0; depth != 16; ++depth)
    {
      rtx_insn *last_tensix = nullptr;
      for (rtx_insn *insn = BB_HEAD (bb); insn; insn = NEXT_INSN (insn))
	{
	  if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	      && get_attr_type (insn) == TYPE_TENSIX)
	    last_tensix = insn;
	  if (insn == BB_END (bb))
	    break;
	}
      if (last_tensix)
	return pure_cc_write_insn_p (last_tensix) ? last_tensix : nullptr;
      if (!single_pred_p (bb)
	  || single_pred (bb) == ENTRY_BLOCK_PTR_FOR_FN (cfun))
	return nullptr;
      bb = single_pred (bb);
    }
  return nullptr;
}

/* Structural preheader of a loop-body region, with the zero-trip and
   whole-body ownership obligations (WP8).  The loop header must have
   exactly its backedge plus one external incoming edge; the incoming
   block must have no other successor, which proves at least one trip on
   this edge, so hoisting the all-lanes enable is not a zero-trip CC
   change; and every Tensix issue in the body must belong to the region,
   so no foreign issue can mutate configuration, CC, or counter state
   between the preheader materialization and any launch.  Refusal paths
   return null after dumping a stable name.  */

static basic_block
loop_region_preheader (function *fn, const macro_region &region, FILE *dump)
{
  basic_block body = region.bb;
  edge incoming = nullptr;
  unsigned self_edges = 0, external_edges = 0;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, body->preds)
    if (e->src == body)
      ++self_edges;
    else
      {
	++external_edges;
	incoming = e;
      }
  if (self_edges != 1 || external_edges != 1
      || incoming->src == ENTRY_BLOCK_PTR_FOR_FN (fn))
    {
      if (dump)
	fprintf (dump, "Macro-planner formation-refusal:"
		 " loop-preheader-unproven\n");
      return nullptr;
    }
  if (EDGE_COUNT (incoming->src->succs) != 1)
    {
      if (dump)
	fprintf (dump, "Macro-planner formation-refusal:"
		 " zero-trip-preheader-unproven\n");
      return nullptr;
    }

  for (rtx_insn *insn = BB_HEAD (body); insn; insn = NEXT_INSN (insn))
    {
      if (NONDEBUG_INSN_P (insn) && recog_memoized (insn) >= 0
	  && get_attr_type (insn) == TYPE_TENSIX)
	{
	  bool owned = false;
	  for (const macro_row &row : region.rows)
	    {
	      owned |= insn == row.enable || insn == row.separator;
	      for (rtx_insn *member : row.insns)
		owned |= insn == member;
	    }
	  for (rtx_insn *sep : region.run_separators)
	    owned |= insn == sep;
	  if (!owned)
	    {
	      if (dump)
		fprintf (dump, "Macro-planner formation-refusal:"
			 " loop-body-not-owned\n");
	      return nullptr;
	    }
	}
      if (insn == BB_END (body))
	break;
    }
  return incoming->src;
}

/* Rewrite the typed address-mode operand of a copied explicit-load
   pattern to ADDR_MODE (WP10 compact CC calendar: the trailing load's
   own auto-increment mode absorbs the deleted separator's stride).
   The operand position mirrors rvtt_dst_access_operands' positional
   knowledge for the one admitted load pattern: rvtt_sfpload_lv_int
   carries operands 1..8 as unspec_volatile vector elements 0..7, so
   the addr_mode operand 8 is element 7.  Returns false -- without
   mutating -- for any other pattern; formation checks this BEFORE any
   emission so refusal paths never mutate.  */

static bool
planner_rewrite_load_addr_mode (rtx_insn *orig, rtx pat, unsigned addr_mode)
{
  if (recog_memoized (orig) != CODE_FOR_rvtt_sfpload_lv_int)
    return false;
  rtx set = GET_CODE (pat) == PARALLEL ? XVECEXP (pat, 0, 0) : pat;
  if (GET_CODE (set) != SET)
    return false;
  rtx src = SET_SRC (set);
  if (GET_CODE (src) != UNSPEC_VOLATILE || XVECLEN (src, 0) < 8)
    return false;
  if (pat != PATTERN (orig))	/* the copy, never the original */
    XVECEXP (src, 0, 7) = GEN_INT (addr_mode);
  return true;
}

/* Emit one run: the configuration prefix (first run only; hoisted to
   CONFIG_PREHEADER for a proven loop-body region), the per-row issue
   calendar from the descriptor, and the drain; then delete the explicit
   rows.  Everything emitted is descriptor data.  */

static void
emit_planner_run (macro_region &region, const macro_schedule &schedule,
		  const macro_descriptor &desc,
		  const rvtt_macro::caps *c,
		  unsigned begin, unsigned end, bool emit_config,
		  basic_block config_preheader, rtx_insn *enable_src)
{
  const macro_row &first = region.rows[begin];
  rtx_insn *anchor = first.enable ? first.enable : first.insns[0];

  if (emit_config)
    {
      /* The all-lanes proof source ENABLE_SRC is the first row's local
	 enable, or (WP10) the first row's own proven all-lanes restore
	 materialized in the prefix -- both proven word-exact all-lanes
	 by formation (cc_enable_all_lanes_proved_p), so this copy
	 re-establishes exactly the proven state; the WP8 relaxation
	 from every-row holds because no region member may write CC
	 outside the admitted CC-template roles, whose only lane-state
	 net effect is the proven all-lanes restore.  A null ENABLE_SRC
	 is the loop preheader's own trailing enable (proven all-lanes;
	 already in place; no copy).  */
      start_sequence ();
      if (enable_src)
	emit_insn (copy_rtx (PATTERN (enable_src)));
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
      if (config_preheader)
	{
	  /* Loop-body region: the prefix executes once, in the proven
	     structural preheader (>= one trip; see
	     loop_region_preheader).  */
	  rtx_insn *tail = BB_END (config_preheader);
	  if (tail && JUMP_P (tail))
	    emit_insn_before (prefix, tail);
	  else
	    emit_insn_after (prefix, tail);
	}
      else
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
     template consuming its value (decoded from the descriptor), or --
     for a load feeding the coalesced lane-merge (WP9) -- the shared
     launch VD the predicated-overwrite dataflow flows through.  */
  unsigned explicit_planned[8] = {};
  bool explicit_planned_valid[8] = {};
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
	  if (cons.realization == macro_event::CC_COALESCED)
	    {
	      xtt_effect_set ce = rvtt_insn_effects (row0.insns[jx]);
	      if ((ce.lreg_read & e.lreg_write)
		  && !desc.launches.is_empty ())
		{
		  explicit_planned[ix] = desc.launches[0].vd;
		  explicit_planned_valid[ix] = true;
		}
	      continue;
	    }
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
	    {
	      explicit_planned[ix] = spec.src_c;
	      explicit_planned_valid[ix] = true;
	    }
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
		if (ix < 8 && explicit_planned_valid[ix])
		  {
		    rtx set = GET_CODE (pat) == PARALLEL
		      ? XVECEXP (pat, 0, 0) : pat;
		    if (GET_CODE (set) == SET)
		      SET_DEST (set)
			= gen_rtx_REG (XTT32SImode,
				       SFPU_REG_FIRST + explicit_planned[ix]);
		  }
		/* WP10 compact CC calendar: the trailing explicit load
		   absorbs the deleted separator's Dst stride through the
		   tables' owned auto-increment address-modifier slot
		   (the SETC16 programs in the configuration prefix own
		   its meaning).  Formation proved the operand rewrite
		   possible before any mutation.  */
		if (ev.absorbs_stride)
		  {
		    bool ok = planner_rewrite_load_addr_mode
		      (region.rows[r].insns[ix], pat,
		       c->auto_increment_dst2_addr_mode);
		    gcc_assert (ok);
		  }
		emit_insn (pat);
	      }
	  }
      /* WP9: the proven CC-template program keeps the row's typed
	 separator in place -- its issue slot is the restore's
	 visibility slot (macro_cc_model), so the next row's launch
	 latches the restored all-lanes mask.  Re-emitted verbatim.  */
      if (desc.keep_separator && row.separator)
	emit_insn (copy_rtx (PATTERN (row.separator)));
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

  /* Configuration ownership: the function-global proof, or the ordered
     region-scoped fallbacks (union merge, WP9 x cross-function
     regions): (1) any loop-body region first tries the loop-scoped
     preheader+body WINDOW proof (the cheaper, more general path -- the
     preheader is computed quietly here; the window dump line names the
     sharing); (2) a proven CC-template program that the window path
     did not prove -- including the straight-line shapes the window
     never covers -- additionally tries the WP9 CC-scoped proof
     (planner_region_config_ownership_ok; no success dump line, as
     before).  Only when every applicable proof fails does the refusal
     keep its established name.  Configuration placement is decided
     once: scoped_preheader is set only by the window path, and the
     loop-preheader recomputation below covers the CC-scoped loop
     case, so the prefix is never double-placed.  */
  basic_block scoped_preheader = nullptr;
  if (!planner_config_ownership_ok (fn, c))
    {
      if (region.loop_body)
	{
	  scoped_preheader = loop_region_preheader (fn, region, nullptr);
	  if (scoped_preheader && !planner_config_window_ok (region))
	    scoped_preheader = nullptr;
	}
      bool cc_scoped_ok = false;
      if (!scoped_preheader && desc.cc.active)
	{
	  basic_block cc_preheader = region.loop_body
	    ? loop_region_preheader (fn, region, nullptr) : nullptr;
	  if (!region.loop_body || cc_preheader)
	    {
	      rtx_insn *anchor = region.rows[0].enable
		? region.rows[0].enable : region.rows[0].insns[0];
	      cc_scoped_ok = planner_region_config_ownership_ok
		(region, cc_preheader, anchor, c);
	    }
	}
      if (!scoped_preheader && !cc_scoped_ok)
	{
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " config-ownership-unproven\n");
	  return false;
	}
      if (scoped_preheader && dump)
	fprintf (dump, "Macro-planner config-ownership: loop-scoped"
		 " window (preheader tail dominates every launch)\n");
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
	if (dump)
	  fprintf (dump, "Macro-planner formation-refusal:"
		   " cc-enable-unproved\n");
	return false;
      }

  /* A lane-predicated calendar needs the ambient all-lanes proof: the
     region's first row's local enable (WP8 relaxation from every-row:
     region members cannot write CC -- such rows refuse
     cc-template-unsupported at discovery -- so the entry lane state
     holds across every row), or, for a loop-body region whose enable
     was written once outside the loop, the proven preheader's trailing
     enable (whole-body ownership keeps it live across every trip).  */
  rtx_insn *enable_src = region.rows[0].enable;
  bool materialized_enable = false;
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
	      if (dump)
		fprintf (dump, "Macro-planner formation-refusal:"
			 " cc-enable-unproved\n");
	      return false;
	    }
	  enable_src = nullptr;	/* already in place; no copy */
	}
      else
	{
	  /* Materialized enable (WP10, superseding the WP9 first-row
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
	  if (!proof_restore)
	    {
	      if (dump)
		fprintf (dump, "Macro-planner formation-refusal:"
			 " all-lanes-proof-missing\n");
	      return false;
	    }
	  enable_src = proof_restore;
	  materialized_enable = true;
	}
    }

  /* Profitability.  Straight-line: every run independently amortizes
     the full configuration prefix (the frozen conservative-per-run
     discipline).  Loop body: the prefix is paid once in the preheader
     and weighted against the profile trip estimate.  */
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
    }
  else
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

  /* Emission deletes each row's typed Dst separator; that is only
     sound when the launch calendar absorbed the stride, or when the
     proven program keeps the separator in place (WP9: the CC-template
     programs re-emit it verbatim as the restore-visibility slot).  */
  for (const macro_row &row : region.rows)
    if (row.separator && !schedule.absorbed_stride && !desc.keep_separator)
      {
	if (dump)
	  fprintf (dump, "Macro-planner formation-refusal:"
		   " stride-not-absorbed\n");
	return false;
      }

  /* WP10 compact CC calendar: the absorbing explicit load's address
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
	  if (dump)
	    fprintf (dump, "Macro-planner formation-refusal:"
		     " stride-not-absorbed\n");
	  return false;
	}

  for (unsigned b = 0; b != run_begins.length (); ++b)
    {
      unsigned begin = run_begins[b];
      unsigned end = b + 1 == run_begins.length ()
	? region.rows.length () : run_begins[b + 1];
      emit_planner_run (region, schedule, desc, c, begin, end, b == 0,
			config_preheader, enable_src);
    }
  if (dump)
    fprintf (dump, "Macro-planner formed: rows=%u runs=%u%s%s\n",
	     region.rows.length (), run_begins.length (),
	     config_preheader ? " config=preheader" : "",
	     materialized_enable ? " lane-proof=materialized-enable" : "");
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
		/* A Layer-7 verification mismatch is a descriptor
		   refusal: the candidate is unproven and must never
		   reach form_region.  */
		const char *verify_fail = nullptr;
		if (riscv_tt_macro_planner_verify || flag_checking)
		  verify_fail
		    = rvtt_macro_verify_descriptor (region, schedule,
						    descriptor, dump_file);
		proven = !descriptor.refusal && !verify_fail;
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
