/* Pass to schedule SFPU insns (insert nops)
   Copyright (C) 2022 Free Software Foundation, Inc.
   Contributed by Paul Keller (pkeller@tenstorrent.com).

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
#include "config/riscv/rvtt.h"
#include "config/riscv/rvtt-protos.h"

#define DUMP(...) //fprintf(stderr, __VA_ARGS__)

using namespace std;

static void insert_nop_after(rtx_insn *insn)
{
  if (flag_grayskull)
    {
      emit_insn_after(gen_rvtt_gs_sfpnop(), insn);
    }
  else
    {
      emit_insn_after(gen_rvtt_wh_sfpnop(), insn);
    }
}

static bool reg_referenced_p(unsigned int regno, rtx_insn *insn)
{
  int noperands = rvtt_get_insn_operand_count(insn);

  for (int i = 0; i < noperands; i++) {
    rtx operand = rvtt_get_insn_operand(i, insn);
    if (GET_CODE(operand) == REG &&
	regno == rvtt_sfpu_regno(operand)) {
      return true;
    }
  }

  return false;
}

static void dynamic_schedule_gs(rtx_insn *insn)
{
  rtx_insn *next_insn;
  const rvtt_insn_data *next_insnd;

  if (rvtt_get_next_insn(&next_insnd, &next_insn, insn, false, INSN_FLAGS_NON_SFPU))
    {
      if (next_insnd->schedule_has_dynamic_dependency_p(next_insn))
	{
	  insert_nop_after(insn);
	}
    }
  else
    {
      // The stmt needing scheduling is the last stmt in the BB
      // If any child has a dependent insn at the start, then we add a nop at
      // the end of this BB
      DUMP(" last stmt in BB, checking children\n");

      basic_block bb = BLOCK_FOR_INSN(insn);
      edge_iterator ei;
      edge e;
      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  rtx_insn *bb_start_insn = BB_HEAD(e->dest);
	  if (bb_start_insn != nullptr &&
	      rvtt_get_next_insn(&next_insnd, &next_insn, bb_start_insn, true, INSN_FLAGS_NON_SFPU) &&
	      next_insnd->schedule_has_dynamic_dependency_p(next_insn))
	    {
	      DUMP(" found a child w/ a dependency, inserting nop\n");
	      insert_nop_after(insn);
	      break;
	    }
	}
    }
}

static void insert_nop_if_needed_wh(rtx_insn *insn,
				    const rvtt_insn_data *next_insnd,
				    rtx_insn *next_insn)
{
  int regint = rvtt_get_insn_dst_regno(insn) - SFPU_REG_FIRST;
  gcc_assert(regint != -1 - SFPU_REG_FIRST);
  unsigned int regno = regint;

  DUMP("  next insn, reg: %s %d\n", next_insnd->name, regno);

  if (reg_referenced_p(regno, next_insn))
    {
      DUMP("     next stmt (%s) uses lhs, inserting NOP\n", next_insnd->name);
      insert_nop_after(insn);
    }
  else
    {
      DUMP("     next stmt (%s) is independent, all good\n", next_insnd->name);
    }
}

static void dynamic_schedule_wh(rtx_insn *insn)
{
  rtx_insn *next_insn;
  const rvtt_insn_data *next_insnd;

  if (rvtt_get_next_insn(&next_insnd, &next_insn, insn))
    {
      insert_nop_if_needed_wh(insn, next_insnd, next_insn);
    }
  else
    {
      // The stmt needing scheduling is the last stmt in the BB
      // If any child has a dependent insn at the start, then we add a nop at
      // the end of this BB
      DUMP(" last stmt in BB, checking children\n");

      basic_block bb = BLOCK_FOR_INSN(insn);
      edge_iterator ei;
      edge e;
      FOR_EACH_EDGE(e, ei, bb->succs)
	{
	  rtx_insn *bb_start_insn = BB_HEAD(e->dest);
	  if (bb_start_insn != nullptr &&
	      rvtt_get_next_insn(&next_insnd, &next_insn, bb_start_insn))
	    {
	      DUMP(" found a child w/ a dependency, inserting nop\n");
	      insert_nop_if_needed_wh(insn, next_insnd, next_insn);
	      break;
	    }
	}
    }
}

// Perform instruction scheduling
//
// For wormhole there is dynamic and static schedule.  Dynamic scheduling
// requires adding a NOP or moving a non-dependent instruction into the single
// instruction shadow of ny instruction which uses the MAD unit, which are:
// MAD, LUT, LUT32, MUL(I), ADD(I).  Presently, this only inserts a NOP.
//
// SWAP/SHFT2 are statically scheduled and always require a NOP.
//
// On GS, we just have to look for loads followed by stores and insert a NOP
// if found.  Have to check across BBs and for non-imm loads/stores.  This is
// "dynamic" since it depends on the instructions
static void transform ()
{
  DUMP("Schedule pass on: %s\n", function_name(cfun));

  basic_block bb;

  FOR_EACH_BB_FN (bb, cfun)
    {
      rtx_insn *insn;

      FOR_BB_INSNS (bb, insn)
	{
	  const rvtt_insn_data *insnd;

	  if (NONDEBUG_INSN_P(insn) &&
	      rvtt_p(&insnd, insn))
	    {
	      if (insnd->schedule_p() &&
		  (!insnd->schedule_in_arg_p() ||
		   insnd->schedule_from_arg_p(insn)))
		{
		  if (insnd->schedule_dynamic_p(insn))
		    {
		      DUMP("  dynamic scheduling %s\n", insnd->name);
		      if (flag_grayskull)
			{
			  dynamic_schedule_gs(insn);
			}
		      else
			{
			  dynamic_schedule_wh(insn);
			}
		    }
		  else if (flag_wormhole)
		    {
		      DUMP("  static scheduling %s\n", insnd->name);
		      int count = insnd->schedule_static_nops(insn);
		      for (int i = 0; i < count; i++)
			{
			  insert_nop_after(insn);
			}
		    }
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

  virtual unsigned int execute (function *);
}; // class pass_rvtt_schedule

} // anon namespace

/* Entry point to rvtt_schedule pass.	*/
unsigned int
pass_rvtt_schedule::execute (function *)
{
  if (flag_grayskull || flag_wormhole)
    {
      transform ();
    }

  return 0;
}

rtl_opt_pass *
make_pass_rvtt_schedule (gcc::context *ctxt)
{
  return new pass_rvtt_schedule (ctxt);
}
