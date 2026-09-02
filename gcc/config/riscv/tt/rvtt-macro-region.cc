/* Macro-planner region discovery over typed effect sets (Layer 2).
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
#include "insn-config.h"
#include "insn-codes.h"
#include "insn-attr.h"
#include "recog.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "basic-block.h"
#include "cfgrtl.h"
#include "df.h"
#include "tm_p.h"
#include "rvtt.h"
#include "rvtt-protos.h"
#include "rvtt-refuse.h"
#include "rvtt-effects.h"
#include "rvtt-macro-region.h"

/* Every classification below queries the typed effect vocabulary of
   rvtt-effects.h.  There is no opcode, operation-name, or raw-encoding
   recognition anywhere in this file; unaudited instructions arrive as
   opaque effect sets and act as region boundaries.  */

const char *
macro_region_refusal_name (macro_region_refusal r)
{
  switch (r)
    {
    case macro_region_refusal::row_opaque_effect:
      return "row-opaque-effect";
    case macro_region_refusal::row_not_closed:
      return "row-not-closed";
    case macro_region_refusal::row_cc_template_unsupported:
      return "cc-template-unsupported";
    case macro_region_refusal::row_config_write:
      return "row-config-write";
    case macro_region_refusal::row_not_isomorphic:
      return "row-not-isomorphic";
    case macro_region_refusal::row_stride_mismatch:
      return "row-stride-mismatch";
    case macro_region_refusal::row_live_through:
      return "row-live-through";
    case macro_region_refusal::row_cc_enable_unproved:
      return "cc-enable-unproved";
    }
  gcc_unreachable ();
}

namespace {

/* A pure ambient CC write (lane-enable shape): only a CC write, no
   other architectural effect.  Whether such a write may serve as an
   ambient all-lanes enable is decided by its lane-state proof
   (cc_write_all_lanes) at the acceptance site below: proven writes
   become pending enables, unproved ones are named refusals and hard
   region boundaries.  */
static bool
pure_cc_write_p (const xtt_effect_set &e)
{
  return !e.opaque && e.cc_write && !e.cc_read
    && !e.lreg_read && !e.lreg_write
    && !e.config_dests_written && !e.addr_mod_slot_write
    && !e.dst_mem_read && !e.dst_mem_write
    && e.rwc.kind == xtt_rwc_effect_t::NONE;
}

/* A pure typed Dst/RWC counter effect: the row- and run-separator class
   of Sec. 2.2 (typed increment, set, or face advance; nothing else).  */
static bool
pure_rwc_p (const xtt_effect_set &e)
{
  return !e.opaque
    && (e.rwc.kind == xtt_rwc_effect_t::INC
	|| e.rwc.kind == xtt_rwc_effect_t::SET
	|| e.rwc.kind == xtt_rwc_effect_t::FACE)
    && !e.lreg_read && !e.lreg_write
    && !e.cc_read && !e.cc_write
    && !e.config_dests_written && !e.addr_mod_slot_write
    && !e.dst_mem_read && !e.dst_mem_write;
}

/* Prove that hard register VALUE has no use after START before an
   all-lane definition kills it (same proof idiom as the existing late
   passes; the trailing query consults DF live-out).  */
static bool
value_dead_after_p (rtx value, rtx_insn *start)
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

/* Structural self-loop marker: loop metadata is unavailable this late,
   so a block that is its own successor is the v1 loop-body shape.  */
static bool
self_loop_bb_p (basic_block bb)
{
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      return true;
  return false;
}

class region_scanner
{
public:
  region_scanner (FILE *dump, vec<macro_region> *out)
    : dump_ (dump), out_ (out) {}
  void scan_bb (basic_block bb);

private:
  void refuse (macro_region_refusal r)
  {
    rvtt_refuse_by_name (macro_region_refusal_name (r), dump_,
			 "Macro-planner refusal: %s\n",
			 macro_region_refusal_name (r));
  }

  bool close_row (basic_block bb);
  bool isomorphic_to_first (macro_row &row);
  void finalize_region (basic_block bb);
  void reset_region ()
  {
    for (macro_row &row : rows_)
      {
	row.insns.release ();
	row.vmap.release ();
      }
    rows_.truncate (0);
    run_separators_.truncate (0);
    runs_ = 1;
    runs_marker_ = false;
    new_run_pending_ = false;
    pending_enable_ = nullptr;
    stride_ok_ = true;
  }

  FILE *dump_;
  vec<macro_region> *out_;
  auto_vec<rtx_insn *> span_;
  auto_vec<xtt_effect_set> span_effects_;
  auto_vec<macro_row> rows_;
  auto_vec<rtx_insn *> run_separators_;
  unsigned runs_ = 1;
  bool runs_marker_ = false;
  bool new_run_pending_ = false;
  rtx_insn *pending_enable_ = nullptr;
  bool stride_ok_ = true;
};

/* Dataflow closure of the current span: walking backward from the store,
   an instruction is a member when it defines an LREG some later member
   reads.  Every instruction in the span must be a member -- the row is a
   closed slice, not a window.  */

bool
region_scanner::close_row (basic_block bb)
{
  gcc_assert (!span_.is_empty ());

  /* Effect legality inside the row.  A CC-writing member is admitted in
     exactly two structural roles (the CC-template extension); whether a
     proven CC-template program realizes them is a descriptor question:
       - a predicate DEFINITION: a value event that reads LREGs, writes
	 CC, and produces NO LREG result (the typed
	 SFPSETCC-on-register class; a CC-writing event that also
	 produces a value -- the SFPIADD CC-mod class -- has no proven
	 dual-effect template and keeps the refusal);
       - the row-end all-lanes RESTORE: a pure CC write whose written
	 state is provably the all-lanes enable (cc_write_all_lanes,
	 word-exact through the one shared SFPENCC derivation).
     Anything else keeps the named refusal: a partial-lane or otherwise
     unproved pure CC write refuses cc-enable-unproved; every other CC
     writer still needs a CC-manipulating template no proven program
     provides.  */
  bool cc_def_seen = false;
  for (const xtt_effect_set &e : span_effects_)
    {
      if (e.cc_write)
	{
	  bool cc_def = e.lreg_read != 0 && !e.lreg_write
	    && !pure_cc_write_p (e);
	  bool cc_restore = cc_def_seen && pure_cc_write_p (e)
	    && e.cc_write_all_lanes;
	  if (!cc_def && !cc_restore)
	    {
	      refuse (pure_cc_write_p (e)
		      ? macro_region_refusal::row_cc_enable_unproved
		      : macro_region_refusal::row_cc_template_unsupported);
	      return false;
	    }
	  cc_def_seen |= cc_def;
	}
      if (e.config_dests_written || e.addr_mod_slot_write)
	{
	  refuse (macro_region_refusal::row_config_write);
	  return false;
	}
    }

  /* Backward slice from the store, over LREG and CC dataflow edges.  A
     CC-reading member depends on the nearest preceding in-row CC write
     (definition or restore); a CC-need surviving past the first span
     instruction is the row's dependency on the AMBIENT lane state --
     the sanctioned all-lanes-enable obligation every lane-predicated
     row already carries, never a closure violation.  */
  unsigned n = span_.length ();
  auto_vec<bool> member (n);
  member.safe_grow_cleared (n);
  member[n - 1] = true;
  uint32_t needed = span_effects_[n - 1].lreg_read;
  bool cc_needed = span_effects_[n - 1].cc_read;
  for (unsigned ix = n - 1; ix-- > 0;)
    {
      const xtt_effect_set &e = span_effects_[ix];
      if ((e.lreg_write & needed) || (cc_needed && e.cc_write))
	{
	  member[ix] = true;
	  needed &= ~e.lreg_write;
	  needed |= e.lreg_read;
	  if (e.cc_write)
	    cc_needed = false;
	  cc_needed |= e.cc_read;
	}
    }
  /* Every span instruction must be a member and every input must be
     produced inside the row (its loads feed its store): rows with
     external LREG inputs are a live-in shape this v1 does not admit.  */
  bool closed = !needed;
  for (unsigned ix = 0; ix != n; ++ix)
    if (!member[ix])
      closed = false;
  if (!closed)
    {
      refuse (macro_region_refusal::row_not_closed);
      return false;
    }

  macro_row row;
  row.insns = vNULL;
  row.vmap = vNULL;
  for (rtx_insn *insn : span_)
    row.insns.safe_push (insn);
  row.separator = nullptr;
  row.dst_delta = 0;
  row.imm_delta = 0;
  row.enable = pending_enable_;
  pending_enable_ = nullptr;
  row.starts_run = false;

  if (!rows_.is_empty () && !isomorphic_to_first (row))
    {
      refuse (macro_region_refusal::row_not_isomorphic);
      row.insns.release ();
      row.vmap.release ();
      /* The rows collected so far still form a region; the offending
	 row starts fresh discovery.  */
      finalize_region (bb);
      return false;
    }

  if (rows_.is_empty ())
    row.starts_run = true;
  else if (new_run_pending_)
    row.starts_run = true;
  new_run_pending_ = false;
  rows_.safe_push (row);
  return true;
}

/* Typed Dst-address operand position of the two admitted Dst access
   patterns -- post-admission positional access, the
   rvtt_dst_access_operands precedent (rvtt-effects.cc): the effect
   class has already admitted the instruction; reaching its typed
   operands by recognized code is the permitted use of code
   comparisons.  -1 when INSN is not an admitted typed Dst access.  */

static int
dst_address_operand_pos (rtx_insn *insn)
{
  int code = recog_memoized (insn);
  if (code == CODE_FOR_rvtt_sfpload_lv_int)
    return 4;
  if (code == CODE_FOR_rvtt_sfpstore_int)
    return 3;
  return -1;
}

/* Pairwise isomorphism against rows[0] under a value map: identical
   pattern structure position by position, identical constant operands,
   and a consistent renaming of register operands.

   Immediate Dst-address deltas: the loop
   fusion passes (gimple rvtt_dst_iteration and kin) carry part of the
   per-row Dst advance in the address immediates -- consecutive rows
   differ ONLY in their typed Dst address constants, with the residual
   advance in a shared separator.  Such rows are admitted here when
   every typed Dst address in the row sits at ONE common constant delta
   from rows[0]'s (recorded as row.imm_delta); any other constant
   mismatch still refuses.  Whether the deltas form a uniform absolute
   progression -- and are therefore expressible through the absorbed-
   stride calendar -- is finalize_region's separate proof; rows
   admitted here that fail it refuse there by name.  */

bool
region_scanner::isomorphic_to_first (macro_row &row)
{
  const macro_row &first = rows_[0];
  if (row.insns.length () != first.insns.length ())
    return false;

  bool imm_delta_set = false;
  HOST_WIDE_INT imm_delta = 0;

  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      rtx_insn *a = first.insns[ix];
      rtx_insn *b = row.insns[ix];
      if (recog_memoized (a) != recog_memoized (b))
	return false;

      int addr_pos = dst_address_operand_pos (a);

      extract_insn (a);
      unsigned n_ops = recog_data.n_operands;
      rtx a_ops[MAX_RECOG_OPERANDS];
      for (unsigned op = 0; op != n_ops; ++op)
	a_ops[op] = recog_data.operand[op];
      extract_insn (b);
      if (recog_data.n_operands != (int) n_ops)
	return false;

      for (unsigned op = 0; op != n_ops; ++op)
	{
	  rtx x = a_ops[op];
	  rtx y = recog_data.operand[op];
	  /* The typed Dst address operand participates in the row
	     delta whether equal (delta 0) or offset: mixed deltas
	     within one row are never a renaming and refuse.  */
	  if (addr_pos >= 0 && op == (unsigned) addr_pos)
	    {
	      if (!CONST_INT_P (x) || !CONST_INT_P (y))
		return false;
	      HOST_WIDE_INT d = INTVAL (y) - INTVAL (x);
	      if (!imm_delta_set)
		{
		  imm_delta = d;
		  imm_delta_set = true;
		}
	      else if (d != imm_delta)
		return false;
	      continue;
	    }
	  if (rtx_equal_p (x, y))
	    continue;
	  /* Unallocated scratches compare by identity in rtx_equal_p;
	     for isomorphism two placeholders of one mode are equal.  */
	  if (GET_CODE (x) == SCRATCH && GET_CODE (y) == SCRATCH
	      && GET_MODE (x) == GET_MODE (y))
	    continue;
	  if (!REG_P (x) || !REG_P (y) || GET_MODE (x) != GET_MODE (y))
	    return false;
	  bool mapped = false;
	  for (const std::pair<rtx, rtx> &m : row.vmap)
	    if (REGNO (m.first) == REGNO (x))
	      {
		if (REGNO (m.second) != REGNO (y))
		  return false;
		mapped = true;
		break;
	      }
	  if (!mapped)
	    row.vmap.safe_push (std::make_pair (x, y));
	}
    }
  /* One common delta for the whole row (INT_MAX guard: the typed
     address field is architecturally narrow; anything outsized
     refuses).  */
  if (imm_delta_set
      && (imm_delta < INT_MIN / 2 || imm_delta > INT_MAX / 2))
    return false;
  row.imm_delta = imm_delta_set ? (int) imm_delta : 0;
  return true;
}

/* Close out the rows collected so far in BB as one region: prove the
   uniform typed stride (including the immediate-delta absolute
   progression) and whole-region row closure (no LREG lives past its
   row), then either push the region onto the output vector or refuse
   by name.  Always resets the scanner's per-region state.  */

void
region_scanner::finalize_region (basic_block bb)
{
  /* Single-row regions are admitted: a lone row is a complete
     dataflow-closed slice; whether one launch amortizes its
     configuration is a Layer-6 profitability question (straight-line
     single rows refuse there; loop bodies weigh the row by the trip
     estimate), never a discovery question.  */
  if (rows_.length () >= 1)
    {
      macro_region region;
      region.bb = bb;
      region.loop_body = self_loop_bb_p (bb);
      region.runs = runs_;
      region.first = rows_[0].insns[0];
      region.last = rows_.last ().separator
	? rows_.last ().separator : rows_.last ().insns.last ();
      region.internal_lregs = 0;
      memset (&region.net, 0, sizeof (region.net));

      /* Uniform typed stride across the row separators.  */
      int stride = 0;
      bool stride_uniform = stride_ok_;
      for (const macro_row &row : rows_)
	if (row.separator)
	  {
	    if (stride == 0)
	      stride = row.dst_delta;
	    else if (row.dst_delta != stride)
	      stride_uniform = false;
	  }

      /* Immediate-delta rows: absolute
	 progression proof.  Row k's absolute Dst advance -- the
	 separator deltas accumulated before it plus its own immediate
	 delta -- must equal k * S for one uniform S, and the region's
	 total separator advance must equal rows * S: the absorbed-
	 stride calendar the formation mandates for this shape advances
	 the counter by S per row, so the downstream counter state is
	 reproduced exactly.  Any other progression refuses by the
	 stride-mismatch name (fail-closed).  */
      region.imm_stride = 0;
      bool any_imm = false;
      for (const macro_row &row : rows_)
	any_imm |= row.imm_delta != 0;
      if (any_imm && stride_uniform)
	{
	  bool ok = rows_.length () >= 2;
	  HOST_WIDE_INT acc = 0;
	  HOST_WIDE_INT s = 0;
	  for (unsigned k = 0; ok && k != rows_.length (); ++k)
	    {
	      HOST_WIDE_INT abs_k = acc + rows_[k].imm_delta;
	      if (k == 0)
		ok = abs_k == 0;
	      else if (k == 1)
		{
		  s = abs_k;
		  ok = s != 0;
		}
	      else
		ok = abs_k == (HOST_WIDE_INT) k * s;
	      if (rows_[k].separator)
		acc += rows_[k].dst_delta;
	    }
	  if (ok)
	    ok = acc == (HOST_WIDE_INT) rows_.length () * s
	      && s > INT_MIN / 2 && s < INT_MAX / 2;
	  if (!ok)
	    stride_uniform = false;
	  else
	    {
	      region.imm_stride = (int) s;
	      stride = (int) s;	/* the dump reports the proven advance */
	    }
	}

      /* Row closure over the whole region: every LREG a row defines is
	 dead after that row's last instruction (later isomorphic rows
	 kill their operands; the final query consults DF live-out).  */
      bool live_through = false;
      for (macro_row &row : rows_)
	{
	  uint32_t written = 0;
	  for (rtx_insn *insn : row.insns)
	    {
	      xtt_effect_set e = rvtt_insn_effects (insn);
	      written |= e.lreg_write;
	      region.net.lreg_write |= e.lreg_write;
	      region.net.lreg_read |= e.lreg_read;
	      region.net.dst_mem_read |= e.dst_mem_read;
	      region.net.dst_mem_write |= e.dst_mem_write;
	      region.net.cc_read |= e.cc_read;
	    }
	  region.internal_lregs |= written;
	  rtx_insn *tail = row.separator ? row.separator : row.insns.last ();
	  for (unsigned reg = 0; reg != 17 && !live_through; ++reg)
	    if ((written >> reg) & 1)
	      if (!value_dead_after_p
		    (gen_rtx_REG (XTT32SImode, SFPU_REG_FIRST + reg), tail))
		live_through = true;
	  if (live_through)
	    break;
	}

      bool clean = stride_uniform && !live_through;
      if (!stride_uniform)
	refuse (macro_region_refusal::row_stride_mismatch);
      else if (live_through)
	refuse (macro_region_refusal::row_live_through);
      if (clean && dump_)
	{
	  fprintf (dump_,
		   "Macro-planner region: rows=%u row-len=%u runs=%u"
		   " stride=%d%s loop=%s\n",
		   rows_.length (), rows_[0].insns.length (), region.runs,
		   stride, region.imm_stride ? " (imm)" : "",
		   region.loop_body ? "yes" : "no");
	  fprintf (dump_, "Macro-planner row-subunits:");
	  static const char *const subunit_names[] = {
	    "none", "simple", "mad", "round", "load", "store", "cfg", "sync"
	  };
	  for (unsigned ix = 0; ix != rows_[0].insns.length (); ++ix)
	    {
	      xtt_effect_set e = rvtt_insn_effects (rows_[0].insns[ix]);
	      fprintf (dump_, "%s%s", ix ? "," : " ",
		       subunit_names[e.subunit]);
	    }
	  fprintf (dump_, "\nMacro-planner region-lregs: 0x%x\n",
		   region.internal_lregs);
	}
      if (clean && out_)
	{
	  /* Transfer row ownership to the collected region.  */
	  region.rows = vNULL;
	  region.run_separators = vNULL;
	  for (macro_row &row : rows_)
	    region.rows.safe_push (row);
	  for (rtx_insn *sep : run_separators_)
	    region.run_separators.safe_push (sep);
	  rows_.truncate (0);	/* handles moved; do not release */
	  out_->safe_push (region);
	}
    }
  reset_region ();
}

/* Scan basic block BB, classifying each insn by its typed effect set:
   audited value instructions extend the current row span (a Dst store
   closes the row), pure CC writes between rows become pending ambient
   enables when proven all-lanes, pure counter effects become row or
   run separators, and opaque effects, configuration writes, and
   unadmitted CC writers refuse by name and bound the region.  Collects
   completed regions through finalize_region.  */

void
region_scanner::scan_bb (basic_block bb)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;

      xtt_effect_set e = rvtt_insn_effects (insn);

      if (e.opaque)
	{
	  /* Opaque effects (unaudited instructions, asm, calls, scalar
	     code) are hard region boundaries.  Inside a row they are a
	     named refusal; between rows they simply close the region.  */
	  if (!span_.is_empty ())
	    {
	      refuse (macro_region_refusal::row_opaque_effect);
	      span_.truncate (0);
	      span_effects_.truncate (0);
	    }
	  finalize_region (bb);
	  continue;
	}

      if (span_.is_empty () && pure_cc_write_p (e))
	{
	  /* Ambient lane-enable between rows; not a region member.  It
	     may stand as the ambient all-lanes enable ONLY when the
	     written value is provably the all-lanes pattern
	     (cc_write_all_lanes, word-exact against the capability
	     table's architectural SFPENCC encoding).  An unproved pure
	     CC write -- partial lanes, complement, register-driven --
	     is outside the proven store/misc envelope: it can never be
	     an enable and invalidates any earlier one, so it refuses by
	     name and closes the region (rows already collected keep
	     their own proof and may still form).  */
	  if (e.cc_write_all_lanes)
	    {
	      pending_enable_ = insn;
	      continue;
	    }
	  refuse (macro_region_refusal::row_cc_enable_unproved);
	  finalize_region (bb);
	  continue;
	}

      if (pure_rwc_p (e))
	{
	  if (!span_.is_empty ())
	    {
	      /* A counter effect inside a row slice breaks closure.  */
	      refuse (macro_region_refusal::row_not_closed);
	      span_.truncate (0);
	      span_effects_.truncate (0);
	      finalize_region (bb);
	      continue;
	    }
	  if (!rows_.is_empty () && !rows_.last ().separator
	      && e.rwc.kind == xtt_rwc_effect_t::INC)
	    {
	      /* The row's own typed Dst increment.  */
	      rows_.last ().separator = insn;
	      rows_.last ().dst_delta = e.rwc.dst_delta;
	    }
	  else if (!rows_.is_empty ())
	    {
	      /* Additional pure-RWC effects separate runs inside one
		 region (typed increment, set, or face advance); the new
		 run is counted when its first row begins.  */
	      run_separators_.safe_push (insn);
	      runs_marker_ = true;
	    }
	  continue;
	}

      /* Configuration writers and CC-writing value events refuse at the
	 event itself, so shapes name their missing capability even
	 when later slice members are opaque: a config write can never
	 be a region member, and a CC writer would need a
	 CC-manipulating instruction template no proven program
	 provides.  */
      if (e.config_dests_written || e.addr_mod_slot_write)
	{
	  refuse (macro_region_refusal::row_config_write);
	  span_.truncate (0);
	  span_effects_.truncate (0);
	  /* A configuration write never changes lane state, so an
	     ambient enable already seen still proves the next row's
	     entry lanes (audited span members it absorbed only read
	     CC).  */
	  rtx_insn *enable = pending_enable_;
	  finalize_region (bb);
	  pending_enable_ = enable;
	  continue;
	}
      if (e.cc_write)
	{
	  /* CC-writing value events extend the span in their two
	     admitted structural roles (see close_row): a predicate
	     definition (reads LREGs, writes CC, no LREG result) or the
	     in-row all-lanes restore (pure, proven, and only AFTER a
	     definition -- a restore with nothing to restore is not the
	     select structure and keeps the established refusal).  A
	     mid-span pure CC write that is NOT the proven all-lanes
	     pattern is a partial-lane/unproved enable
	     (cc-enable-unproved); any other CC writer -- including the
	     value-producing SFPIADD CC-mod class, for which no proven
	     dual-effect template exists -- keeps the missing
	     CC-template refusal.  Both remain hard region
	     boundaries.  */
	  bool span_has_def = false;
	  for (const xtt_effect_set &se : span_effects_)
	    span_has_def |= se.cc_write && se.lreg_read != 0
	      && !se.lreg_write && !pure_cc_write_p (se);
	  bool cc_def = e.lreg_read != 0 && !e.lreg_write
	    && !pure_cc_write_p (e);
	  bool cc_restore = span_has_def && pure_cc_write_p (e)
	    && e.cc_write_all_lanes;
	  if (cc_def || cc_restore)
	    {
	      span_.safe_push (insn);
	      span_effects_.safe_push (e);
	      continue;
	    }
	  refuse (pure_cc_write_p (e)
		  ? macro_region_refusal::row_cc_enable_unproved
		  : macro_region_refusal::row_cc_template_unsupported);
	  span_.truncate (0);
	  span_effects_.truncate (0);
	  finalize_region (bb);
	  continue;
	}

      /* Ordinary audited value instruction: extend the row span.  */
      if (span_.is_empty () && runs_marker_)
	{
	  ++runs_;
	  runs_marker_ = false;
	  new_run_pending_ = true;
	}
      span_.safe_push (insn);
      span_effects_.safe_push (e);
      if (e.dst_mem_write)
	{
	  if (close_row (bb))
	    ;
	  span_.truncate (0);
	  span_effects_.truncate (0);
	}
    }

  if (!span_.is_empty ())
    {
      span_.truncate (0);
      span_effects_.truncate (0);
    }
  finalize_region (bb);
}

} /* anonymous namespace */

/* Discover the macro-candidate regions of FN into *OUT (rows of
   isomorphic dataflow-closed load-to-store slices with a uniform typed
   Dst stride), scanning every basic block independently.  Refusals and
   accepted regions are reported on DUMP.  OUT may be null (analysis
   only); collected regions own their vectors and are released with
   rvtt_macro_region_release.  */

void
rvtt_macro_regions_discover (function *fn, FILE *dump,
			     vec<macro_region> *out)
{
  region_scanner scanner (dump, out);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    scanner.scan_bb (bb);
}

/* Free the heap storage owned by REGION: every row's insn and
   value-map vectors, the row vector itself, and the run separators.  */

void
rvtt_macro_region_release (macro_region *region)
{
  for (macro_row &row : region->rows)
    {
      row.insns.release ();
      row.vmap.release ();
    }
  region->rows.release ();
  region->run_separators.release ();
}

/* Dump-only entry point: run region discovery over FN for its refusal
   and region reports on DUMP, collecting nothing.  */

void
rvtt_macro_region_analyze (function *fn, FILE *dump)
{
  rvtt_macro_regions_discover (fn, dump, nullptr);
}
