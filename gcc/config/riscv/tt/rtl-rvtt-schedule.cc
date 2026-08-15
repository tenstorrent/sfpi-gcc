/* Pass to schedule tensix insns (insert nops)
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

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "tree-pass.h"
#include "df.h"
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt.h"

namespace {

struct insn_regs
{
  HARD_REG_SET uses;
  HARD_REG_SET defs;
};

static bool
collect_sfpu_regs (rtx_insn *insn, insn_regs *regs)
{
  CLEAR_HARD_REG_SET (regs->uses);
  CLEAR_HARD_REG_SET (regs->defs);

  for (df_ref ref = DF_INSN_USES (insn); ref;
       ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return false;
      SET_HARD_REG_BIT (regs->uses, regno);
    }
  for (df_ref ref = DF_INSN_DEFS (insn); ref;
       ref = DF_REF_NEXT_LOC (ref))
    {
      unsigned regno = DF_REF_REGNO (ref);
      if (regno >= FIRST_PSEUDO_REGISTER || !SFPU_REG_P (regno))
	return false;
      SET_HARD_REG_BIT (regs->defs, regno);
    }

  return !hard_reg_set_empty_p (regs->defs);
}

static bool
latency_reorderable_p (rtx_insn *insn, insn_regs *regs)
{
  return NONDEBUG_INSN_P (insn)
    && recog_memoized (insn) >= 0
    && get_attr_type (insn) == TYPE_TENSIX
    && get_attr_xtt_latency_reorder (insn) == XTT_LATENCY_REORDER_SAFE
    && !contains_mem_rtx_p (PATTERN (insn))
    && collect_sfpu_regs (insn, regs);
}

static rtx_insn *
next_issued_insn (basic_block bb, rtx_insn *insn)
{
  for (insn = NEXT_INSN (insn); insn != NEXT_INSN (BB_END (bb));
       insn = NEXT_INSN (insn))
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      if (recog_memoized (insn) < 0 || get_attr_type (insn) != TYPE_TENSIX
	  || !get_attr_length (insn))
	return nullptr;
      return insn;
    }
  return nullptr;
}

static bool
intersect_p (const HARD_REG_SET &a, const HARD_REG_SET &b)
{
  return hard_reg_set_intersect_p (a, b);
}

/* Move one independent ready instruction into a single exposed result-latency
   slot.  The existing delay pass runs afterward and remains the authority for
   target scoreboarding and WH/BH/QSR errata.  */
static void
fill_latency_bubbles (function *fn)
{
  df_analyze ();

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    for (rtx_insn *producer = BB_HEAD (bb); producer;)
      {
	rtx_insn *consumer = next_issued_insn (bb, producer);
	rtx_insn *filler = consumer ? next_issued_insn (bb, consumer) : nullptr;
	insn_regs producer_regs, consumer_regs, filler_regs;
	bool moved = (consumer && filler
		      && latency_reorderable_p (producer, &producer_regs)
		      && latency_reorderable_p (consumer, &consumer_regs)
		      && latency_reorderable_p (filler, &filler_regs)
		      && get_attr_xtt_delay_bubbles (producer) == 1
		      && intersect_p (producer_regs.defs, consumer_regs.uses)
		      && !intersect_p (producer_regs.defs, filler_regs.uses)
		      && !intersect_p (producer_regs.defs, filler_regs.defs)
		      && !intersect_p (consumer_regs.defs, filler_regs.uses)
		      && !intersect_p (consumer_regs.uses, filler_regs.defs)
		      && !intersect_p (consumer_regs.defs, filler_regs.defs));

	if (moved)
	  {
	    int filler_uid = INSN_UID (filler);
	    int producer_uid = INSN_UID (producer);
	    reorder_insns (filler, filler, producer);
	    if (dump_file)
	      fprintf (dump_file,
		       "Latency-fill moved uid=%d after producer uid=%d "
		       "target=%s\n", filler_uid, producer_uid,
		       TARGET_XTT_TENSIX_WH ? "wh" :
		       TARGET_XTT_TENSIX_BH ? "bh" : "qsr");
	    producer = consumer;
	  }
	else
	  producer = NEXT_INSN (producer);

	if (!producer || producer == NEXT_INSN (BB_END (bb)))
	  break;
      }
}

} // anonymous namespace

/* The generated target cost hook deliberately returns one for the existing
   STATIC/DYNAMIC contracts.  Do not generalize this to instruction distance:
   that needs a separate walk over emitted Tensix insns.  */
static unsigned
rvtt_delay_bubbles (rtx_insn *insn)
{
  unsigned nops = get_attr_xtt_delay_bubbles (insn);
  gcc_assert (nops <= 1);
  return nops;
}

/* Walk the BB graph from PROBE_INSN until we meet a TENSIX insn. Return true
   if REGNO != 0 and the TENSIX insn is dependent.  Return true if REGNO == 0
   and the TENSIX insn is not a NOP. Return false in all other cases. If we
   meet the end of a block, recurse into successor blocks and return the first
   true we get.  Populate VISITED with the BB's we marked. Takes advantage of
   no multi-register values, no return values and no clobbers of TENSIX
   registers. */

static bool
find_next_insn (std::vector<basic_block> &visited, basic_block bb, int regno,
		rtx_insn *probe_insn, bool check_probe = false)
{
  if (bb->flags & BB_VISITED)
    return false;

  if (check_probe)
    {
      // Each block, other than the starting block, should only be
      // walked once -- don't get trapped in a loop of non-TENSIX
      // insns. The starting block should be walked exactly twice, if
      // reachable from itself.
      bb->flags |= BB_VISITED;
      visited.push_back (bb);
    }

  if (probe_insn)
    for (; probe_insn != NEXT_INSN (BB_END (bb));
	 check_probe = true, probe_insn = NEXT_INSN (probe_insn))
      {
	if (!check_probe)
	  continue;

	if (GET_CODE (probe_insn) != INSN)
	  continue;
	rtx pattern = PATTERN (probe_insn);

	if (GET_CODE (pattern) == USE)
	  // The case where this would be a dependency does not arise.
	  continue;
	if (GET_CODE (pattern) == CLOBBER)
	  continue;

	if (get_attr_type (probe_insn) != TYPE_TENSIX)
	  continue;

	if (!regno)
	  {
	    bool is_nop = recog_memoized (probe_insn) == CODE_FOR_rvtt_sfpnop;
	    if (dump_file)
	      {
		fprintf (dump_file, "Found %snop insn ", is_nop ? "" : "non-");
		dump_insn_slim (dump_file, probe_insn);
	      }
	    return !is_nop;
	  }

	auto reg_used_p = [] (auto self, unsigned regno, rtx rtl) -> bool
	{
	  switch (GET_CODE (rtl))
	    {
	    default:
	      // Unknown tensix insn component
	      gcc_unreachable ();

	    case PARALLEL:
	    case UNSPEC:
	    case UNSPEC_VOLATILE:
	      {
		// All 3 have the vector at position 0
		auto &vec = XVEC (rtl, 0);
		for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
		  if (self (self, regno, RTVEC_ELT (vec, ix)))
		    return true;
	      }
	      break;

	    case SET:
	      if (self (self, regno, SET_SRC (rtl)))
		return true;
	      break;

	    case REG:
	      if (regno == REGNO (rtl))
		return true;
	      break;

	    case CONST_INT:
	    case MEM:
	    case CLOBBER:
	    case USE:
	      break;
	    }
	  return false;
	};

	bool is_dependent = reg_used_p (reg_used_p, regno, pattern);

	if (is_dependent)
	  if (unsigned mask =
	      TARGET_XTT_TENSIX_BH ? XTT_DYNAMIC_BUG_BH :
	      TARGET_XTT_TENSIX_QSR ? XTT_DYNAMIC_BUG_QSR :
	      0)
	    // BH & QSR has scoreboarding, but with bugs
	    if (!(mask & get_attr_xtt_dynamic_bug (probe_insn)))
	      is_dependent = false;

	if (!is_dependent && !get_attr_length (probe_insn))
	  continue;

	if (dump_file)
	  {
	    fprintf (dump_file, "Found %sdependent insn ", is_dependent ? "" : "non-");
	    dump_insn_slim (dump_file, probe_insn);
	  }
	return is_dependent;
      }

  // Walk all the successors
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (find_next_insn (visited, e->dest, regno, BB_HEAD (e->dest), true))
      return true;

  return false;
}

// Perform instruction scheduling. We conditionally insert a nop after
// instructions.

static void
transform (function *fn)
{
  std::vector<basic_block> visited;

  basic_block bb;
  FOR_EACH_BB_FN (bb, fn)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  if (GET_CODE (insn) != INSN)
	    continue;

	  if (recog_memoized (insn) < 0)
	    continue;

	  if (get_attr_type (insn) != TYPE_TENSIX)
	    continue;

	  enum xtt_delay delay = get_attr_xtt_delay (insn);
	  if (delay == XTT_DELAY_NONE)
	    continue;

	  visited.reserve (n_basic_blocks_for_fn (fn));
	  bool insert = false;
	  if (delay == XTT_DELAY_STATIC)
	    {
	      insert = find_next_insn (visited, bb, 0, insn);
	      for (auto *bb : visited)
		bb->flags &= ~BB_VISITED;
	      visited.clear ();
	    }
	  else
	    {
	      gcc_assert (delay == XTT_DELAY_DYNAMIC);
	      auto find_next = [] (auto self, std::vector<basic_block> &visited, basic_block bb,
				   rtx_insn *insn, rtx rtl) -> bool
	      {
		switch (GET_CODE (rtl))
		  {
		  default:
		    gcc_unreachable ();

		  case SET:
		    if (REG_P (SET_DEST (rtl)))
		      {
			unsigned regno = REGNO (SET_DEST (rtl));
			if (SFPU_REG_P (regno))
			  {
			    // Writing to a constant reg falls on the floor
			    bool insert = regno < SFPU_REG_FIRST + SFPU_CREG_IDX_LWM
			      && find_next_insn (visited, bb, regno, insn);

			    for (auto *bb : visited)
			      bb->flags &= ~BB_VISITED;
			    visited.clear ();

			    return insert;
			  }
		      }
		    break;

		  case PARALLEL:
		    {
		      auto &vec = XVEC (rtl, 0);
		      for (unsigned ix = GET_NUM_ELEM (vec); ix--;)
			if (self (self, visited, bb, insn, RTVEC_ELT (vec, ix)))
			  return true;
		    }
		    break;

		  case CLOBBER:
		  case SCRATCH:
		    break;
		  }

		return false;
	      };

	      insert = find_next (find_next, visited, bb, insn, PATTERN (insn));
	    }

	  if (insert)
	    for (unsigned nops = rvtt_delay_bubbles (insn); nops; --nops)
	      emit_insn_after (gen_rvtt_sfpnop (), insn);
	  if (dump_file)
	    {
	      fprintf (dump_file, "%snserting %s nop after ",
		       insert ? "I" : "Not i",
		       delay == XTT_DELAY_STATIC ? "static" : "dynamic");
	      dump_insn_slim (dump_file, insn);
	      fprintf (dump_file, "\n");
	    }
       }
    }
}

namespace {

const pass_data pass_data_rvtt_schedule =
{
  RTL_PASS, /* type */
  "rvtt_schedule", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  0, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_rvtt_schedule : public rtl_opt_pass
{
public:
  pass_rvtt_schedule (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_rvtt_schedule, ctxt)
  {}

  virtual bool gate (function *) override
  {
    return TARGET_XTT_TENSIX;
  }

  virtual unsigned execute (function *fn) override
  {
    if (riscv_tt_opt_latency_schedule)
      fill_latency_bubbles (fn);
    transform (fn);
    return 0;
  }
}; // class pass_rvtt_schedule

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}
