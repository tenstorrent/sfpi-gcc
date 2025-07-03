/* Pass to schedule SFPU insns (insert nops)
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

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
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "cfghooks.h"
#include "df.h"
#include "memmodel.h"
#include "tm_p.h"
#include "insn-config.h"
#include "regs.h"
#include "emit-rtl.h"
#include "recog.h"
#include "cgraph.h"
#include "tree-pretty-print.h" /* for dump_function_header */
#include "varasm.h"
#include "insn-attr.h"
#include "conditions.h"
#include "flags.h"
#include "output.h"
#include "except.h"
#include "rtl-error.h"
#include "toplev.h" /* exact_log2, floor_log2 */
#include "reload.h"
#include "intl.h"
#include "cfgrtl.h"
#include "debug.h"
#include "tree-pass.h"
#include "tree-ssa.h"
#include "cfgloop.h"
#include "stringpool.h"
#include "attribs.h"
#include "asan.h"
#include "rtl-iter.h"
#include "print-rtl.h"
#include "function-abi.h"
#include "rvtt.h"
#include "rvtt-protos.h"

#if 0
// FIXME: should dump to the dump file.
#define DUMP(...) fprintf (stderr, __VA_ARGS__)
#else
// Sadly deploying sizeof here results in a bunch of LHS or comma has
// no effect warnings.
#define DUMP(...) (void)0
#endif

using namespace std;

static void
insert_nop_after (rtx_insn *insn)
{
  rtx nop = NULL_RTX;

  if (TARGET_RVTT_WH)
    nop = gen_rvtt_wh_sfpnop();
  else if (TARGET_RVTT_BH)
    nop = gen_rvtt_bh_sfpnop();
  else
    gcc_unreachable ();
  emit_insn_after (nop, insn);
}

static bool reg_referenced_p(unsigned int regno, rtx_insn *insn)
{
  int noperands = rvtt_get_insn_operand_count(insn);

  for (int i = 0; i < noperands; i++) {
    // FIXME: we're looking at all operands, not just input ops.
    // That's probably harmlessly more work. Divergent control flow
    // could cause us to overwrite the output register and not need a
    // NOP though.
    rtx operand = rvtt_get_insn_operand(i, insn);
    if (GET_CODE(operand) == REG &&
	regno == rvtt_sfpu_regno(operand)) {
      return true;
    }
  }

  return false;
}

/* Walk the BB graph from BB:probe_insn until we meet an SPU
   insn. Return true if the SPU insn is dependent.  Populate VISITED
   with the BB's we marked.  */

static bool
walk_blocks(int regno, basic_block bb, rtx_insn *probe_insn, bool check_probe,
	    std::vector<basic_block> &visited)
{
  if (check_probe)
    {
      // Each block, other than the starting block, should only be
      // walked once -- don't get trapped in a loop of non-SPU
      // insns. The starting block should be walked exactly twice, if
      // reachable.
      if (bb->flags & BB_VISITED)
	return false;
      bb->flags |= BB_VISITED;
      visited.push_back (bb);
    }

  const rvtt_insn_data *insn_data;
  if (probe_insn
      && rvtt_get_next_insn (&insn_data, &probe_insn, probe_insn, check_probe))
    {
      // We've met an SPU insn. If it is dependent, we'll need to
      // insert a nop.  If it is non-dependent, it's filling the
      // original insn's shadow, so any following dependent insn will
      // be fine. Either way, we're done searching this BB.
      bool is_dependent = reg_referenced_p (regno, probe_insn);
      DUMP ("Found %sdependent insn at %s\n",
	    is_dependent ? "" : "non-", probe_insn->name);
      return is_dependent;
    }

  // Walk all the successors
  edge_iterator ei;
  edge e;
  FOR_EACH_EDGE (e, ei, bb->succs)
    if (walk_blocks (regno, e->dest, BB_HEAD (e->dest), true, visited))
      return true;

  // We didn't find anything
  return false;
}

/* Insert a nop after ORIG_INSN, if there is a depednent SPU insn in
   its latency shadow.  */

static void
dynamic_schedule_wh_bh (basic_block bb, rtx_insn *orig_insn,
			std::vector<basic_block> &visited)
{
  gcc_assert (visited.empty ());

  if (walk_blocks (rvtt_get_insn_dst_regno (orig_insn) - SFPU_REG_FIRST,
		   bb, orig_insn, false, visited)) {
    insert_nop_after (orig_insn);
    DUMP ("Inserting nop after %s\n", orig_insn->name);
  }

  for (auto *bb : visited)
    bb->flags &= ~BB_VISITED;
  visited.clear ();
}

// Perform instruction scheduling
//
// For wormhole/blackhole there is dynamic and static schedule.
// Dynamic scheduling requires adding a NOP into the single
// instruction shadow of any instruction that uses the MAD unit, which
// are: MAD, LUT, LUT32, MUL(I), ADD(I). An alternative to
// NOP-insertion is to move a non-dependent instruction into that
// slot, but we do not implement that.
//
// SWAP/SHFT2 are statically scheduled and always require a NOP.
//
// On GS, we just have to look for loads followed by stores and insert a NOP
// if found.  Have to check across BBs and for non-imm loads/stores.  This is
// "dynamic" since it depends on the instructions
static void transform ()
{
  DUMP ("Schedule pass on: %s\n", function_name(cfun));

  std::vector<basic_block> visited;

  basic_block bb;
  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  const rvtt_insn_data *insnd;

	  if (NONDEBUG_INSN_P (insn) && rvtt_p (&insnd, insn)
	      && insnd->schedule_p ()
	      && (!insnd->schedule_in_arg_p ()
		  || insnd->schedule_from_arg_p (insn)))
	    {
	      if (insnd->schedule_dynamic_p (insn))
		{
		  DUMP ("  dynamic scheduling %s\n", insnd->name);
		  // Reserve space now we know we need it
		  visited.reserve (n_basic_blocks_for_fn (cfun));
		  dynamic_schedule_wh_bh (bb, insn, visited);
		}
	      else if (TARGET_RVTT_WH || TARGET_RVTT_BH)
		{
		  DUMP ("  static scheduling %s\n", insnd->name);
		  int count = insnd->schedule_static_nops (insn);
		  for (int i = 0; i < count; i++)
		    insert_nop_after(insn);
		}
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
    return TARGET_RVTT;
  }

  virtual unsigned execute (function *) override
  {
    transform ();
    return 0;
  }
}; // class pass_rvtt_schedule

} // anon namespace

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}
