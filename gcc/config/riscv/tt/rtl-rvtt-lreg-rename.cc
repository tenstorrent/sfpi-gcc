/* Break storage-induced false LREG dependences in capturable rows.
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

/* Post-allocation renaming of LREG def-use chains.

   The register allocator often packs unrelated short lifetimes into one
   LREG.  The stall-filling code in rtl-rvtt-schedule.cc then refuses to
   move a row member whose destination is also written by another member,
   although no value flows between the two lifetimes.  This pass breaks
   such false dependences by renaming, as gcc/regrename.cc does, but
   restricted to the eight SFPU LREGs, to single basic blocks, and using
   the typed effect tables (rvtt_insn_effects) rather than constraint
   queries.  A chain is one single-LREG definition plus every reader of it
   up to the close (the next write of the register, or block exit where
   the register is dead); the whole chain moves or nothing does.  Only
   register fields change, never the number of delivered words.

   The pass works in two modes.  As a service (rvtt_lreg_rename_chain),
   other passes request a rename, price it themselves and can undo it
   (rvtt_lreg_rename_web_undo).  Standalone, it greedily renames chains
   that collide with another write in the row, accepting a rename when
   the row's interlock timing (rvtt_timing::interlock_sim) is no worse.
   Standalone mode leaves chains in repeated instruction sequences alone,
   because renaming only some copies of an unrolled body would break up
   replay windows.

   A rename is refused if the chain touches CC, config, Dst or RWC state,
   a replay-owned or multi-result instruction, a fixed-LREG marker, or
   accesses the register implicitly (as LUT table slots do).  There must
   be no CC-state change inside the chain, so every reader sees the lanes
   the definition wrote and the close overwrites exactly those lanes;
   -mtt-tensix-optimize-rename-cc-region also accepts balanced
   SFPPUSHC/SFPPOPC frames that only narrow the enabled lanes, or a span
   entered with all lanes enabled (see rvtt-cc-region-rtl.cc).

   The target LREG must be untouched in the block and dead on entry and
   exit.  DF liveness does not see through calls, asm or raw .ttinsn
   words, so in functions containing them chains closed by block exit and
   block-free targets are refused.  With -mtt-tensix-optimize-rename-temporal
   a target only needs to be free across the chain itself, and its first
   later use must be a fresh definition with no CC change in between (or
   it must be dead at block exit).  Such a target is borrowed from a live
   register and can remove fill opportunities and break replay windows, so
   it must show a strict timing gain; under the current timing model that
   never happens, so only callers that own the row's whole delivery shape
   (RVTT_RENAME_SHAPE_OWNED) get temporal renames.

   After committing, the pass rescans the edited instructions and checks
   that the chain now uses only the target; any discrepancy reverts the
   rename (and asserts with -fchecking).  Renames add no upward-exposed
   uses and extend no live range across blocks, so DF liveness computed
   at pass entry stays valid.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "df.h"
#include "tree-pass.h"
#include "cfgrtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "tm_p.h"
#include "print-rtl.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-refuse.h"
#include "rvtt-timing.h"
#include "rvtt-cc-region.h"

/* From rvtt-pressure.cc; rvtt-pressure.h is a GIMPLE-only header.  */
extern unsigned rvtt_pressure_capacity ();

namespace {

struct row_member
{
  rtx_insn *insn;
  xtt_effect_set fx;
};

/* Return true if BB is a self-loop containing only SFPU instructions plus
   the counter step and jump, and collect the SFPU members with their
   effects in MEMBERS.  This matches what the stall-fill passes accept.
   Used only to prioritize candidates in standalone mode; *REASON is
   informational.  */

static bool
rename_row_p (basic_block bb, std::vector<row_member> *members,
	      const char **reason)
{
  *reason = nullptr;
  bool self = false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (e->dest == bb)
      self = true;
  if (!self)
    return false;

  bool saw_scalar = false;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (JUMP_P (insn))
	{
	  if (insn != BB_END (bb))
	    {
	      *reason = "control flow inside the row";
	      return false;
	    }
	  continue;
	}
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	{
	  *reason = "rename-span-opaque";
	  return false;
	}
      if (GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER)
	continue;
      if (recog_memoized (insn) >= 0 && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    continue; /* bookkeeping insn */
	  row_member m;
	  m.insn = insn;
	  m.fx = rvtt_insn_effects (insn);
	  if (m.fx.opaque)
	    {
	      *reason = "rename-member-effects-unaudited";
	      return false;
	    }
	  members->push_back (m);
	  continue;
	}
      rtx set = single_set (insn);
      if (saw_scalar || !set || !REG_P (SET_DEST (set))
	  || SFPU_REG_P (REGNO (SET_DEST (set)))
	  || contains_mem_rtx_p (PATTERN (insn)))
	{
	  *reason = "scalar payload beyond the counter";
	  return false;
	}
      saw_scalar = true;
    }
  return members->size () >= 2;
}

/* Return true if some member with result_latency > 0 is immediately
   followed by a member reading one of its destinations.  */

static bool
row_has_audited_stall_p (const std::vector<row_member> &members)
{
  for (size_t i = 0; i + 1 < members.size (); ++i)
    if (members[i].fx.result_latency > 0
	&& (members[i + 1].fx.lreg_read & members[i].fx.lreg_write))
      return true;
  return false;
}

/* Replace every use of hard reg OLDR with NEWR inside *LOC, adding the
   changes to the current validate_change group.  */

static void
queue_reg_replacements (rtx_insn *insn, rtx *loc, unsigned oldr, rtx newreg)
{
  rtx x = *loc;
  if (!x)
    return;
  if (REG_P (x))
    {
      if (REGNO (x) == oldr)
	validate_change (insn, loc, gen_rtx_REG (GET_MODE (x),
						 REGNO (newreg)), true);
      return;
    }
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	queue_reg_replacements (insn, &XEXP (x, i), oldr, newreg);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  queue_reg_replacements (insn, &XVECEXP (x, i, j), oldr, newreg);
    }
}

/* The def-use chain engine.  */

/* One scanned block position.  */

struct span_insn
{
  rtx_insn *insn;
  enum kind_t { SI_TENSIX, SI_MARKER, SI_SCALAR, SI_OPAQUE, SI_BOOKKEEPING } kind;
  xtt_effect_set fx;		/* SI_TENSIX only.  */
  uint32_t touch;		/* L0..L7 bits this position references.  */
};

/* Return the LREG bits (L0..L7) of every REG in X.  Used for insns
   whose effects are not described by the effect tables.  */

static uint32_t
sfpu_reg_mask (rtx x)
{
  if (!x)
    return 0;
  if (REG_P (x))
    return SFPU_REG_P (REGNO (x)) ? 1u << (REGNO (x) - SFPU_REG_FIRST) : 0;
  uint32_t mask = 0;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	mask |= sfpu_reg_mask (XEXP (x, i));
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  mask |= sfpu_reg_mask (XVECEXP (x, i, j));
    }
  return mask;
}

/* Return the number of occurrences of hard register REGNO in X.  */

static unsigned
count_reg_occurrences (rtx x, unsigned regno)
{
  if (!x)
    return 0;
  if (REG_P (x))
    return REGNO (x) == regno ? 1 : 0;
  unsigned n = 0;
  const char *fmt = GET_RTX_FORMAT (GET_CODE (x));
  for (int i = GET_RTX_LENGTH (GET_CODE (x)) - 1; i >= 0; --i)
    {
      if (fmt[i] == 'e')
	n += count_reg_occurrences (XEXP (x, i), regno);
      else if (fmt[i] == 'E')
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  n += count_reg_occurrences (XVECEXP (x, i, j), regno);
    }
  return n;
}

/* Classify every nondebug insn of BB in order.  */

static void
scan_block (basic_block bb, std::vector<span_insn> *out)
{
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      span_insn si;
      si.insn = insn;
      si.fx = xtt_effect_set ();
      si.touch = 0;
      uint32_t marker_mask = 0;
      if (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0)
	si.kind = span_insn::SI_OPAQUE;
      else if (GET_CODE (PATTERN (insn)) == USE
	       || GET_CODE (PATTERN (insn)) == CLOBBER)
	{
	  /* Bookkeeping; still record any LREG it mentions.  */
	  si.kind = span_insn::SI_SCALAR;
	  si.touch = sfpu_reg_mask (PATTERN (insn));
	}
      else if (rvtt_lreg_marker (insn, &marker_mask))
	{
	  /* Zero-length marker for a fixed-LREG interface; its registers
	     can never be renamed.  */
	  si.kind = span_insn::SI_MARKER;
	  si.touch = marker_mask & 0xFF;
	}
      else if (recog_memoized (insn) >= 0
	       && get_attr_type (insn) == TYPE_TENSIX)
	{
	  if (!get_attr_length (insn))
	    si.kind = span_insn::SI_BOOKKEEPING;	/* bookkeeping insn */
	  else
	    {
	      si.kind = span_insn::SI_TENSIX;
	      si.fx = rvtt_insn_effects (insn);
	      si.touch = (si.fx.lreg_read | si.fx.lreg_write) & 0xFF;
	    }
	  /* Also record LREGs the pattern mentions that the effects do
	     not cover.  */
	  si.touch |= sfpu_reg_mask (PATTERN (insn)) & 0xFF;
	}
      else
	{
	  si.kind = span_insn::SI_SCALAR;
	  si.touch = sfpu_reg_mask (PATTERN (insn)) & 0xFF;
	}
      out->push_back (si);
    }
}

/* Return true if the function contains an instruction whose register
   uses DF cannot see (a call, asm or raw .ttinsn word).  */

static bool
function_has_opaque_insn_p (function *fn)
{
  basic_block bb;
  rtx_insn *insn;
  FOR_EACH_BB_FN (bb, fn)
    FOR_BB_INSNS (bb, insn)
      if (NONDEBUG_INSN_P (insn)
	  && (CALL_P (insn) || asm_noperands (PATTERN (insn)) >= 0))
	return true;
  return false;
}

/* A chain that can be renamed.  */

struct chain_desc
{
  basic_block bb;
  std::vector<span_insn> scan;	/* the block scan used */
  size_t wi;			/* writer index into scan */
  std::vector<size_t> readers;	/* reader indices */
  size_t close;			/* closing write, or scan.size () */
  bool close_reads;		/* the close also reads the chain value
				   through OP_IN operands */
  int old_l, new_l;		/* L indices */
  bool temporal;		/* target is free only across the span,
				   not the whole block */
};

static unsigned n_chain_renamed;

/* Record refusal REASON for a chain in BB, blamed on insn AT, and print
   the dump line.  */

static void
refuse_chain (const char *reason, rtx_insn *at, basic_block bb)
{
  rvtt_refuse_by_name_at (reason, at, dump_file,
			  "Lreg chain rename refused: %s in bb %d\n",
			  reason, bb->index);
}

/* Return true if every reference of W to hard reg OLDR in its effects
   is an explicit REG in its pattern, so it can be edited.  This rejects
   implicit accesses such as LUT table slots.  */

static bool
explicit_operand_p (const span_insn &si, unsigned oldr)
{
  return count_reg_occurrences (PATTERN (si.insn), oldr) > 0;
}

/* Analyze the chain of the single-LREG definition at SCAN[WI] in BB.
   On success fill *CH (which takes a copy of SCAN) and return true;
   otherwise record a refusal and return false.  FN_HAS_OPAQUE disables
   chains closed by block exit.  */

static bool
analyze_chain (basic_block bb, const std::vector<span_insn> &scan,
	       size_t wi, bool fn_has_opaque, int target_l,
	       chain_desc *ch)
{
  const span_insn &w = scan[wi];
  gcc_assert (w.kind == span_insn::SI_TENSIX);

  /* Reject writers with effects the rename cannot handle.  */
  if (w.fx.opaque)
    {
      refuse_chain ("regrename-span-opaque", w.insn, bb);
      return false;
    }
  if (popcount_hwi (w.fx.lreg_write) != 1
      || (w.fx.lreg_write & ~0xFFu))
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  uint32_t oldbit = w.fx.lreg_write;
  int old_l = exact_log2 (oldbit);
  if (old_l < 0 || old_l >= (int) SFPU_REG_NUM)
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  unsigned oldr = SFPU_REG_FIRST + old_l;
  if (w.fx.cc_write || w.fx.config_dests_written || w.fx.addr_mod_slot_write
      || w.fx.rwc.kind != xtt_rwc_effect_t::NONE || w.fx.dst_mem_write)
    {
      refuse_chain ("regrename-effect-veto", w.insn, bb);
      return false;
    }
  xtt_multiresult_group grp;
  if (rvtt_multiresult_group (w.insn, w.fx, &grp))
    {
      refuse_chain ("regrename-companion-coupled", w.insn, bb);
      return false;
    }
  if (get_attr_xtt_replay (w.insn) == XTT_REPLAY_OWNER)
    {
      refuse_chain ("regrename-replay-boundary", w.insn, bb);
      return false;
    }
  /* A def whose live-value merge source (operand 1; the plain forms have
     a noval marker there) is its own destination merges with the prior
     value of that register on disabled lanes, so it cannot be renamed.
     Other reads of the destination register are reads of its previous
     live range (e.g. p = mul (x, x) allocated to x's register); only the
     writer's outputs are edited, so those reads stay correct.  Patterns
     with a "0"-tied source fail re-recognition (regrename-constraint).  */
  if (w.fx.lreg_read & oldbit)
    {
      extract_insn (w.insn);
      for (int oi = 1; oi < recog_data.n_operands; ++oi)
	{
	  rtx op = recog_data.operand[oi];
	  if (REG_P (op) && REGNO (op) == oldr
	      && recog_data.operand_type[oi] == OP_IN
	      && oi == 1)
	    {
	      refuse_chain ("regrename-self-merge", w.insn, bb);
	      return false;
	    }
	}
    }
  if (!explicit_operand_p (w, oldr))
    {
      refuse_chain ("regrename-implicit-operand", w.insn, bb);
      return false;
    }

  /* Walk the span: collect readers up to the close.  */
  std::vector<size_t> readers;
  size_t close = scan.size ();
  bool close_reads = false;
  /* First CC-state change inside the span, when
     -mtt-tensix-optimize-rename-cc-region lets the walk continue past
     it.  */
  rtx_insn *first_cc = nullptr;
  for (size_t i = wi + 1; i < scan.size (); ++i)
    {
      const span_insn &si = scan[i];
      switch (si.kind)
	{
	case span_insn::SI_OPAQUE:
	  refuse_chain ("regrename-span-opaque", si.insn, bb);
	  return false;
	case span_insn::SI_MARKER:
	  if (si.touch & oldbit)
	    {
	      refuse_chain ("regrename-pinned-protocol", si.insn, bb);
	      return false;
	    }
	  continue;
	case span_insn::SI_SCALAR:
	case span_insn::SI_BOOKKEEPING:
	  if (si.touch & oldbit)
	    {
	      refuse_chain ("regrename-pinned-protocol", si.insn, bb);
	      return false;
	    }
	  continue;
	case span_insn::SI_TENSIX:
	  break;
	}
      if (si.fx.opaque)
	{
	  refuse_chain ("regrename-span-opaque", si.insn, bb);
	  return false;
	}
      if (si.fx.lreg_write & oldbit)
	{
	  /* The close.  The allocator often gives a consumer the register of
	     a dying source, so the close may both read the chain value
	     and overwrite the register.  A plain OP_IN read can be split:
	     only the input operands move to the target.  A read through the
	     destination itself (OP_INOUT) cannot be split.  */
	  extract_insn (si.insn);
	  for (int oi = 0; oi < recog_data.n_operands; ++oi)
	    {
	      rtx op = recog_data.operand[oi];
	      if (REG_P (op) && REGNO (op) == oldr
		  && recog_data.operand_type[oi] == OP_INOUT)
		{
		  refuse_chain ("regrename-tied-close", si.insn, bb);
		  return false;
		}
	      if (REG_P (op) && REGNO (op) == oldr
		  && recog_data.operand_type[oi] == OP_IN)
		close_reads = true;
	    }
	  close = i;
	  break;
	}
      if (si.fx.cc_write)
	{
	  /* A CC change inside the span: a later reader may see lanes the
	     writer did not write, and a masked close would overwrite only
	     some lanes.  Without -mtt-tensix-optimize-rename-cc-region,
	     refuse (after classifying the span for the dump).  With it,
	     keep walking and let the CC-region view decide once the close
	     is known.  */
	  if (!riscv_tt_opt_rename_cc_region)
	    {
	      if (dump_file)
		{
		  /* Find the close only to report the CC-region view's
		     classification in the dump.  */
		  size_t c = scan.size ();
		  for (size_t j = i; j < scan.size (); ++j)
		    if (scan[j].kind == span_insn::SI_TENSIX
			&& (scan[j].fx.lreg_write & oldbit))
		      {
			c = j;
			break;
		      }
		  rvtt_cc_rtl_span_verdict v
		    = rvtt_cc_rtl_classify_span
			(bb, w.insn,
			 c == scan.size () ? nullptr : scan[c].insn,
			 c != scan.size ());
		  fprintf (dump_file,
			   "Lreg chain rename cc-span rtl-view: %s"
			   " in bb %d\n",
			   rvtt_cc_rtl_span_verdict_name (v), bb->index);
		}
	      refuse_chain ("regrename-cc-span", si.insn, bb);
	      return false;
	    }
	  if (!first_cc)
	    first_cc = si.insn;
	}
      if (si.fx.lreg_read & oldbit)
	{
	  if (get_attr_xtt_replay (si.insn) == XTT_REPLAY_OWNER)
	    {
	      refuse_chain ("regrename-replay-boundary", si.insn, bb);
	      return false;
	    }
	  if (!explicit_operand_p (si, oldr))
	    {
	      refuse_chain ("regrename-implicit-operand", si.insn, bb);
	      return false;
	    }
	  readers.push_back (i);
	}
    }
  if (first_cc)
    {
      /* The span contains CC changes: ask the CC-region view about the
	 whole span (writer to close, or to block end).  With a kill close
	 the CC state at the end must equal that at entry, so the close
	 overwrites exactly the lanes the definition wrote.  */
      gcc_assert (riscv_tt_opt_rename_cc_region);
      rvtt_cc_rtl_span_verdict v
	= rvtt_cc_rtl_classify_span (bb, w.insn,
				     close == scan.size ()
				     ? nullptr : scan[close].insn,
				     close != scan.size ());
      if (dump_file)
	fprintf (dump_file,
		 "Lreg chain rename cc-span rtl-view: %s in bb %d\n",
		 rvtt_cc_rtl_span_verdict_name (v), bb->index);
      /* The walk saw a CC change, so the view cannot report NO_EVENT.
	 Checked at run time under flag_checking so that it also runs in
	 release-checking builds.  */
      if (flag_checking)
	gcc_assert (v != RVTT_CC_RTL_SPAN_NO_EVENT);
      if (!rvtt_cc_rtl_span_admissible_p (v))
	{
	  refuse_chain ("regrename-cc-span-region-unproven", first_cc, bb);
	  return false;
	}
    }
  else if (flag_checking)
    {
      /* Under flag_checking, a span without CC changes must be NO_EVENT
	 in the CC-region view too; otherwise dump, refuse and assert.  */
      rvtt_cc_rtl_span_verdict v
	= rvtt_cc_rtl_classify_span (bb, w.insn,
				     close == scan.size ()
				     ? nullptr : scan[close].insn,
				     close != scan.size ());
      if (v != RVTT_CC_RTL_SPAN_NO_EVENT)
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "Lreg chain rename cc-span rtl-view SHADOW"
		     " DIVERGENCE: %s in bb %d\n",
		     rvtt_cc_rtl_span_verdict_name (v), bb->index);
	  refuse_chain ("regrename-cc-span", w.insn, bb);
	  gcc_assert (!"cc-span rtl-view shadow divergence");
	  return false;
	}
    }

  if (close == scan.size ())
    {
      /* No write closes the chain inside the block: accept only if the
	 register is dead at block exit and DF can be trusted (no opaque
	 instruction in the function).  The span then extends to the block
	 end.  */
      if (fn_has_opaque
	  || REGNO_REG_SET_P (df_get_live_out (bb), oldr))
	{
	  refuse_chain ("regrename-chain-open", w.insn, bb);
	  return false;
	}
    }

  /* Target selection: the lowest-numbered LREG that no position in the
     block touches and that is neither live into nor out of the block.  */
  uint32_t block_touch = 0;
  for (const span_insn &si : scan)
    block_touch |= si.touch;
  unsigned nlreg = SFPU_REG_NUM;
  if (rvtt_pressure_capacity () < nlreg)
    nlreg = rvtt_pressure_capacity ();
  int new_l = -1;
  bool temporal = false;
  /* Block-free targets.  Their deadness at block entry and exit comes
     from DF, which cannot be trusted in a function with opaque
     instructions: there an untouched register may carry a value around a
     loop without DF seeing it.  So admit no block-free target in such a
     function, but remember that one existed for the dump; a temporal
     target may still be found.  */
  bool liveness_untrusted_block = false;
  for (unsigned l = 0; l < nlreg; ++l)
    {
      if (target_l >= 0 && (int) l != target_l)
	continue;
      uint32_t bit = 1u << l;
      if (block_touch & bit)
	continue;
      if (fn_has_opaque)
	{
	  /* An untouched register exists, but its deadness cannot be
	     trusted.  */
	  liveness_untrusted_block = true;
	  continue;
	}
      if (REGNO_REG_SET_P (df_get_live_in (bb), SFPU_REG_FIRST + l)
	  || REGNO_REG_SET_P (df_get_live_out (bb), SFPU_REG_FIRST + l))
	continue;
      new_l = l;
      break;
    }
  /* A writer that reads its own destination register (p = op (x, ...)
     with p allocated to x's register) keeps reading the old register
     after the rename, while its write moves.  The old register then
     stays live from the writer to the close.  Temporal targets are
     checked only against each chain's own span, so a later temporal
     chain could pick the old register and clobber that value.  Refuse
     temporal targets for such writers; block-free targets are dead
     throughout the block and remain safe.  */
  if (new_l < 0 && riscv_tt_opt_rename_temporal
      && (w.fx.lreg_read & oldbit))
    {
      refuse_chain ("regrename-temporal-dest-reuse", w.insn, bb);
      return false;
    }
  if (new_l < 0 && riscv_tt_opt_rename_temporal)
    {
      /* Temporal targets: the lowest-numbered register that is free
	 across the span and whose later uses cannot see the chain
	 value.  */
      size_t span_end = close == scan.size () ? scan.size () - 1 : close;
      for (unsigned l = 0; l < nlreg; ++l)
	{
	  if (target_l >= 0 && (int) l != target_l)
	    continue;
	  uint32_t bit = 1u << l;
	  if (bit & oldbit)
	    continue;
	  /* Untouched across the span (writer through close, or to the
	     block end for a chain closed by block exit).  */
	  bool span_clear = true;
	  for (size_t i = wi; i <= span_end && span_clear; ++i)
	    span_clear = !(scan[i].touch & bit);
	  if (!span_clear)
	    continue;
	  /* A fixed-LREG marker anywhere in the block rules it out.  */
	  bool pinned = false;
	  for (const span_insn &si : scan)
	    if (si.kind == span_insn::SI_MARKER && (si.touch & bit))
	      pinned = true;
	  if (pinned)
	    continue;
	  /* The first later touch must be a Tensix instruction with known
	     effects that writes the register without reading it, with no
	     CC change before it; if there is no later touch the register
	     must be dead at block exit and DF must be trusted.  */
	  size_t first_post = scan.size ();
	  for (size_t i = span_end + 1; i < scan.size (); ++i)
	    if (scan[i].touch & bit)
	      {
		first_post = i;
		break;
	      }
	  if (first_post != scan.size ())
	    {
	      const span_insn &p = scan[first_post];
	      if (p.kind != span_insn::SI_TENSIX || p.fx.opaque
		  || !(p.fx.lreg_write & bit) || (p.fx.lreg_read & bit))
		continue;
	      bool gap_clear = true;
	      for (size_t i = span_end; i < first_post && gap_clear; ++i)
		{
		  if (scan[i].kind == span_insn::SI_OPAQUE)
		    gap_clear = false;
		  else if (scan[i].kind == span_insn::SI_TENSIX
			   && (scan[i].fx.opaque || scan[i].fx.cc_write))
		    gap_clear = false;
		}
	      if (!gap_clear)
		continue;
	    }
	  else
	    {
	      /* Live in but untouched and dead out should not happen, but
		 refuse it anyway.  */
	      if (fn_has_opaque
		  || REGNO_REG_SET_P (df_get_live_in (bb),
				      SFPU_REG_FIRST + l)
		  || REGNO_REG_SET_P (df_get_live_out (bb),
				      SFPU_REG_FIRST + l))
		continue;
	    }
	  new_l = l;
	  temporal = true;
	  break;
	}
    }
  if (new_l < 0)
    {
      /* Report regrename-liveness-untrusted if an untouched register
	 existed but DF could not be trusted, otherwise no free LREG.  */
      refuse_chain (liveness_untrusted_block
		    ? "regrename-liveness-untrusted"
		    : "regrename-no-free-lreg", w.insn, bb);
      return false;
    }

  ch->bb = bb;
  ch->scan = scan;
  ch->wi = wi;
  ch->readers = readers;
  ch->close = close;
  ch->close_reads = close_reads;
  ch->old_l = old_l;
  ch->new_l = new_l;
  ch->temporal = temporal;
  return true;
}

/* Return true if renaming CH does not make the row slower, comparing
   interlocked issue slots before and after the rename with the timing
   model.  For a block-free target the window is the span (writer to
   close) and the rename must be no worse.  For a temporal target the
   window extends to the target's first later touch, which the rename
   now depends on, and the rename must be strictly better.  Refuse if
   the window cannot be priced.  */

static bool
span_no_worse_p (const chain_desc &ch)
{
  uint32_t oldbit = 1u << ch.old_l;
  uint32_t newbit = 1u << ch.new_l;
  size_t end = ch.close == ch.scan.size () ? ch.scan.size () : ch.close + 1;
  /* For a temporal target, extend the window through the target's
     first later touch.  */
  if (ch.temporal && ch.close != ch.scan.size ())
    for (size_t i = ch.close + 1; i < ch.scan.size (); ++i)
      if (ch.scan[i].touch & newbit)
	{
	  end = i + 1;
	  break;
	}
  int64_t slots[2] = { 0, 0 };
  for (int world = 0; world < 2; ++world)
    {
      rvtt_timing::interlock_sim sim;
      for (size_t i = ch.wi; i < end; ++i)
	{
	  const span_insn &si = ch.scan[i];
	  if (si.kind != span_insn::SI_TENSIX)
	    continue;
	  uint32_t read = si.fx.lreg_read;
	  uint32_t write = si.fx.lreg_write;
	  if (world == 1)
	    {
	      if (i == ch.wi)
		write = (write & ~oldbit) | newbit;
	      else if (ch.close_reads && i == ch.close)
		read = (read & ~oldbit) | newbit;
	      else
		for (size_t r : ch.readers)
		  if (r == i)
		    read = (read & ~oldbit) | newbit;
	    }
	  rvtt_timing::issue_op op;
	  op.deps = (read | write) & 0xFFFF;
	  op.writes = write & 0xFFFF;
	  op.words = get_attr_length (si.insn) / 4;
	  op.lat = si.fx.result_latency;
	  op.next_slot_stall = si.fx.next_slot_stall;
	  if (!sim.step (op))
	    {
	      refuse_chain ("regrename-row-unpriceable",
			    si.insn, ch.bb);
	      return false;
	    }
	}
      slots[world] = sim.slots ();
    }
  if (slots[1] > slots[0])
    {
      refuse_chain ("regrename-cost-regressed",
		    ch.scan[ch.wi].insn, ch.bb);
      return false;
    }
  /* Temporal targets must show a strict gain (see the file
     header).  */
  if (ch.temporal && slots[1] == slots[0])
    {
      refuse_chain ("regrename-temporal-no-modeled-gain",
		    ch.scan[ch.wi].insn, ch.bb);
      return false;
    }
  return true;
}

/* Return true if the part of the chain the rename would edit (writer to
   last edited position) is a sequence of Tensix words that occurs again
   in the block.  Standalone mode does not rename such chains (see the
   file header).  Using the edited part rather than the whole span
   matters when the close is the first word of the next repetition or
   the span runs into unique code at the block end.  Words are compared
   by INSN_CODE and delivered_pattern_equal_p, and only Tensix
   instructions are considered since scalar code is not recorded.  A
   repetition may overlap the window.  Single-word windows never
   count.  */

/* Return true if X and Y deliver the same word: like rtx_equal_p, but
   SCRATCHes of the same mode are equal.  A scratch clobber emits no word,
   and every Tensix load/store pattern has one, so rtx_equal_p would never
   find two equal loads.  */

static bool
delivered_pattern_equal_p (const_rtx x, const_rtx y)
{
  if (x == y)
    return true;
  if (!x || !y)
    return false;
  enum rtx_code code = GET_CODE (x);
  if (code != GET_CODE (y) || GET_MODE (x) != GET_MODE (y))
    return false;
  switch (code)
    {
    case SCRATCH:
      return true;
    case REG:
      return REGNO (x) == REGNO (y);
    case LABEL_REF:
      return label_ref_label (x) == label_ref_label (y);
    case SYMBOL_REF:
      return XSTR (x, 0) == XSTR (y, 0);
    default:
      break;
    }
  const char *fmt = GET_RTX_FORMAT (code);
  for (int i = GET_RTX_LENGTH (code) - 1; i >= 0; --i)
    switch (fmt[i])
      {
      case 'e':
	if (!delivered_pattern_equal_p (XEXP (x, i), XEXP (y, i)))
	  return false;
	break;
      case 'E':
	if (XVECLEN (x, i) != XVECLEN (y, i))
	  return false;
	for (int j = XVECLEN (x, i) - 1; j >= 0; --j)
	  if (!delivered_pattern_equal_p (XVECEXP (x, i, j),
					  XVECEXP (y, i, j)))
	    return false;
	break;
      case 'i':
      case 'n':
	if (XINT (x, i) != XINT (y, i))
	  return false;
	break;
      case 'w':
	if (XWINT (x, i) != XWINT (y, i))
	  return false;
	break;
      case 's':
	if (strcmp (XSTR (x, i), XSTR (y, i)) != 0)
	  return false;
	break;
      case '0':
	break;
      default:
	/* Unknown operand kind: treat as unequal, which can only
	   allow a rename, never make one unsafe.  */
	return false;
      }
  return true;
}

static bool
chain_in_periodic_window_p (const chain_desc &ch)
{
  /* Last position the commit would edit.  */
  size_t edit_end = ch.wi;
  if (!ch.readers.empty ())
    edit_end = ch.readers.back ();
  if (ch.close_reads && ch.close != ch.scan.size () && ch.close > edit_end)
    edit_end = ch.close;
  /* The block's Tensix instructions in order, with the first and last
     edited ones located.  */
  std::vector<size_t> tensix;
  size_t s0 = SIZE_MAX, send = SIZE_MAX;
  for (size_t i = 0; i < ch.scan.size (); ++i)
    if (ch.scan[i].kind == span_insn::SI_TENSIX)
      {
	if (i == ch.wi)
	  s0 = tensix.size ();
	if (i >= ch.wi && i <= edit_end)
	  send = tensix.size ();
	tensix.push_back (i);
      }
  gcc_assert (s0 != SIZE_MAX && send != SIZE_MAX && send >= s0);
  size_t len = send - s0 + 1;
  if (len < 2)
    return false;
  auto same_word = [&] (size_t a, size_t b)
    {
      rtx_insn *x = ch.scan[tensix[a]].insn;
      rtx_insn *y = ch.scan[tensix[b]].insn;
      return INSN_CODE (x) == INSN_CODE (y)
	     && delivered_pattern_equal_p (PATTERN (x), PATTERN (y));
    };
  for (size_t t = 0; t + len <= tensix.size (); ++t)
    {
      if (t == s0)
	continue;
      bool match = true;
      for (size_t k = 0; k < len && match; ++k)
	match = same_word (t + k, s0 + k);
      if (match)
	return true;
    }
  return false;
}

/* Commit CH: move the def-use web from OLD_L to NEW_L, then check the
   result on the committed insns.  On any discrepancy revert, record a
   refusal and assert with -fchecking.  Return true if the rename stands.
   If WEB is non-null, record the committed web in it for undo; a web too
   large for the record is refused before any change.  */

static bool
commit_chain (const chain_desc &ch, rvtt_lreg_rename_web *web = nullptr)
{
  basic_block bb = ch.bb;
  unsigned oldr = SFPU_REG_FIRST + ch.old_l;
  unsigned newr = SFPU_REG_FIRST + ch.new_l;
  uint32_t oldbit = 1u << ch.old_l;
  uint32_t newbit = 1u << ch.new_l;
  rtx newreg = gen_rtx_REG (XTT32SImode, newr);
  rtx_insn *w_insn = ch.scan[ch.wi].insn;

  /* Facts recorded before the commit, for checking afterwards.  */
  std::vector<size_t> edited;
  edited.push_back (ch.wi);
  for (size_t r : ch.readers)
    edited.push_back (r);
  std::vector<int> pre_len;
  std::vector<uint32_t> pre_write;
  for (size_t e : edited)
    {
      pre_len.push_back (get_attr_length (ch.scan[e].insn));
      pre_write.push_back (ch.scan[e].fx.lreg_write);
    }

  /* Refuse a web too large for the undo record before changing
     anything.  */
  if (web
      && edited.size () + (ch.close_reads ? 1 : 0)
	 > RVTT_LREG_RENAME_WEB_MAX)
    {
      refuse_chain ("regrename-web-record-overflow", w_insn, bb);
      return false;
    }

  /* Record the target's references outside the span (none for a
     block-free target); they must be unchanged after the commit.  */
  size_t span_end
    = ch.close == ch.scan.size () ? ch.scan.size () - 1 : ch.close;
  std::vector<rtx_insn *> outside;
  for (size_t i = 0; i < ch.scan.size (); ++i)
    if ((i < ch.wi || i > span_end) && (ch.scan[i].touch & newbit))
      outside.push_back (ch.scan[i].insn);
  gcc_assert (ch.temporal || outside.empty ());

  /* Only the writer's outputs are edited; a read of the source register
     in the writer is a read of its previous live range and must stay
     (see analyze_chain).  Readers are edited throughout.  */
  {
    rtx_insn *insn = ch.scan[ch.wi].insn;
    extract_insn (insn);
    bool renamed_op[MAX_RECOG_OPERANDS] = {};
    for (int oi = 0; oi < recog_data.n_operands; ++oi)
      {
	rtx op = recog_data.operand[oi];
	if (REG_P (op) && REGNO (op) == oldr
	    && recog_data.operand_type[oi] != OP_IN)
	  {
	    validate_change (insn, recog_data.operand_loc[oi],
			     gen_rtx_REG (GET_MODE (op), newr), true);
	    renamed_op[oi] = true;
	  }
      }
    for (int di = 0; di < recog_data.n_dups; ++di)
      if (renamed_op[recog_data.dup_num[di]])
	{
	  rtx dup = *recog_data.dup_loc[di];
	  validate_change (insn, recog_data.dup_loc[di],
			   gen_rtx_REG (GET_MODE (dup), newr), true);
	}
  }
  for (size_t e : edited)
    if (e != ch.wi)
      queue_reg_replacements (ch.scan[e].insn, &PATTERN (ch.scan[e].insn),
			      oldr, newreg);
  if (ch.close_reads)
    {
      /* The close both overwrites the register and reads the chain
	 value: move only its OP_IN operands (and their match_dups).  */
      rtx_insn *close_insn = ch.scan[ch.close].insn;
      extract_insn (close_insn);
      bool renamed_op[MAX_RECOG_OPERANDS] = {};
      for (int oi = 0; oi < recog_data.n_operands; ++oi)
	{
	  rtx op = recog_data.operand[oi];
	  if (REG_P (op) && REGNO (op) == oldr
	      && recog_data.operand_type[oi] == OP_IN)
	    {
	      validate_change (close_insn, recog_data.operand_loc[oi],
			       gen_rtx_REG (GET_MODE (op), newr), true);
	      renamed_op[oi] = true;
	    }
	}
      for (int di = 0; di < recog_data.n_dups; ++di)
	if (renamed_op[recog_data.dup_num[di]])
	  {
	    rtx dup = *recog_data.dup_loc[di];
	    validate_change (close_insn, recog_data.dup_loc[di],
			     gen_rtx_REG (GET_MODE (dup), newr), true);
	  }
    }
  if (!apply_change_group ())
    {
      refuse_chain ("regrename-constraint", w_insn, bb);
      return false;
    }

  /* Check the committed result.  */
  const char *diverged = nullptr;
  for (size_t ei = 0; ei < edited.size () && !diverged; ++ei)
    {
      rtx_insn *insn = ch.scan[edited[ei]].insn;
      if (recog_memoized (insn) < 0
	  || get_attr_type (insn) != TYPE_TENSIX
	  || get_attr_length (insn) != pre_len[ei])
	{
	  diverged = "edited insn shape";
	  break;
	}
      xtt_effect_set fx = rvtt_insn_effects (insn);
      if (fx.opaque)
	diverged = "edited insn effects opaque";
      else if (edited[ei] == ch.wi)
	{
	  /* Every output must be the target and none the source.  An
	     explicit input of the target means a real source read was
	     rewritten.  Inputs of the source register's previous live
	     range may remain.  */
	  if (fx.lreg_write != newbit)
	    diverged = "writer web";
	  else
	    {
	      extract_insn (insn);
	      for (int oi = 0; oi < recog_data.n_operands; ++oi)
		{
		  rtx op = recog_data.operand[oi];
		  if (!REG_P (op))
		    continue;
		  if (recog_data.operand_type[oi] == OP_IN
		      && REGNO (op) == newr)
		    diverged = "writer source rewritten";
		  if (recog_data.operand_type[oi] != OP_IN
		      && REGNO (op) == oldr)
		    diverged = "writer output kept source";
		}
	    }
	}
      else if (!(fx.lreg_read & newbit) || (fx.lreg_read & oldbit)
	       || fx.lreg_write != pre_write[ei])
	diverged = "reader web";
      if (!diverged && edited[ei] != ch.wi
	  && count_reg_occurrences (PATTERN (insn), oldr))
	diverged = "stale source reference";
    }
  if (!diverged && ch.close != ch.scan.size ())
    {
      rtx_insn *close_insn = ch.scan[ch.close].insn;
      xtt_effect_set fx = rvtt_insn_effects (close_insn);
      if (!(fx.lreg_write & oldbit))
	diverged = "close no longer kills";
      else if (ch.close_reads)
	{
	  /* The moved reads must now use the target, and no OP_IN read
	     of the source may remain.  */
	  if (!(fx.lreg_read & newbit))
	    diverged = "reading close web";
	  else if (recog_memoized (close_insn) < 0)
	    diverged = "reading close shape";
	  else
	    {
	      extract_insn (close_insn);
	      for (int oi = 0; oi < recog_data.n_operands; ++oi)
		{
		  rtx op = recog_data.operand[oi];
		  if (REG_P (op) && REGNO (op) == oldr
		      && recog_data.operand_type[oi] == OP_IN)
		    diverged = "stale reading-close reference";
		}
	    }
	}
    }
  if (!diverged)
    {
      /* Every position referencing the target must be an edited web
	 member, the reading close, or one of the recorded references
	 outside the span.  */
      std::vector<span_insn> rescan;
      scan_block (bb, &rescan);
      unsigned hits = 0, strays = 0;
      for (const span_insn &si : rescan)
	{
	  if (!(si.touch & newbit))
	    continue;
	  ++hits;
	  bool known = ch.close_reads && si.insn == ch.scan[ch.close].insn;
	  for (size_t e : edited)
	    if (si.insn == ch.scan[e].insn)
	      known = true;
	  for (rtx_insn *o : outside)
	    if (si.insn == o)
	      known = true;
	  if (!known)
	    ++strays;
	}
      if (strays
	  || hits != edited.size () + (ch.close_reads ? 1 : 0)
		     + outside.size ())
	diverged = "target reference scan";
    }
  /* The checks above see only BB, not a target that is live through
     the block around a loop.  A block-free target relies on DF
     liveness, so it must not have been committed in a function with
     opaque instructions.  Temporal targets are redefined within the
     block and do not depend on DF.  */
  if (!diverged && !ch.temporal && function_has_opaque_insn_p (cfun))
    diverged = "whole-block target committed in opaque function"
	       " (DF hard-register liveness untrusted)";
  if (diverged)
    {
      /* Revert the web and refuse.  For a reading close, the only
	 target references are the moved inputs, so replacing the whole
	 pattern reverts exactly those.  */
      rtx oldreg_rtx = gen_rtx_REG (XTT32SImode, oldr);
      for (size_t e : edited)
	queue_reg_replacements (ch.scan[e].insn,
				&PATTERN (ch.scan[e].insn), newr,
				oldreg_rtx);
      if (ch.close_reads)
	queue_reg_replacements (ch.scan[ch.close].insn,
				&PATTERN (ch.scan[ch.close].insn), newr,
				oldreg_rtx);
      bool reverted = apply_change_group ();
      if (dump_file)
	fprintf (dump_file,
		 "Lreg chain rename POST-COMMIT DIVERGENCE (%s),"
		 " reverted=%d in bb %d\n", diverged, reverted, bb->index);
      refuse_chain ("regrename-postcommit-divergence", w_insn, bb);
      if (flag_checking)
	gcc_assert (reverted && !"lreg chain rename post-commit divergence");
      return false;
    }

  /* Update DF insn references, which the scheduler reads.  Block
     liveness needs no update.  */
  for (size_t e : edited)
    df_insn_rescan (ch.scan[e].insn);
  if (ch.close_reads)
    df_insn_rescan (ch.scan[ch.close].insn);

  if (dump_file)
    {
      int words = 0;
      for (size_t e : edited)
	words += get_attr_length (ch.scan[e].insn) / 4;
      fprintf (dump_file,
	       "Lreg chain rename%s: L%d -> L%d in bb %d (def uid=%d,"
	       " %zu readers, close=%s)\n",
	       ch.temporal ? " (temporal)" : "",
	       ch.old_l, ch.new_l, bb->index, INSN_UID (w_insn),
	       ch.readers.size (),
	       ch.close == ch.scan.size () ? "dead-at-exit"
	       : ch.close_reads ? "kill+read" : "kill");
      fprintf (dump_file,
	       "Lreg chain scan: %d words before == %d words after,"
	       " register fields only\n", words, words);
    }
  if (web)
    {
      web->old_l = ch.old_l;
      web->new_l = ch.new_l;
      web->n_insns = 0;
      for (size_t e : edited)
	web->insns[web->n_insns++] = ch.scan[e].insn;
      if (ch.close_reads)
	web->insns[web->n_insns++] = ch.scan[ch.close].insn;
    }
  n_chain_renamed++;
  return true;
}

const pass_data pass_data_rvtt_lreg_rename_chains =
{
  RTL_PASS,
  "rvtt_lreg_rename_chains",
  OPTGROUP_OTHER,
  TV_NONE,
  0,
  0,
  0,
  0,
  0,
};

class pass_rvtt_lreg_rename_chains : public rtl_opt_pass
{
public:
  pass_rvtt_lreg_rename_chains (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_lreg_rename_chains, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX && riscv_tt_opt_lreg_rename_chains > 0;
  }

  /* Return true if the candidate at SCAN[I] is a latency-0 pure-value
     definition with invariant inputs whose register collides with another
     write, in a self-loop row with a stall.  Standalone mode renames these
     first, so that free registers go to the chains blocking stall fills
     before general chains.  */
  static bool
  fill_blocking_candidate_p (const std::vector<span_insn> &scan, size_t i)
  {
    const span_insn &w = scan[i];
    uint32_t row_writes = 0;
    bool row_writes_cc = false;
    for (const span_insn &si : scan)
      if (si.kind == span_insn::SI_TENSIX && si.insn != w.insn)
	{
	  row_writes |= si.fx.lreg_write;
	  row_writes_cc |= si.fx.cc_write;
	}
    if (w.fx.result_latency != 0
	|| w.fx.cc_write
	|| (w.fx.cc_read && row_writes_cc)
	|| w.fx.config_dests_written || w.fx.addr_mod_slot_write
	|| w.fx.rwc.kind != xtt_rwc_effect_t::NONE
	|| w.fx.dst_mem_read || w.fx.dst_mem_write
	|| popcount_hwi (w.fx.lreg_write) != 1)
      return false;
    /* Invariant inputs: nothing another member writes.  */
    if ((w.fx.lreg_read & ~w.fx.lreg_write) & row_writes)
      return false;
    /* Its register is also written elsewhere in the row.  */
    return (w.fx.lreg_write & row_writes) != 0;
  }

  unsigned execute (function *fn) final override
  {
    df_analyze ();
    n_chain_renamed = 0;
    bool fn_has_opaque = function_has_opaque_insn_p (fn);
    basic_block bb;
    FOR_EACH_BB_FN (bb, fn)
      {
	/* Standalone mode runs two phases per block, rescanning after
	   every commit:
	   1. chains that block stall fills (see the predicate above), in
	      row order, on self-loop rows with a stall;
	   2. every remaining chain whose register collides with another
	      write.
	   Both phases use the same analysis, pricing and commit.  */
	std::vector<row_member> row;
	const char *row_reason = nullptr;
	bool stall_row = rename_row_p (bb, &row, &row_reason)
		      && row_has_audited_stall_p (row);
	for (int phase = stall_row ? 0 : 1; phase < 2; ++phase)
	  {
	    bool progress = true;
	    unsigned attempts = 0;
	    std::vector<bool> tried;
	    while (progress && ++attempts < 64)
	      {
		progress = false;
		std::vector<span_insn> scan;
		scan_block (bb, &scan);
		if (tried.size () < scan.size ())
		  tried.resize (scan.size (), false);
		for (size_t i = 0; i < scan.size (); ++i)
		  {
		    if (tried[i] || scan[i].kind != span_insn::SI_TENSIX
			|| scan[i].fx.opaque
			|| popcount_hwi (scan[i].fx.lreg_write) != 1)
		      continue;
		    uint32_t bit = scan[i].fx.lreg_write;
		    if (bit & ~0xFFu)
		      continue;
		    if (phase == 0)
		      {
			if (!fill_blocking_candidate_p (scan, i))
			  continue;
		      }
		    else
		      {
			/* Another position also writes this register.  Chains
			   without such a collision are left to the service
			   interface.  */
			bool collision = false;
			for (size_t j = 0; j < scan.size () && !collision;
			     ++j)
			  if (j != i && scan[j].kind == span_insn::SI_TENSIX
			      && (scan[j].fx.lreg_write & bit))
			    collision = true;
			if (!collision)
			  continue;
		      }
		    tried[i] = true;
		    chain_desc ch;
		    if (!analyze_chain (bb, scan, i, fn_has_opaque, -1, &ch))
		      continue;
		    /* Phase 1 takes only chains closed by a write inside the
		       row that does not read the chain value.  */
		    if (phase == 0
			&& (ch.close == ch.scan.size () || ch.close_reads))
		      continue;
		    /* Do not break up repeated sequences (see the file
		       header).  */
		    if (chain_in_periodic_window_p (ch))
		      {
			refuse_chain ("regrename-periodic-window",
				      scan[i].insn, bb);
			continue;
		      }
		    if (!span_no_worse_p (ch))
		      continue;
		    if (commit_chain (ch))
		      {
			progress = true;
			break;	/* re-scan */
		      }
		  }
	      }
	    /* The tried set is per phase, so phase 2 reconsiders
	       candidates phase 1 skipped.  */
	  }
      }
    if (dump_file)
      fprintf (dump_file, "Lreg chain rename: renames=%u\n",
	       n_chain_renamed);
    if (n_chain_renamed)
      df_analyze ();
    return 0;
  }
};

} /* anonymous namespace */

/* Try to rename the chain of DEF_INSN's single-LREG definition in BB to
   TARGET_LREG (an L index, or -1 for the lowest free one).  Return true if
   the rename was committed.  Block-free targets are not priced here; the
   caller prices them.  Temporal targets must still show a strict gain
   unless the caller passes RVTT_RENAME_SHAPE_OWNED.  DF liveness must be
   current on entry and stays valid.  If WEB is non-null, record the
   committed web for rvtt_lreg_rename_web_undo.  */

bool
rvtt_lreg_rename_chain (basic_block bb, rtx_insn *def_insn, int target_lreg,
			rvtt_lreg_rename_web *web,
			enum rvtt_lreg_rename_pricing pricing)
{
  std::vector<span_insn> scan;
  scan_block (bb, &scan);
  size_t wi = scan.size ();
  for (size_t i = 0; i < scan.size (); ++i)
    if (scan[i].insn == def_insn)
      {
	wi = i;
	break;
      }
  if (wi == scan.size () || scan[wi].kind != span_insn::SI_TENSIX)
    {
      rvtt_refuse_by_name_at ("regrename-effect-veto", def_insn, dump_file,
			      "Lreg chain rename refused:"
			      " regrename-effect-veto in bb %d\n", bb->index);
      return false;
    }
  chain_desc ch;
  if (!analyze_chain (bb, scan, wi, function_has_opaque_insn_p (cfun),
		      target_lreg, &ch))
    return false;
  /* Price temporal targets here too: their costs (lost fill
     opportunities, broken replay windows) are invisible to the caller's
     own acceptance.  Callers that own the row's whole delivery shape
     (RVTT_RENAME_SHAPE_OWNED) account for them themselves.  */
  if (ch.temporal && pricing == RVTT_RENAME_PRICE_TEMPORAL
      && !span_no_worse_p (ch))
    return false;
  return commit_chain (ch, web);
}

/* Undo a committed service rename by moving every occurrence of the
   target register in the recorded web members back to the source.  This
   is exact because the post-commit check showed the members' only
   target references are the moved ones.  */

void
rvtt_lreg_rename_web_undo (const rvtt_lreg_rename_web &web)
{
  unsigned oldr = SFPU_REG_FIRST + web.old_l;
  unsigned newr = SFPU_REG_FIRST + web.new_l;
  rtx oldreg = gen_rtx_REG (XTT32SImode, oldr);
  for (unsigned i = 0; i < web.n_insns; ++i)
    queue_reg_replacements (web.insns[i], &PATTERN (web.insns[i]),
			    newr, oldreg);
  bool ok = apply_change_group ();
  gcc_assert (ok);
  for (unsigned i = 0; i < web.n_insns; ++i)
    df_insn_rescan (web.insns[i]);
  if (dump_file)
    fprintf (dump_file, "Lreg chain rename undo: L%d -> L%d (%u insns)\n",
	     web.new_l, web.old_l, web.n_insns);
}

/* Create the chain-rename pass.  It runs before postreload and is
   enabled by -mtt-tensix-optimize-lreg-rename-chains or its alias
   -mtt-tensix-optimize-lreg-rename.  */

rtl_opt_pass *
make_pass_rvtt_lreg_rename_chains (gcc::context *ctxt)
{
  return new pass_rvtt_lreg_rename_chains (ctxt);
}
