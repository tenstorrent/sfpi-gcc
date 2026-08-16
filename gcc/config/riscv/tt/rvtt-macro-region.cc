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
    case macro_region_refusal::row_cc_write:
      return "row-cc-write";
    case macro_region_refusal::row_config_write:
      return "row-config-write";
    case macro_region_refusal::row_not_isomorphic:
      return "row-not-isomorphic";
    case macro_region_refusal::row_stride_mismatch:
      return "row-stride-mismatch";
    case macro_region_refusal::row_live_through:
      return "row-live-through";
    }
  gcc_unreachable ();
}

namespace {

/* A pure ambient CC write (all-lanes enable shape): only a CC write, no
   other architectural effect.  Such instructions may appear between rows
   without splitting a run; whether the lane state is actually the
   all-lanes pattern is a Layer-5 ownership obligation, not a region
   question.  */
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
  region_scanner (FILE *dump) : dump_ (dump) {}
  void scan_bb (basic_block bb);

private:
  void refuse (macro_region_refusal r)
  {
    if (dump_)
      fprintf (dump_, "Macro-planner refusal: %s\n",
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
    stride_ok_ = true;
  }

  FILE *dump_;
  auto_vec<rtx_insn *> span_;
  auto_vec<xtt_effect_set> span_effects_;
  auto_vec<macro_row> rows_;
  auto_vec<rtx_insn *> run_separators_;
  unsigned runs_ = 1;
  bool runs_marker_ = false;
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

  /* Effect legality inside the row.  */
  for (const xtt_effect_set &e : span_effects_)
    {
      if (e.cc_write)
	{
	  refuse (macro_region_refusal::row_cc_write);
	  return false;
	}
      if (e.config_dests_written || e.addr_mod_slot_write)
	{
	  refuse (macro_region_refusal::row_config_write);
	  return false;
	}
    }

  /* Backward slice from the store.  */
  unsigned n = span_.length ();
  auto_vec<bool> member (n);
  member.safe_grow_cleared (n);
  member[n - 1] = true;
  uint32_t needed = span_effects_[n - 1].lreg_read;
  for (unsigned ix = n - 1; ix-- > 0;)
    {
      const xtt_effect_set &e = span_effects_[ix];
      if (e.lreg_write & needed)
	{
	  member[ix] = true;
	  needed &= ~e.lreg_write;
	  needed |= e.lreg_read;
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

  rows_.safe_push (row);
  return true;
}

/* Pairwise isomorphism against rows[0] under a value map: identical
   pattern structure position by position, identical constant operands,
   and a consistent renaming of register operands.  */

bool
region_scanner::isomorphic_to_first (macro_row &row)
{
  const macro_row &first = rows_[0];
  if (row.insns.length () != first.insns.length ())
    return false;

  for (unsigned ix = 0; ix != row.insns.length (); ++ix)
    {
      rtx_insn *a = first.insns[ix];
      rtx_insn *b = row.insns[ix];
      if (recog_memoized (a) != recog_memoized (b))
	return false;

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
  return true;
}

void
region_scanner::finalize_region (basic_block bb)
{
  if (rows_.length () >= 2)
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

      if (!stride_uniform)
	refuse (macro_region_refusal::row_stride_mismatch);
      else if (live_through)
	refuse (macro_region_refusal::row_live_through);
      else if (dump_)
	{
	  fprintf (dump_,
		   "Macro-planner region: rows=%u row-len=%u runs=%u"
		   " stride=%d loop=%s\n",
		   rows_.length (), rows_[0].insns.length (), region.runs,
		   stride, region.loop_body ? "yes" : "no");
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
    }
  reset_region ();
}

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
	/* Ambient lane-enable between rows; a Layer-5 obligation, not a
	   region member.  */
	continue;

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

      /* Ordinary audited value instruction: extend the row span.  */
      if (span_.is_empty () && runs_marker_)
	{
	  ++runs_;
	  runs_marker_ = false;
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

} // anonymous namespace

void
rvtt_macro_region_analyze (function *fn, FILE *dump)
{
  region_scanner scanner (dump);
  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    scanner.scan_bb (bb);
}
