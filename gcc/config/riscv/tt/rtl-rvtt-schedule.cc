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
#include "print-rtl.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-codes.h"
#include "recog.h"
#include "rvtt.h"

/* The scoreboarding stall logic is broken for these consumer
   instructions of a SFPMAD pipeline.  A NOP is still needed between a MAD
   pipeline producer and one of these consumers when the producer writes a
   register that the consumer reads.  */

static bool
is_mad_pipeline_consumer (rtx_insn *insn)
{
  switch (recog_memoized (insn))
    {
    case CODE_FOR_rvtt_sfpand_2op:
    case CODE_FOR_rvtt_sfpand_lv_int:
    case CODE_FOR_rvtt_sfpor_2op:
    case CODE_FOR_rvtt_sfpor_lv_int:
    case CODE_FOR_rvtt_sfpiadd_v_int:
    case CODE_FOR_rvtt_sfpiadd_i_lv_int:
    case CODE_FOR_rvtt_sfpshft_v:
    case CODE_FOR_rvtt_sfpshft_i_bh:
    case CODE_FOR_rvtt_sfpwriteconfig_v:
    case CODE_FOR_rvtt_sfpswap_int:
    case CODE_FOR_rvtt_sfpswap_cst1:
    case CODE_FOR_rvtt_sfpswap_cst2:
    case CODE_FOR_rvtt_sfpswap_cst3:
    case CODE_FOR_rvtt_sfpshft2_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_copy4_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_int:
    case CODE_FOR_rvtt_sfpshft2_subvec_shfl1_dead:
      return true;
    default:
      return false;
    }
}

/* Walk the BB graph from PROBE_INSN until we meet a TENSIX insn. Return true
   if REGNO != 0 and the TENSIX insn is dependent.  Return true if REGNO == 0
   and the TENSIX insn is not a NOP. Return false in all other cases. If we
   meet the end of a block, recurse into successor blocks and return the first
   true we get.  Populate VISITED with the BB's we marked. Takes advantage of
   no multi-register values, no return values and no clobbers of TENSIX
   registers.

   When check_mad_pipeline_only is true, a dependent consumer only
   triggers a NOP if it is one of the mad-pipeline instructions. */

static bool
find_next_insn (std::vector<basic_block> &visited, basic_block bb, int regno,
                rtx_insn *probe_insn, bool check_probe = false,
                bool check_mad_pipeline_only = false)
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

	    CASE_CONST_ANY:
	    case MEM:
	    case CLOBBER:
	    case USE:
	      break;
	    }
	  return false;
	};

	bool is_dependent = reg_used_p (reg_used_p, regno, pattern);

	if (is_dependent && check_mad_pipeline_only
		&& !is_mad_pipeline_consumer (probe_insn))
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
  if (find_next_insn (visited, e->dest, regno, BB_HEAD (e->dest), true,
                      check_mad_pipeline_only))
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

	  enum xtt_delay delay
	    = TARGET_XTT_TENSIX_WH  ? get_attr_xtt_delay_wh (insn)
	    : TARGET_XTT_TENSIX_BH  ? get_attr_xtt_delay_bh (insn)
	    : TARGET_XTT_TENSIX_QSR ? get_attr_xtt_delay_qsr (insn)
	    : (gcc_unreachable (), XTT_DELAY_NONE);

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
	      bool check_mad_pipeline_only = TARGET_XTT_TENSIX_BH;
	      auto find_next = [] (auto self, std::vector<basic_block> &visited, basic_block bb,
				   rtx_insn *insn, rtx rtl,
				   bool check_mad_pipeline_only) -> bool
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
			    bool insert = find_next_insn (visited, bb, regno, insn,
						                      false, check_mad_pipeline_only);

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
			if (self (self, visited, bb, insn,
				      RTVEC_ELT (vec, ix), check_mad_pipeline_only))
			  return true;
		    }
		    break;

		  case CLOBBER:
		  case SCRATCH:
		    break;
		  }

		return false;
	      };

	      insert = find_next (find_next, visited, bb, insn, PATTERN (insn),
				              check_mad_pipeline_only);
	    }

	  if (insert)
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
